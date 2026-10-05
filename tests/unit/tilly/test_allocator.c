#include "tilly/allocator.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static _Alignas(max_align_t) unsigned char custom_storage[64];
static int custom_freed;
static void *custom_alloc(tilly_allocator_t *allocator, size_t size, size_t align) {
    (void)allocator;
    return size <= sizeof(custom_storage) && align <= _Alignof(max_align_t) ? custom_storage : NULL;
}
static void custom_free(tilly_allocator_t *allocator, void *pointer) {
    (void)allocator;
    assert(pointer == custom_storage);
    ++custom_freed;
}

int main(void) {
    tilly_allocator_t *heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, 0);
    assert(heap);
    unsigned char *p = tilly_alloc(heap, 32, _Alignof(max_align_t));
    assert(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
    memset(p, 42, 32);
    assert(tilly_allocator_usage(heap) == 32);
    p = tilly_realloc(heap, p, 128);
    assert(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
    for (size_t i = 0; i < 32; ++i) assert(p[i] == 42);
    assert(tilly_allocator_usage(heap) == 128 && heap->peak == 128);
    p = tilly_realloc(heap, p, 16);
    assert(p && tilly_allocator_usage(heap) == 16 && heap->peak == 128);
    void *other = tilly_alloc(heap, 24, 8);
    assert(other && tilly_allocator_usage(heap) == 40);
    tilly_free(heap, other);
    assert(tilly_allocator_usage(heap) == 16);
    assert(!tilly_realloc(heap, p, SIZE_MAX));
    assert(tilly_allocator_usage(heap) == 16 && p[0] == 42);
    tilly_free(heap, p);
    assert(tilly_allocator_usage(heap) == 0);
    p = tilly_realloc(heap, NULL, 64);
    assert(p && tilly_allocator_usage(heap) == 64);
    assert(!tilly_realloc(heap, p, 0) && tilly_allocator_usage(heap) == 0);
    assert(heap->alloc_count == 3 && heap->free_count == 3);
    assert(!tilly_alloc(heap, SIZE_MAX, 8));
    tilly_allocator_destroy(heap);

    /* The untracked default allocator must retain its malloc-compatible path. */
    heap = (tilly_allocator_t *)tilly_default_allocator();
    p = tilly_alloc(heap, 8, 8);
    assert(p);
    p[0] = 7;
    p = tilly_realloc(heap, p, 64);
    assert(p && p[0] == 7);
    tilly_free(heap, p);

    tilly_allocator_t *pool = tilly_allocator_create(TILLY_ALLOC_POOL, 128);
    assert(pool);
    void *a = tilly_alloc(pool, 32, 8);
    void *b = tilly_alloc(pool, 32, 8);
    assert(a && b && a != b && !tilly_alloc(pool, 32, 8));
    tilly_free(pool, a);
    tilly_free(pool, a);
    assert(tilly_allocator_usage(pool) == 64 && pool->free_count == 1);
    void *c = tilly_alloc(pool, 32, 8);
    assert(c == a && !tilly_alloc(pool, 32, 8));
    tilly_free(pool, c);
    tilly_free(pool, b);
    assert(tilly_allocator_usage(pool) == 0);
    tilly_allocator_reset(pool);
    a = tilly_alloc(pool, 32, 8);
    b = tilly_alloc(pool, 32, 8);
    assert(a && b && a != b);
    tilly_free(pool, a);
    tilly_free(pool, b);
    assert(tilly_allocator_usage(pool) == 0);
    tilly_allocator_destroy(pool);

    const tilly_alloc_strategy_t bounded[] = {TILLY_ALLOC_ARENA, TILLY_ALLOC_STACK};
    for (size_t i = 0; i < sizeof(bounded) / sizeof(*bounded); ++i) {
        tilly_allocator_t *scratch = tilly_allocator_create(bounded[i], 128);
        assert(scratch && tilly_alloc(scratch, 64, 1));
        assert(tilly_allocator_usage(scratch) == 64 && scratch->peak == 64);
        assert(!tilly_alloc(scratch, 128, 1));
        tilly_allocator_reset(scratch);
        assert(tilly_allocator_usage(scratch) == 0 && scratch->peak == 64);
        assert(tilly_alloc(scratch, 16, 1) && scratch->peak == 64);
        tilly_allocator_destroy(scratch);
    }
    tilly_allocator_t *scratch = tilly_allocator_create(TILLY_ALLOC_STACK, 128);
    assert(scratch);
    tilly_thread_set_allocator(scratch);
    assert(tilly_thread_get_allocator() == scratch);
    tilly_allocator_destroy(scratch);
    assert(!tilly_thread_get_allocator());

    /* A custom general strategy need not return libc-compatible pointers. */
    tilly_allocator_t custom = {.strategy = TILLY_ALLOC_GENERAL,
        .alloc = custom_alloc, .free = custom_free};
    p = tilly_alloc(&custom, 16, 8);
    assert(p == custom_storage);
    p[0] = 42;
    assert(!tilly_realloc(&custom, p, 32) && p[0] == 42 && !custom_freed);
    assert(!tilly_realloc(&custom, p, 0) && custom_freed == 1);
    return 0;
}
