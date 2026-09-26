#include "tilly/allocator.h"
#include "tilly/logger.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Thread-local storage for stack allocator
static __thread tilly_allocator_t *tls_stack_allocator = NULL;

// ==================== Arena Allocator ====================

typedef struct {
    uint8_t *base;
    size_t size;
    size_t offset;
    size_t peak;
    pthread_mutex_t lock;
} arena_state_t;

static void *arena_alloc(tilly_allocator_t *alloc, size_t size, size_t align) {
    arena_state_t *state = (arena_state_t *)alloc->state;
    if (!state || !state->base) return NULL;
    
    pthread_mutex_lock(&state->lock);
    
    uintptr_t addr = (uintptr_t)(state->base + state->offset);
    uintptr_t aligned = (addr + align - 1) & ~(uintptr_t)(align - 1);
    size_t padding = aligned - addr;
    
    if (padding > state->size - state->offset || size > state->size - state->offset - padding) {
        pthread_mutex_unlock(&state->lock);
        return NULL;
    }
    
    state->offset += padding + size;
    if (state->offset > state->peak) state->peak = state->offset;
    alloc->used = state->offset;
    alloc->alloc_count++;
    
    void *ptr = (void *)aligned;
    pthread_mutex_unlock(&state->lock);
    return ptr;
}

static void arena_free(tilly_allocator_t *alloc, void *ptr) {
    // Arena allocator doesn't support individual frees
    (void)alloc; (void)ptr;
}

static void arena_reset(tilly_allocator_t *alloc) {
    arena_state_t *state = (arena_state_t *)alloc->state;
    if (state) {
        pthread_mutex_lock(&state->lock);
        state->offset = 0;
        alloc->used = 0;
        pthread_mutex_unlock(&state->lock);
    }
}

static size_t arena_usage(const tilly_allocator_t *alloc) {
    arena_state_t *state = (arena_state_t *)alloc->state;
    return state ? state->offset : 0;
}

// ==================== Pool Allocator ====================

typedef struct pool_block {
    struct pool_block *next;
} pool_block_t;

typedef struct {
    pool_block_t *free_list;
    size_t block_size;
    size_t block_count;
    uint8_t *blocks;
    pthread_mutex_t lock;
} pool_state_t;

static void *pool_alloc(tilly_allocator_t *alloc, size_t size, size_t align) {
    pool_state_t *state = (pool_state_t *)alloc->state;
    if (!state || size > state->block_size || align > _Alignof(max_align_t)) return NULL;
    
    pthread_mutex_lock(&state->lock);
    
    if (!state->free_list) {
        pthread_mutex_unlock(&state->lock);
        return NULL;
    }
    
    pool_block_t *block = state->free_list;
    state->free_list = block->next;
    alloc->used += state->block_size;
    alloc->alloc_count++;
    if (alloc->used > alloc->peak) alloc->peak = alloc->used;
    
    pthread_mutex_unlock(&state->lock);
    return block;
}

static void pool_free(tilly_allocator_t *alloc, void *ptr) {
    pool_state_t *state = (pool_state_t *)alloc->state;
    if (!state || !ptr) return;
    
    pthread_mutex_lock(&state->lock);
    
    uintptr_t position = (uintptr_t)ptr, base = (uintptr_t)state->blocks;
    if (position < base || position - base >= state->block_count * state->block_size ||
        (position - base) % state->block_size) { pthread_mutex_unlock(&state->lock); return; }
    pool_block_t *block = (pool_block_t *)ptr;
    block->next = state->free_list;
    state->free_list = block;
    alloc->used -= state->block_size;
    alloc->free_count++;
    
    pthread_mutex_unlock(&state->lock);
}

static void pool_reset(tilly_allocator_t *alloc) {
    pool_state_t *state = (pool_state_t *)alloc->state;
    if (!state) return;
    
    pthread_mutex_lock(&state->lock);
    state->free_list = NULL;
    for (size_t i = 0; i < state->block_count; i++) {
        pool_block_t *block = (pool_block_t *)(state->blocks + i * state->block_size);
        block->next = state->free_list;
        state->free_list = block;
    }
    alloc->used = 0;
    pthread_mutex_unlock(&state->lock);
}

static size_t pool_usage(const tilly_allocator_t *alloc) {
    pool_state_t *state = (pool_state_t *)alloc->state;
    if (!state) return 0;
    return alloc->used;
}

// ==================== General Allocator (malloc/free) ====================

