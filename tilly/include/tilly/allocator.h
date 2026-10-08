#ifndef TILLY_ALLOCATOR_H
#define TILLY_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

/* Version 2 replaces malloc-compatible default storage with owned MemTKX
 * storage and gives general capacity a live-payload budget. Struct ABI/layout
 * and existing function signatures are retained. */
#define TILLY_ALLOCATOR_API_MAJOR 2
#define TILLY_ALLOCATOR_API_MINOR 0
#define TILLY_ALLOCATOR_API_PATCH 0

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

/* Process-lifetime MemTKX allocator. Storage is not libc-compatible; release
 * through tilly_free. Operations and usage snapshots are synchronized. */
const tilly_allocator_t *tilly_default_allocator(void);

/* Create a MemTKX allocator. General capacity bounds live payload bytes (zero
 * means growable). Pool capacity is rounded to 64-byte slots, at least one.
 * Scratch capacity includes alignment/guard overhead. */
tilly_allocator_t *tilly_allocator_create(
    tilly_alloc_strategy_t strategy,
    size_t capacity
);

/* Reclaim all owned storage, clearing its calling-thread binding. Finish all
 * operations and release resource-owning objects before destruction. */
void tilly_allocator_destroy(tilly_allocator_t *alloc);

// Allocate memory
void *tilly_alloc(tilly_allocator_t *alloc, size_t size, size_t align);

/* Free an exact live pointer. Foreign/interior/already-freed pointers are
 * ignored by built-in heaps/pools. Arena/stack frees are deferred to reset. */
void tilly_free(tilly_allocator_t *alloc, void *ptr);

/* Reallocate memory.
 *
 * Only built-in TILLY_ALLOC_GENERAL heaps support resizing, preserving the
 * original alignment and bytes. Other strategies return NULL. Copy when
 * the allocator is not general. Custom general allocators cannot resize
 * non-null blocks either; they may return storage that
 * is incompatible with libc. Invalid/foreign pointers are rejected by built-in
 * heaps. `ptr` and live usage are left untouched on failure. */
void *tilly_realloc(tilly_allocator_t *alloc, void *ptr, size_t new_size);

// Reset allocator (for arena/stack)
void tilly_allocator_reset(tilly_allocator_t *alloc);

/* Get live payload usage (pool allocations count the entire slot). The peak
 * counter retains the high-water mark across arena, pool and stack resets. */
size_t tilly_allocator_usage(const tilly_allocator_t *alloc);

/* Thread-local binding. Built-in allocator destruction invalidates bindings
 * on other threads too; custom allocators require caller-managed lifetime. */
void tilly_thread_set_allocator(tilly_allocator_t *alloc);
tilly_allocator_t *tilly_thread_get_allocator(void);

#ifdef __cplusplus
}
#endif

#endif // TILLY_ALLOCATOR_H
