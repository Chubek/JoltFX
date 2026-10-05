#ifndef TILLY_ALLOCATOR_H
#define TILLY_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TILLY_ALLOC_ARENA = 0,
    TILLY_ALLOC_POOL,
    TILLY_ALLOC_GENERAL,
    TILLY_ALLOC_STACK,
} tilly_alloc_strategy_t;

typedef struct tilly_allocator {
    tilly_alloc_strategy_t strategy;
    void *state;
    size_t capacity;
    size_t used;
    size_t peak;
    uint32_t alloc_count;
    uint32_t free_count;
    void *(*alloc)(struct tilly_allocator *alloc, size_t size, size_t align);
    void (*free)(struct tilly_allocator *alloc, void *ptr);
    void (*reset)(struct tilly_allocator *alloc);
    size_t (*usage)(const struct tilly_allocator *alloc);
} tilly_allocator_t;

// Get the default system allocator (malloc/free)
const tilly_allocator_t *tilly_default_allocator(void);

// Create a new allocator
tilly_allocator_t *tilly_allocator_create(
    tilly_alloc_strategy_t strategy,
    size_t capacity
);

// Destroy an allocator, clearing its binding on the calling thread if present.
void tilly_allocator_destroy(tilly_allocator_t *alloc);

// Allocate memory
void *tilly_alloc(tilly_allocator_t *alloc, size_t size, size_t align);

// Free memory
void tilly_free(tilly_allocator_t *alloc, void *ptr);

/* Reallocate memory.
 *
 * Only TILLY_ALLOC_GENERAL can grow a block in place; arena, pool and stack
 * blocks carry no size header, so growing one returns NULL and logs an error
 * (it is not an out-of-memory condition). Allocate a new block and copy when
 * the allocator is not general. Custom general allocators cannot resize
 * non-null blocks either; they may return storage that
 * is incompatible with libc. `ptr` is left untouched on failure. */
void *tilly_realloc(tilly_allocator_t *alloc, void *ptr, size_t new_size);

// Reset allocator (for arena/stack)
void tilly_allocator_reset(tilly_allocator_t *alloc);

/* Get live payload usage (pool allocations count the entire slot). The peak
 * counter retains the high-water mark across arena, pool and stack resets. */
size_t tilly_allocator_usage(const tilly_allocator_t *alloc);

// Thread-local stack allocator
void tilly_thread_set_allocator(tilly_allocator_t *alloc);
tilly_allocator_t *tilly_thread_get_allocator(void);

#ifdef __cplusplus
}
#endif

#endif // TILLY_ALLOCATOR_H