static void *general_alloc(tilly_allocator_t *alloc, size_t size, size_t align) {
    if (align > _Alignof(max_align_t)) return NULL;
    void *ptr = malloc(size);
    if (ptr) {
        alloc->used += size;
        alloc->alloc_count++;
        if (alloc->used > alloc->peak) alloc->peak = alloc->used;
    }
    return ptr;
}

static void general_free(tilly_allocator_t *alloc, void *ptr) {
    if (!ptr) return;
    // Note: we can't track exact size freed with plain malloc
    alloc->free_count++;
    free(ptr);
}

static void general_reset(tilly_allocator_t *alloc) {
    // General allocator can't be reset
    (void)alloc;
}

static size_t general_usage(const tilly_allocator_t *alloc) {
    return alloc->used;
}

// ==================== Stack Allocator (thread-local) ====================

typedef struct {
    uint8_t *base;
    size_t size;
    size_t offset;
    size_t peak;
    size_t *frame_offsets;
    size_t frame_count;
    size_t frame_capacity;
} stack_state_t;

static void *stack_alloc(tilly_allocator_t *alloc, size_t size, size_t align) {
    stack_state_t *state = (stack_state_t *)alloc->state;
    if (!state || !state->base) return NULL;
    
    uintptr_t addr = (uintptr_t)(state->base + state->offset);
    uintptr_t aligned = (addr + align - 1) & ~(uintptr_t)(align - 1);
    size_t padding = aligned - addr;
    
    if (padding > state->size - state->offset || size > state->size - state->offset - padding) {
        return NULL;
    }
    
    state->offset += padding + size;
    if (state->offset > state->peak) state->peak = state->offset;
    alloc->used = state->offset;
    alloc->alloc_count++;
    
    return (void *)aligned;
}

static void stack_free(tilly_allocator_t *alloc, void *ptr) {
    // Stack allocator doesn't support individual frees, only frame reset
    (void)alloc; (void)ptr;
}

static void stack_reset(tilly_allocator_t *alloc) {
    stack_state_t *state = (stack_state_t *)alloc->state;
    if (state) {
        state->offset = 0;
        alloc->used = 0;
    }
}

static size_t stack_usage(const tilly_allocator_t *alloc) {
    stack_state_t *state = (stack_state_t *)alloc->state;
    return state ? state->offset : 0;
}

// ==================== Default Allocator ====================

static void *default_alloc(size_t size, void *user_data) {
    (void)user_data;
    return malloc(size);
}

static void default_free(void *ptr, void *user_data) {
    (void)user_data;
    free(ptr);
}

static tilly_allocator_t default_allocator = {
    .strategy = TILLY_ALLOC_GENERAL,
    .state = NULL,
    .capacity = 0,
    .used = 0,
    .peak = 0,
    .alloc_count = 0,
    .free_count = 0,
    .alloc = NULL,  // Use the static functions below
    .free = NULL,
    .reset = NULL,
    .usage = NULL,
};

// Wrapper functions for default allocator
static void *default_alloc_wrapper(tilly_allocator_t *alloc, size_t size, size_t align) {
    (void)alloc; (void)align;
    return default_alloc(size, NULL);
}

static void default_free_wrapper(tilly_allocator_t *alloc, void *ptr) {
    (void)alloc;
    default_free(ptr, NULL);
}

const tilly_allocator_t *tilly_default_allocator(void) {
    static int initialized = 0;
    if (!initialized) {
        default_allocator.alloc = default_alloc_wrapper;
        default_allocator.free = default_free_wrapper;
        initialized = 1;
    }
    return &default_allocator;
}

// ==================== Allocator Factory ====================

