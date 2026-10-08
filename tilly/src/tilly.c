#include "tilly/tilly.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "tilly/module.h"
#include "tilly/memory.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "runtime_internal.h"

static pthread_mutex_t runtime_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t runtime_count;

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
    
    // Create context
    tilly_context_t *ctx = tilly_mem_calloc(1, sizeof(tilly_context_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->bootstrap_ctx = default_config.bootstrap_ctx;
    if (pthread_mutex_init(&ctx->module_lock, NULL) != 0) { tilly_mem_free(ctx); return NULL; }
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes) != 0) {
        pthread_mutex_destroy(&ctx->module_lock); tilly_mem_free(ctx); return NULL;
    }
    int status = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    if (!status) status = pthread_mutex_init(&ctx->lifecycle_lock, &attributes);
    pthread_mutexattr_destroy(&attributes);
    if (status) { pthread_mutex_destroy(&ctx->module_lock); tilly_mem_free(ctx); return NULL; }
    
    // Create heap allocator
    ctx->heap = tilly_allocator_create(TILLY_ALLOC_GENERAL, default_config.heap_size);
    if (!ctx->heap) {
        pthread_mutex_destroy(&ctx->lifecycle_lock);
        pthread_mutex_destroy(&ctx->module_lock);
        tilly_mem_free(ctx);
        return NULL;
    }
    
    /* Logging is shared by all live contexts. Only the final shutdown may
     * clear the sink registry; do not invoke sinks while holding runtime_lock. */
    pthread_mutex_lock(&runtime_lock);
    if (!runtime_count) tilly_log_init();
    ++runtime_count;
    if (default_config.enable_logging) tilly_log_set_level(default_config.log_level);
    pthread_mutex_unlock(&runtime_lock);
    tilly_log_info("tilly", "Tilly runtime initialized successfully");
    
    return ctx;
}

void tilly_shutdown(tilly_context_t *ctx) {
    if (!ctx) return;
    
    tilly_log_info("tilly", "Shutting down Tilly runtime");
    pthread_mutex_lock(&ctx->lifecycle_lock);
    ctx->shutting_down = true;
    
    // Unload all modules in reverse order
    while (ctx->module_count) {
        tilly_module_t *mod = &ctx->modules[ctx->module_count - 1]->module;
        mod->ref_count = 1;
        tilly_module_unload(ctx, mod);
    }
    
    // Destroy heap allocator
    if (ctx->heap) {
        tilly_allocator_destroy(ctx->heap);
        ctx->heap = NULL;
    }
    
    pthread_mutex_destroy(&ctx->module_lock);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    pthread_mutex_destroy(&ctx->lifecycle_lock);
    
    tilly_log_info("tilly", "Tilly runtime shutdown complete");
    pthread_mutex_lock(&runtime_lock);
    if (--runtime_count == 0) tilly_log_shutdown();
    pthread_mutex_unlock(&runtime_lock);
    
    tilly_mem_free(ctx);
}

tilly_allocator_t *tilly_get_heap_allocator(tilly_context_t *ctx) {
    return ctx ? ctx->heap : NULL;
}
