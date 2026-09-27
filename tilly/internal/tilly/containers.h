#ifndef TILLY_CONTAINERS_H
#define TILLY_CONTAINERS_H
/* Internal klib adapter. No global allocator switching or libc allocation macros.
 * khash owns its buckets; callers own string keys. Vectors use checked reserve
 * because upstream kv_push loses the allocation on realloc failure. */
#include "tilly/allocator.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <kvec.h>
static inline void *tilly_container_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes, _Alignof(max_align_t));
}
static inline void tilly_container_free(void *ptr) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), ptr);
}
static inline void *tilly_container_calloc(size_t count, size_t bytes) {
    if (!bytes || count > SIZE_MAX / bytes) return NULL;
    void *p = tilly_container_alloc(count * bytes);
    if (p) memset(p, 0, count * bytes);
    return p;
}
static inline void *tilly_container_realloc(void *ptr, size_t bytes) {
    return tilly_realloc((tilly_allocator_t *)tilly_default_allocator(), ptr, bytes);
}
#define kmalloc tilly_container_alloc
#define kcalloc tilly_container_calloc
#define krealloc tilly_container_realloc
#define kfree tilly_container_free
#include <khash.h>
/* Return a new pointer without changing the vector on failure. */
static inline void *tilly_vec_grow(void *old, size_t used, size_t capacity,
                                   size_t count, size_t width, size_t *out_capacity) {
    if (!width || count > SIZE_MAX / width) return NULL;
    size_t next = capacity ? capacity : 8;
    while (next < count) {
        if (next > SIZE_MAX / 2) { next = count; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / width) next = count;
    void *p = tilly_container_alloc(next * width);
    if (!p) return NULL;
    if (used) memcpy(p, old, used * width);
    tilly_container_free(old);
    *out_capacity = next;
    return p;
}
/* Typed macro avoids aliasing a T** as void**. */
#define tilly_vec_reserve(v, count) \
    ((count) <= (v)->m ? true : \
     tilly_vec_reserve_impl((v), (count)))
/* C11 generic vectors share a layout, but accessing through a different struct
 * type would violate aliasing. Copy the representation through memcpy instead. */
static inline bool tilly_vec_reserve_raw(void *vector, size_t count, size_t width) {
    struct { size_t n, m; void *a; } v;
    memcpy(&v, vector, sizeof(v));
    void *p = tilly_vec_grow(v.a, v.n, v.m, count, width, &v.m);
    if (!p) return false;
    v.a = p;
    memcpy(vector, &v, sizeof(v));
    return true;
}
#define tilly_vec_reserve_impl(v, count) tilly_vec_reserve_raw((v), (count), sizeof(*(v)->a))
#define tilly_vec_destroy(v) do { tilly_container_free((v).a); kv_init(v); } while (0)
#endif
