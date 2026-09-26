#include "tilly/allocator.h"
#include "tilly/logger.h"
#include <stdlib.h>

static tilly_allocator_t *g_frame_arena = NULL;
static tilly_allocator_t *g_resource_pool = NULL;
static tilly_allocator_t *g_heap = NULL;

void memory_init(tilly_allocator_t *heap) {
    g_heap = heap;
    
    // Create frame arena (1MB per frame)
    g_frame_arena = tilly_allocator_create(TILLY_ALLOC_ARENA, 1024 * 1024);
    
    // Create resource pool (4KB blocks, 1024 blocks = 4MB)
    g_resource_pool = tilly_allocator_create(TILLY_ALLOC_POOL, 4 * 1024 * 1024);
    
    tilly_log_debug("memory", "Memory subsystem initialized: frame_arena=%p, resource_pool=%p", 
                    (void*)g_frame_arena, (void*)g_resource_pool);
}

void memory_shutdown(void) {
    if (g_frame_arena) {
        tilly_allocator_destroy(g_frame_arena);
        g_frame_arena = NULL;
    }
    if (g_resource_pool) {
        tilly_allocator_destroy(g_resource_pool);
        g_resource_pool = NULL;
    }
    tilly_log_debug("memory", "Memory subsystem shutdown");
}

// Frame arena - reset each frame
void *jfx_frame_alloc(size_t size, size_t align) {
    if (!g_frame_arena) return NULL;
    return tilly_alloc(g_frame_arena, size, align);
}

void jfx_frame_reset(void) {
    if (g_frame_arena) {
        tilly_allocator_reset(g_frame_arena);
    }
}

// Resource pool - for persistent allocations
void *jfx_resource_alloc(size_t size, size_t align) {
    if (!g_resource_pool) return NULL;
    return tilly_alloc(g_resource_pool, size, align);
}

void jfx_resource_free(void *ptr) {
    if (!g_resource_pool || !ptr) return;
    tilly_free(g_resource_pool, ptr);
}

// Heap allocator - for long-lived allocations
void *jfx_heap_alloc(size_t size, size_t align) {
    if (!g_heap) return NULL;
    return tilly_alloc(g_heap, size, align);
}

void jfx_heap_free(void *ptr) {
    if (!g_heap || !ptr) return;
    tilly_free(g_heap, ptr);
}

void *jfx_heap_realloc(void *ptr, size_t new_size) {
    if (!g_heap) return NULL;
    return tilly_realloc(g_heap, ptr, new_size);
}

// Get allocator usage stats
size_t jfx_frame_arena_usage(void) {
    return g_frame_arena ? tilly_allocator_usage(g_frame_arena) : 0;
}

size_t jfx_resource_pool_usage(void) {
    return g_resource_pool ? tilly_allocator_usage(g_resource_pool) : 0;
}

size_t jfx_heap_usage(void) {
    return g_heap ? tilly_allocator_usage(g_heap) : 0;
}