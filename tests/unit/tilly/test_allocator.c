#include "tilly/allocator.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
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

static void *allocation_worker(void *user) {
    tilly_allocator_t *heap = user;
    for (size_t i = 0; i < 2000; ++i) {
        size_t n = i % 257 + 1;
        unsigned char *p = tilly_alloc(heap, n, 64);
        assert(p && (uintptr_t)p % 64 == 0);
        memset(p, (int)(i % 256), n);
        unsigned char *q = tilly_realloc(heap, p, n + 100);
        assert(q && (uintptr_t)q % 64 == 0);
        for (size_t j = 0; j < n; ++j) assert(q[j] == i % 256);
        tilly_free(heap, q);
    }
    return NULL;
}

static pthread_mutex_t binding_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t binding_changed = PTHREAD_COND_INITIALIZER;
static int binding_stage;
static void *binding_worker(void *user) {
    tilly_thread_set_allocator(user);
    pthread_mutex_lock(&binding_lock);
    binding_stage = 1;
    pthread_cond_signal(&binding_changed);
    while (binding_stage != 2) pthread_cond_wait(&binding_changed, &binding_lock);
    assert(!tilly_thread_get_allocator());
    pthread_mutex_unlock(&binding_lock);
    return NULL;
}

static void checked_ownership(void) {
    tilly_allocator_t *heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, 4096);
    tilly_allocator_t *foreign = tilly_allocator_create(TILLY_ALLOC_GENERAL, 0);
    assert(heap && foreign);
    unsigned char *p = tilly_alloc(heap, 100, 256);
    assert(p && (uintptr_t)p % 256 == 0);
    memset(p, 17, 100);
    tilly_free(foreign, p);
    tilly_free(heap, p + 1);
    int local = 0;
    tilly_free(heap, &local);
    assert(!tilly_realloc(foreign, p, 200));
    assert(!tilly_realloc(heap, p + 1, 200));
    assert(tilly_allocator_usage(heap) == 100 && !heap->free_count);
    unsigned char *q = tilly_realloc(heap, p, 200);
    assert(q && (uintptr_t)q % 256 == 0);
    for (size_t i = 0; i < 100; ++i) assert(q[i] == 17);
    assert(!tilly_realloc(heap, q, SIZE_MAX) && q[0] == 17);
    tilly_free(heap, q);
    tilly_free(heap, q);
    assert(!tilly_allocator_usage(heap) && heap->free_count == 1);
    assert(!tilly_alloc(heap, 1, 0) && !tilly_alloc(heap, 1, 3));
    assert(!tilly_alloc(heap, SIZE_MAX, 1));
    /* Destroy must reclaim outstanding allocations, not just its metadata. */
    assert(tilly_alloc(heap, 1024, 32));
    tilly_allocator_destroy(heap);
    tilly_allocator_destroy(foreign);

    heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, 0);
    assert(heap);
    pthread_t workers[4];
    for (size_t i = 0; i < 4; ++i) assert(!pthread_create(&workers[i], NULL, allocation_worker, heap));
    for (size_t i = 0; i < 4; ++i) assert(!pthread_join(workers[i], NULL));
    assert(!tilly_allocator_usage(heap));
    tilly_allocator_destroy(heap);
    tilly_allocator_t *scratch = tilly_allocator_create(TILLY_ALLOC_STACK, 128);
    assert(scratch);
    pthread_t binder;
    assert(!pthread_create(&binder, NULL, binding_worker, scratch));
    pthread_mutex_lock(&binding_lock);
    while (binding_stage != 1) pthread_cond_wait(&binding_changed, &binding_lock);
    tilly_allocator_destroy(scratch);
    binding_stage = 2;
    pthread_cond_signal(&binding_changed);
    pthread_mutex_unlock(&binding_lock);
    assert(!pthread_join(binder, NULL));
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

    /* The process allocator uses the same ownership/realloc contracts. */
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
    checked_ownership();
    return 0;
}