tilly_allocator_t *tilly_allocator_create(tilly_alloc_strategy_t strategy, size_t capacity) {
    if (strategy < TILLY_ALLOC_ARENA || strategy > TILLY_ALLOC_STACK ||
        (capacity == 0 && strategy != TILLY_ALLOC_GENERAL)) return NULL;
    tilly_allocator_t *alloc = calloc(1, sizeof(tilly_allocator_t));
    if (!alloc) return NULL;
    
    alloc->strategy = strategy;
    alloc->capacity = capacity;
    
    switch (strategy) {
        case TILLY_ALLOC_ARENA: {
            arena_state_t *state = calloc(1, sizeof(arena_state_t));
            if (!state) { free(alloc); return NULL; }
            state->base = malloc(capacity);
            if (!state->base) { free(state); free(alloc); return NULL; }
            state->size = capacity;
            pthread_mutex_init(&state->lock, NULL);
            alloc->state = state;
            alloc->alloc = arena_alloc;
            alloc->free = arena_free;
            alloc->reset = arena_reset;
            alloc->usage = arena_usage;
            break;
        }
        case TILLY_ALLOC_POOL: {
            // For pool, capacity is total size, block_size defaults to 64
            size_t block_size = 64;
            size_t block_count = capacity / block_size;
            if (block_count == 0) block_count = 1;
            
            pool_state_t *state = calloc(1, sizeof(pool_state_t));
            if (!state) { free(alloc); return NULL; }
            state->block_size = block_size;
            state->block_count = block_count;
            state->blocks = malloc(block_count * block_size);
            if (!state->blocks) { free(state); free(alloc); return NULL; }
            
            // Initialize free list
            state->free_list = NULL;
            for (size_t i = 0; i < block_count; i++) {
                pool_block_t *block = (pool_block_t *)(state->blocks + i * block_size);
                block->next = state->free_list;
                state->free_list = block;
            }
            pthread_mutex_init(&state->lock, NULL);
            alloc->state = state;
            alloc->alloc = pool_alloc;
            alloc->free = pool_free;
            alloc->reset = pool_reset;
            alloc->usage = pool_usage;
            break;
        }
        case TILLY_ALLOC_GENERAL: {
            alloc->state = NULL;
            alloc->alloc = general_alloc;
            alloc->free = general_free;
            alloc->reset = general_reset;
            alloc->usage = general_usage;
            break;
        }
        case TILLY_ALLOC_STACK: {
            stack_state_t *state = calloc(1, sizeof(stack_state_t));
            if (!state) { free(alloc); return NULL; }
            state->base = malloc(capacity);
            if (!state->base) { free(state); free(alloc); return NULL; }
            state->size = capacity;
            state->frame_capacity = 32;
            state->frame_offsets = calloc(state->frame_capacity, sizeof(size_t));
            if (!state->frame_offsets) { free(state->base); free(state); free(alloc); return NULL; }
            alloc->state = state;
            alloc->alloc = stack_alloc;
            alloc->free = stack_free;
            alloc->reset = stack_reset;
            alloc->usage = stack_usage;
            break;
        }
    }
    
    return alloc;
}

void tilly_allocator_destroy(tilly_allocator_t *alloc) {
    if (!alloc) return;
    
    switch (alloc->strategy) {
        case TILLY_ALLOC_ARENA: {
            arena_state_t *state = (arena_state_t *)alloc->state;
            if (state) {
                free(state->base);
                pthread_mutex_destroy(&state->lock);
                free(state);
            }
            break;
        }
        case TILLY_ALLOC_POOL: {
            pool_state_t *state = (pool_state_t *)alloc->state;
            if (state) {
                free(state->blocks);
                pthread_mutex_destroy(&state->lock);
                free(state);
            }
            break;
        }
        case TILLY_ALLOC_STACK: {
            stack_state_t *state = (stack_state_t *)alloc->state;
            if (state) {
                free(state->base);
                free(state->frame_offsets);
                free(state);
            }
            break;
        }
        case TILLY_ALLOC_GENERAL:
            break;
    }
    
    free(alloc);
}

void *tilly_alloc(tilly_allocator_t *alloc, size_t size, size_t align) {
    if (!alloc || !alloc->alloc || size == 0 || !align || (align & (align - 1))) return NULL;
    return alloc->alloc(alloc, size, align);
}

void tilly_free(tilly_allocator_t *alloc, void *ptr) {
    if (!alloc || !alloc->free || !ptr) return;
    alloc->free(alloc, ptr);
}

void *tilly_realloc(tilly_allocator_t *alloc, void *ptr, size_t new_size) {
    if (!alloc) return NULL;
    if (!ptr) return tilly_alloc(alloc, new_size, 8);
    if (new_size == 0) { tilly_free(alloc, ptr); return NULL; }
    
    // For now, only general allocator supports realloc properly
    if (alloc->strategy == TILLY_ALLOC_GENERAL) {
        void *new_ptr = realloc(ptr, new_size);
        if (new_ptr) {
            // Can't track exact size change
        }
        return new_ptr;
    }
    
    // Old allocation size is unavailable for arena, pool and stack.
    return NULL;
}

void tilly_allocator_reset(tilly_allocator_t *alloc) {
    if (alloc && alloc->reset) {
        alloc->reset(alloc);
    }
}

size_t tilly_allocator_usage(const tilly_allocator_t *alloc) {
    if (alloc && alloc->usage) {
        return alloc->usage(alloc);
    }
    return alloc ? alloc->used : 0;
}

void tilly_thread_set_allocator(tilly_allocator_t *alloc) {
    tls_stack_allocator = alloc;
}

tilly_allocator_t *tilly_thread_get_allocator(void) {
    return tls_stack_allocator;
}