#include "tilly/allocator.h"
#include "tilly/logger.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

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
    alloc->peak = state->peak;
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
    if (!state) return 0;
    pthread_mutex_lock(&state->lock);
    size_t used = state->offset;
    pthread_mutex_unlock(&state->lock);
    return used;
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
    bool *allocated;
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
    state->allocated[((uintptr_t)block - (uintptr_t)state->blocks) / state->block_size] = true;
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
    size_t index = (position - base) / state->block_size;
    if (!state->allocated[index]) { pthread_mutex_unlock(&state->lock); return; }
    state->allocated[index] = false;
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
    memset(state->allocated, 0, state->block_count * sizeof(*state->allocated));
    state->free_list = NULL;
    for (size_t i = 0; i < state->block_count; i++) {
        // The allocation and fixed 64-byte stride preserve block alignment.
        pool_block_t *block = (pool_block_t *)(void *)(state->blocks + i * state->block_size);
        block->next = state->free_list;
        state->free_list = block;
    }
    alloc->used = 0;
    pthread_mutex_unlock(&state->lock);
}

static size_t pool_usage(const tilly_allocator_t *alloc) {
    pool_state_t *state = (pool_state_t *)alloc->state;
    if (!state) return 0;
    pthread_mutex_lock(&state->lock);
    size_t used = alloc->used;
    pthread_mutex_unlock(&state->lock);
    return used;
}

// ==================== General Allocator (malloc/free) ====================

typedef struct {
    pthread_mutex_t lock;
} general_state_t;

/* Keep returned storage aligned for every fundamental type while recording
 * the payload size for free/realloc accounting. The default allocator below
 * remains untracked and uses ordinary malloc-compatible pointers. */
typedef union {
    max_align_t alignment;
    size_t size;
} general_header_t;

static void *general_alloc(tilly_allocator_t *alloc, size_t size, size_t align) {
    if (align > _Alignof(max_align_t) || size > SIZE_MAX - sizeof(general_header_t)) return NULL;
    general_state_t *state = alloc->state;
    general_header_t *ptr = malloc(sizeof(*ptr) + size);
    if (ptr) {
        ptr->size = size;
        pthread_mutex_lock(&state->lock);
        alloc->used += size;
        alloc->alloc_count++;
        if (alloc->used > alloc->peak) alloc->peak = alloc->used;
        pthread_mutex_unlock(&state->lock);
    }
    return ptr ? ptr + 1 : NULL;
}

static void general_free(tilly_allocator_t *alloc, void *ptr) {
    if (!ptr) return;
    general_state_t *state = alloc->state;
    general_header_t *header = (general_header_t *)ptr - 1;
    pthread_mutex_lock(&state->lock);
    alloc->used -= header->size;
    alloc->free_count++;
    pthread_mutex_unlock(&state->lock);
    free(header);
}

static void general_reset(tilly_allocator_t *alloc) {
    // General allocator can't be reset
    (void)alloc;
}

static size_t general_usage(const tilly_allocator_t *alloc) {
    general_state_t *state = alloc->state;
    pthread_mutex_lock(&state->lock);
    size_t used = alloc->used;
    pthread_mutex_unlock(&state->lock);
    return used;
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
    alloc->peak = state->peak;
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

// Immutable dispatch table: safe to obtain concurrently during worker startup.
static void *default_alloc_wrapper(tilly_allocator_t *alloc, size_t size, size_t align) {
    (void)alloc;
    return align <= _Alignof(max_align_t) ? default_alloc(size, NULL) : NULL;
}
static void default_free_wrapper(tilly_allocator_t *alloc, void *ptr) {
    (void)alloc;
    default_free(ptr, NULL);
}
static tilly_allocator_t default_allocator = {
    .strategy = TILLY_ALLOC_GENERAL,
    .alloc = default_alloc_wrapper,
    .free = default_free_wrapper,
};
const tilly_allocator_t *tilly_default_allocator(void) { return &default_allocator; }

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
            if (pthread_mutex_init(&state->lock, NULL) != 0) {
                free(state->base); free(state); free(alloc); return NULL;
            }
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
            state->allocated = calloc(block_count, sizeof(*state->allocated));
            if (!state->allocated) { free(state->blocks); free(state); free(alloc); return NULL; }
            
            // Initialize free list
            state->free_list = NULL;
            for (size_t i = 0; i < block_count; i++) {
                pool_block_t *block = (pool_block_t *)(void *)(state->blocks + i * block_size);
                block->next = state->free_list;
                state->free_list = block;
            }
            if (pthread_mutex_init(&state->lock, NULL) != 0) {
                free(state->allocated); free(state->blocks); free(state); free(alloc); return NULL;
            }
            alloc->state = state;
            alloc->alloc = pool_alloc;
            alloc->free = pool_free;
            alloc->reset = pool_reset;
            alloc->usage = pool_usage;
            break;
        }
        case TILLY_ALLOC_GENERAL: {
            general_state_t *state = calloc(1, sizeof(*state));
            if (!state) { free(alloc); return NULL; }
            if (pthread_mutex_init(&state->lock, NULL) != 0) {
                free(state); free(alloc); return NULL;
            }
            alloc->state = state;
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
    if (tls_stack_allocator == alloc) tls_stack_allocator = NULL;
    
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
                free(state->allocated);
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
        case TILLY_ALLOC_GENERAL: {
            general_state_t *state = alloc->state;
            if (state) { pthread_mutex_destroy(&state->lock); free(state); }
            break;
        }
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

    // Only the general allocator can grow a block in place: arena, pool and
    // stack allocations carry no size header, so the old size is unknown and an
    // in-place grow is impossible. Report that rather than returning NULL with
    // no explanation, which callers read as "out of memory".
    if (alloc->strategy != TILLY_ALLOC_GENERAL) {
        tilly_log_simple(TILLY_LOG_ERROR,
            "tilly_realloc: strategy %d cannot grow a block in place; "
            "allocate a new block and copy instead", (int)alloc->strategy);
        return NULL;
    }
    if (alloc->alloc == general_alloc) {
        if (new_size > SIZE_MAX - sizeof(general_header_t)) return NULL;
        general_state_t *state = alloc->state;
        general_header_t *header = (general_header_t *)ptr - 1;
        size_t old_size = header->size;
        general_header_t *resized = realloc(header, sizeof(*header) + new_size);
        if (!resized) return NULL;
        resized->size = new_size;
        pthread_mutex_lock(&state->lock);
        alloc->used = alloc->used - old_size + new_size;
        if (alloc->used > alloc->peak) alloc->peak = alloc->used;
        pthread_mutex_unlock(&state->lock);
        return resized + 1;
    }
    /* The strategy enum does not prove that a custom callback returns malloc
     * storage. There is no custom realloc callback in this ABI. */
    if (alloc->alloc != default_alloc_wrapper || alloc->free != default_free_wrapper) return NULL;
    void *new_ptr = realloc(ptr, new_size);
    if (!new_ptr) {
        tilly_log_simple(TILLY_LOG_ERROR, "tilly_realloc: failed to grow to %zu bytes", new_size);
        return NULL;
    }
    return new_ptr;
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
