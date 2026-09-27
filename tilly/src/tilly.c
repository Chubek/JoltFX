#include "tilly/tilly.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "tilly/module.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

// Forward declaration
void tilly_set_global_context(struct tilly_context *ctx);

struct tilly_context {
    tillyz_context_t *bootstrap_ctx;
    tilly_allocator_t *heap;
    tilly_module_t *modules[32];
    uint32_t module_count;
    pthread_mutex_t module_lock;
};

tilly_context_t *tilly_init(const tilly_config_t *config) {
    tilly_config_t default_config = {
        .bootstrap_ctx = NULL,
        .heap_size = 64 * 1024 * 1024,  // 64 MB
        .enable_logging = true,
        .log_level = TILLY_LOG_INFO,
        .enable_profiling = false,
    };
    
    if (config) {
        default_config = *config;
    }
    
    // Initialize logging first
    tilly_log_init();
    if (default_config.enable_logging) {
        tilly_log_set_level(default_config.log_level);
        tilly_log_info("tilly", "Initializing Tilly runtime (heap: %zu MB)", 
                       default_config.heap_size / (1024 * 1024));
    }
    
    // Create context
    tilly_context_t *ctx = calloc(1, sizeof(tilly_context_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->bootstrap_ctx = default_config.bootstrap_ctx;
    pthread_mutex_init(&ctx->module_lock, NULL);
    
    // Create heap allocator
    ctx->heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, default_config.heap_size);
    if (!ctx->heap) {
        pthread_mutex_destroy(&ctx->module_lock);
        free(ctx);
        return NULL;
    }
    
    // Set global context for module access
    tilly_set_global_context(ctx);
    
    tilly_log_info("tilly", "Tilly runtime initialized successfully");
    
    return ctx;
}

void tilly_shutdown(tilly_context_t *ctx) {
    if (!ctx) return;
    
    tilly_log_info("tilly", "Shutting down Tilly runtime");
    
    // Unload all modules in reverse order
    while (ctx->module_count) {
        tilly_module_t *mod = ctx->modules[ctx->module_count - 1];
        mod->ref_count = 1;
        tilly_module_unload(ctx, mod);
    }
    
    // Destroy heap allocator
    if (ctx->heap) {
        tilly_allocator_destroy(ctx->heap);
        ctx->heap = NULL;
    }
    
    pthread_mutex_destroy(&ctx->module_lock);
    
    // Clear global context
    tilly_set_global_context(NULL);
    
    tilly_log_info("tilly", "Tilly runtime shutdown complete");
    tilly_log_shutdown();
    
    free(ctx);
}

tilly_allocator_t *tilly_get_heap_allocator(tilly_context_t *ctx) {
    return ctx ? ctx->heap : NULL;
}