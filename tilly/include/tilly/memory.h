#ifndef TILLY_MEMORY_H
#define TILLY_MEMORY_H

#include "tilly/allocator.h"
#include <string.h>

/* Process-lifetime allocation helpers. Always release through tilly_mem_free,
 * never libc free. These also work before/after individual engine contexts. */
static inline void *tilly_mem_alloc(size_t bytes) {
#ifdef __cplusplus
    const size_t alignment = alignof(max_align_t);
#else
    const size_t alignment = _Alignof(max_align_t);
#endif
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes, alignment);
}
static inline void tilly_mem_free(void *pointer) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), pointer);
}
static inline void *tilly_mem_calloc(size_t count, size_t width) {
    if (!width || count > SIZE_MAX / width) return NULL;
    void *pointer = tilly_mem_alloc(count * width);
    if (pointer) memset(pointer, 0, count * width);
    return pointer;
}
static inline void *tilly_mem_realloc(void *pointer, size_t bytes) {
    return tilly_realloc((tilly_allocator_t *)tilly_default_allocator(), pointer, bytes);
}
static inline char *tilly_mem_strndup(const char *text, size_t limit) {
    if (!text) return NULL;
    size_t n = 0;
    while (n < limit && text[n]) ++n;
    if (n == SIZE_MAX) return NULL;
    char *copy = (char *)tilly_mem_alloc(n + 1);
    if (copy) { memcpy(copy, text, n); copy[n] = 0; }
    return copy;
}
static inline char *tilly_mem_strdup(const char *text) {
    return tilly_mem_strndup(text, SIZE_MAX);
}
#endif
