#include "jfx/jfx_engine.h"
#include "jfx/jfx_memory.h"
#include "jfx/jfx_scheduler.h"
#include "jfx/jfx_events.h"
#include "jfx/d3d12_backend.h"
#include "jfx/mtl_backend.h"
#include "jfx/vk_backend.h"
#include "jfx/wgpu_backend.h"
#include "tilly/tilly.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct jfx_engine {
    tilly_context_t *ctx;
    jfx_engine_config_t config;
    char backend_name[16];
    enum { JFX_BACKEND_VULKAN, JFX_BACKEND_METAL, JFX_BACKEND_D3D12, JFX_BACKEND_WEBGPU } kind;
    void *backend;
    jfx_engine_metrics_t metrics;
};

static uint64_t timestamp_ns(void) {
    struct timespec timestamp;
    if (timespec_get(&timestamp, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) +
        (uint64_t)timestamp.tv_nsec;
}

static bool resolve_backend(const char *name, char *out, size_t size) {
    const char *selected = "vulkan";
    if (name && strcmp(name, "auto") != 0 && strcmp(name, "vulkan") != 0 &&
        strcmp(name, "metal") != 0 && strcmp(name, "d3d12") != 0 &&
        strcmp(name, "webgpu") != 0) {
        return false;
    }
    if (name && strcmp(name, "auto") != 0) {
        selected = name;
    }
    snprintf(out, size, "%s", selected);
    return true;
}

jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine) {
    if (!config || !out_engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    *out_engine = NULL;
    char backend_name[16];
    if (!resolve_backend(config->backend_name, backend_name, sizeof(backend_name))) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_engine_t *engine = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*engine), _Alignof(jfx_engine_t));
    if (!engine) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(engine, 0, sizeof(*engine));

    tilly_config_t tilly_config = {
        .heap_size = 64 * 1024 * 1024,
        .enable_logging = true,
        .log_level = TILLY_LOG_INFO,
        .enable_profiling = false,
    };

    engine->ctx = tilly_init(&tilly_config);
    if (!engine->ctx) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    // Initialize memory subsystem with heap allocator
    tilly_allocator_t *heap = tilly_get_heap_allocator(engine->ctx);
    if (!memory_init(heap)) {
        tilly_shutdown(engine->ctx); tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine); return JFX_ERROR_OUT_OF_MEMORY;
    }

    // Initialize scheduler with configured thread count
    uint32_t worker_count = config->max_buffers > 0 ? config->max_buffers : 4;
    if (!scheduler_init(worker_count)) {
        memory_shutdown(); tilly_shutdown(engine->ctx); tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine); return JFX_ERROR_NOT_INITIALIZED;
    }

    // Initialize event bus
    if (!event_bus_init()) {
        scheduler_shutdown(); memory_shutdown(); tilly_shutdown(engine->ctx); tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    // Select the backend last so shutdown unwinds in reverse order.
    jfx_result_t backend_status = JFX_ERROR_BACKEND_FAILURE;
    if (strcmp(backend_name, "vulkan") == 0) {
        jfx_vk_backend_config_t backend_config = { .probe_gpu = true, .memory_limit = 0 };
        engine->kind = JFX_BACKEND_VULKAN;
        backend_status = jfx_vk_backend_create(&backend_config,
            (jfx_vk_backend_t **)&engine->backend);
    } else if (strcmp(backend_name, "metal") == 0) {
        jfx_mtl_backend_config_t backend_config = { .probe_gpu = true, .memory_limit = 0 };
        engine->kind = JFX_BACKEND_METAL;
        backend_status = jfx_mtl_backend_create(&backend_config,
            (jfx_mtl_backend_t **)&engine->backend);
    } else if (strcmp(backend_name, "d3d12") == 0) {
        jfx_d3d12_backend_config_t backend_config = { .probe_gpu = true, .memory_limit = 0 };
        engine->kind = JFX_BACKEND_D3D12;
        backend_status = jfx_d3d12_backend_create(&backend_config,
            (jfx_d3d12_backend_t **)&engine->backend);
    } else {
        jfx_wgpu_backend_config_t backend_config = { .probe_gpu = true, .memory_limit = 0 };
        engine->kind = JFX_BACKEND_WEBGPU;
        backend_status = jfx_wgpu_backend_create(&backend_config,
            (jfx_wgpu_backend_t **)&engine->backend);
    }
    if (backend_status != JFX_SUCCESS) {
        event_bus_shutdown(); scheduler_shutdown(); memory_shutdown();
        tilly_shutdown(engine->ctx); tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
        return JFX_ERROR_BACKEND_FAILURE;
    }

    memcpy(&engine->config, config, sizeof(jfx_engine_config_t));
    snprintf(engine->backend_name, sizeof(engine->backend_name), "%s", backend_name);
    engine->metrics.size = sizeof(engine->metrics);

    tilly_log_info("jfx_engine", "JoltFX engine initialized");

    *out_engine = engine;
    return JFX_SUCCESS;
}

void jfx_engine_shutdown(jfx_engine_t *engine) {
    if (engine) {
        tilly_log_info("jfx_engine", "JoltFX engine shutting down");
        
        // Shutdown subsystems in reverse order
        switch (engine->kind) {
        case JFX_BACKEND_VULKAN: jfx_vk_backend_destroy((jfx_vk_backend_t *)engine->backend); break;
        case JFX_BACKEND_METAL: jfx_mtl_backend_destroy((jfx_mtl_backend_t *)engine->backend); break;
        case JFX_BACKEND_D3D12: jfx_d3d12_backend_destroy((jfx_d3d12_backend_t *)engine->backend); break;
        case JFX_BACKEND_WEBGPU: jfx_wgpu_backend_destroy((jfx_wgpu_backend_t *)engine->backend); break;
        }
        engine->backend = NULL;
        event_bus_shutdown();
        scheduler_shutdown();
        memory_shutdown();
        
        tilly_shutdown(engine->ctx);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
    }
}

const char *jfx_engine_backend_name(const jfx_engine_t *engine) {
    return engine ? engine->backend_name : NULL;
}

jfx_result_t jfx_engine_get_metrics(const jfx_engine_t *engine,
    jfx_engine_metrics_t *out_metrics) {
    if (!engine || !out_metrics || out_metrics->size < sizeof(*out_metrics)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_metrics = engine->metrics;
    return JFX_SUCCESS;
}

jfx_result_t jfx_engine_tick(jfx_engine_t *engine) {
    if (!engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    
    uint64_t started_ns = timestamp_ns();
    // Begin frame
    event_publish(JFX_EVENT_FRAME_BEGIN, NULL);
    
    // Reset frame arena
    jfx_frame_reset();
    
    // Phase 1 frame boundary: complete queued CPU work before ending the frame.
    scheduler_wait_idle();
    
    // End frame
    event_publish(JFX_EVENT_FRAME_END, NULL);
    
    uint64_t finished_ns = timestamp_ns();
    uint64_t elapsed_ns = finished_ns >= started_ns ? finished_ns - started_ns : 0;
    engine->metrics.frame_count++;
    engine->metrics.last_frame_ns = elapsed_ns;
    engine->metrics.total_frame_ns += elapsed_ns;
    if (elapsed_ns > engine->metrics.max_frame_ns) engine->metrics.max_frame_ns = elapsed_ns;
    return JFX_SUCCESS;
}
