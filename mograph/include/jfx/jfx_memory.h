#ifndef JFX_MEMORY_H
#define JFX_MEMORY_H

#include <stddef.h>
#include <stdbool.h>
#include "tilly/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize memory subsystem with heap allocator
bool memory_init(tilly_allocator_t *heap_allocator);

// Shutdown memory subsystem
void memory_shutdown(void);

// Frame arena - reset each frame
void *jfx_frame_alloc(size_t size, size_t align);
void jfx_frame_reset(void);

// Resource pool - for persistent allocations
void *jfx_resource_alloc(size_t size, size_t align);
void jfx_resource_free(void *ptr);

// Heap allocator - for long-lived allocations
void *jfx_heap_alloc(size_t size, size_t align);
void jfx_heap_free(void *ptr);
void *jfx_heap_realloc(void *ptr, size_t new_size);

// Get allocator usage stats
size_t jfx_frame_arena_usage(void);
size_t jfx_resource_pool_usage(void);
size_t jfx_heap_usage(void);

#ifdef __cplusplus
}
#endif

#endif // JFX_MEMORY_H