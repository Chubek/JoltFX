#include "jfx/jfx_engine.h"
#include "jfx/jfx_memory.h"
#include "jfx/jfx_scheduler.h"
#include "jfx/jfx_events.h"
#include "tilly/tilly.h"
#include "tilly/allocator.h"
#include <stdlib.h>
#include <string.h>

struct jfx_engine {
    tilly_context_t *ctx;
    jfx_engine_config_t config;
};

jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine) {
    if (!config || !out_engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    *out_engine = NULL;
    jfx_engine_t *engine = malloc(sizeof(jfx_engine_t));
    if (!engine) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    tilly_config_t tilly_config = {
        .heap_size = 64 * 1024 * 1024,
        .enable_logging = true,
        .log_level = TILLY_LOG_INFO,
        .enable_profiling = false,
    };

    engine->ctx = tilly_init(&tilly_config);
    if (!engine->ctx) {
        free(engine);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    // Initialize memory subsystem with heap allocator
    tilly_allocator_t *heap = tilly_get_heap_allocator(engine->ctx);
    if (!memory_init(heap)) {
        tilly_shutdown(engine->ctx); free(engine); return JFX_ERROR_OUT_OF_MEMORY;
    }

    // Initialize scheduler with configured thread count
    uint32_t worker_count = config->max_buffers > 0 ? config->max_buffers : 4;
    if (!scheduler_init(worker_count)) {
        memory_shutdown(); tilly_shutdown(engine->ctx); free(engine); return JFX_ERROR_NOT_INITIALIZED;
    }

    // Initialize event bus
    if (!event_bus_init()) {
        scheduler_shutdown(); memory_shutdown(); tilly_shutdown(engine->ctx); free(engine);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    memcpy(&engine->config, config, sizeof(jfx_engine_config_t));

    tilly_log_info("jfx_engine", "JoltFX engine initialized");

    *out_engine = engine;
    return JFX_SUCCESS;
}

void jfx_engine_shutdown(jfx_engine_t *engine) {
    if (engine) {
        tilly_log_info("jfx_engine", "JoltFX engine shutting down");
        
        // Shutdown subsystems in reverse order
        event_bus_shutdown();
        scheduler_shutdown();
        memory_shutdown();
        
        tilly_shutdown(engine->ctx);
        free(engine);
    }
}

jfx_result_t jfx_engine_tick(jfx_engine_t *engine) {
    if (!engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    
    // Begin frame
    event_publish(JFX_EVENT_FRAME_BEGIN, NULL);
    
    // Reset frame arena
    jfx_frame_reset();
    
    // Phase 1 frame boundary: complete queued CPU work before ending the frame.
    scheduler_wait_idle();
    
    // End frame
    event_publish(JFX_EVENT_FRAME_END, NULL);
    
    return JFX_SUCCESS;
}
