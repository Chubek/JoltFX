#include "jfx/jfx_engine.h"
#include "jfx/jfx_buffer.h"
#include "jfx/jfx_events.h"
#include "jfx/jfx_kernel.h"
#include "jfx/jfx_memory.h"
#include "jfx/jfx_scheduler.h"
#include "jfx/jfx_texture.h"
#include "jfx/backend_interface.h"
#if defined(JFX_BACKEND_VULKAN)
#include "jfx/vk_backend.h"
#endif
#if defined(JFX_BACKEND_METAL)
#include "jfx/mtl_backend.h"
#endif
#if defined(JFX_BACKEND_D3D12)
#include "jfx/d3d12_backend.h"
#endif
#if defined(JFX_BACKEND_WEBGPU)
#include "jfx/wgpu_backend.h"
#endif
#include "joltscript/compiler.h"
#include "engine_internal.h"
#include "tilly/tilly.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define JFX_DEFAULT_MAX_BUFFERS 256u
#define JFX_DEFAULT_MAX_TEXTURES 256u
#define JFX_DEFAULT_MAX_KERNELS 64u
#define JFX_MAX_WORKER_THREADS 64u
#define JFX_SOURCE_MAX_BYTES ((size_t)1u << 20)

struct jfx_engine {
    tilly_context_t *ctx;
    jfx_engine_config_t config;
    char backend_name[16];
    const jfx_backend_ops_t *ops;
    jfx_backend_handle_t backend;
    jfx_engine_metrics_t metrics;
    uint32_t live_buffers;
    uint32_t live_textures;
    uint32_t live_kernels;
};

/* ---- Backend registry ----------------------------------------------------
 *
 * "auto" walks this table in priority order and takes the first backend that
 * reports a device. When no device is available the first entry is still used,
 * with its own CPU fallback, so "auto" always yields a working engine. */

typedef jfx_result_t (*jfx_backend_create_fn)(const jfx_backend_config_t *config,
    jfx_backend_handle_t *out_backend);

/* One row per backend, in "auto" priority order. */
typedef struct jfx_backend_entry {
    const char *name;
    const jfx_backend_ops_t *(*ops)(void);
    jfx_backend_create_fn create;
} jfx_backend_entry_t;

#if defined(JFX_BACKEND_VULKAN)
static jfx_result_t create_vulkan(const jfx_backend_config_t *config,
    jfx_backend_handle_t *out_backend) {
    jfx_vk_backend_t *backend = NULL;
    jfx_result_t status = jfx_vk_backend_create(config, &backend);
    *out_backend = backend;
    return status;
}
#endif

#if defined(JFX_BACKEND_METAL)
static jfx_result_t create_metal(const jfx_backend_config_t *config,
    jfx_backend_handle_t *out_backend) {
    jfx_mtl_backend_t *backend = NULL;
    jfx_result_t status = jfx_mtl_backend_create(config, &backend);
    *out_backend = backend;
    return status;
}
#endif

#if defined(JFX_BACKEND_D3D12)
static jfx_result_t create_d3d12(const jfx_backend_config_t *config,
    jfx_backend_handle_t *out_backend) {
    jfx_d3d12_backend_t *backend = NULL;
    jfx_result_t status = jfx_d3d12_backend_create(config, &backend);
    *out_backend = backend;
    return status;
}
#endif

#if defined(JFX_BACKEND_WEBGPU)
static jfx_result_t create_webgpu(const jfx_backend_config_t *config,
    jfx_backend_handle_t *out_backend) {
    jfx_wgpu_backend_t *backend = NULL;
    jfx_result_t status = jfx_wgpu_backend_create(config, &backend);
    *out_backend = backend;
    return status;
}
#endif

static const jfx_backend_entry_t kBackendRegistry[] = {
#if defined(JFX_BACKEND_VULKAN)
    { "vulkan", jfx_vk_backend_ops, create_vulkan },
#endif
#if defined(JFX_BACKEND_METAL)
    { "metal", jfx_mtl_backend_ops, create_metal },
#endif
#if defined(JFX_BACKEND_D3D12)
    { "d3d12", jfx_d3d12_backend_ops, create_d3d12 },
#endif
#if defined(JFX_BACKEND_WEBGPU)
    { "webgpu", jfx_wgpu_backend_ops, create_webgpu },
#endif
};

#define JFX_BACKEND_COUNT (sizeof(kBackendRegistry) / sizeof(kBackendRegistry[0]))

/* NUL-terminated list of the backends this build contains, for diagnostics. */
static const char *kBackendNames =
#if defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_METAL) && \
    defined(JFX_BACKEND_D3D12) && defined(JFX_BACKEND_WEBGPU)
    "vulkan, metal, d3d12, webgpu or auto";
#elif defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_METAL) && \
    defined(JFX_BACKEND_D3D12)
    "vulkan, metal, d3d12 or auto";
#elif defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_METAL) && \
    defined(JFX_BACKEND_WEBGPU)
    "vulkan, metal, webgpu or auto";
#elif defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_D3D12) && \
    defined(JFX_BACKEND_WEBGPU)
    "vulkan, d3d12, webgpu or auto";
#elif defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_METAL)
    "vulkan, metal or auto";
#elif defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_D3D12)
    "vulkan, d3d12 or auto";
#elif defined(JFX_BACKEND_VULKAN) && defined(JFX_BACKEND_WEBGPU)
    "vulkan, webgpu or auto";
#elif defined(JFX_BACKEND_METAL) && defined(JFX_BACKEND_D3D12) && \
    defined(JFX_BACKEND_WEBGPU)
    "metal, d3d12, webgpu or auto";
#elif defined(JFX_BACKEND_VULKAN)
    "vulkan or auto";
#elif defined(JFX_BACKEND_METAL)
    "metal or auto";
#elif defined(JFX_BACKEND_D3D12)
    "d3d12 or auto";
#else
    "webgpu or auto";
#endif

static const jfx_backend_entry_t *find_backend(const char *name) {
    for (size_t i = 0; i < JFX_BACKEND_COUNT; ++i) {
        if (strcmp(kBackendRegistry[i].name, name) == 0) {
            return &kBackendRegistry[i];
        }
    }
    return NULL;
}

/* ---- Helpers ------------------------------------------------------------- */

static uint64_t timestamp_ns(void) {
    struct timespec timestamp;
#if defined(__ANDROID__)
    /* timespec_get is only exported by Bionic starting at API 29. */
    if (clock_gettime(CLOCK_REALTIME, &timestamp) != 0) return 0;
#else
    if (timespec_get(&timestamp, TIME_UTC) != TIME_UTC) return 0;
#endif
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) +
        (uint64_t)timestamp.tv_nsec;
}

static jfx_result_t map_jolt_status(jolt_status_t status) {
    switch (status) {
    case JOLT_OK:
        return JFX_SUCCESS;
    case JOLT_ERR_MEMORY:
        return JFX_ERROR_OUT_OF_MEMORY;
    case JOLT_ERR_ARGUMENT:
    case JOLT_ERR_SYNTAX:
    case JOLT_ERR_BYTECODE:
    case JOLT_ERR_NOT_FOUND:
    case JOLT_ERR_DUPLICATE:
        return JFX_ERROR_INVALID_ARGUMENT;
    default:
        return JFX_ERROR_BACKEND_FAILURE;
    }
}

static uint32_t default_worker_threads(void) {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uint32_t count = info.dwNumberOfProcessors;
#else
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t count = online > 0 ? (uint32_t)online : 1u;
#endif
    if (count == 0) count = 1u;
    return count > JFX_MAX_WORKER_THREADS ? JFX_MAX_WORKER_THREADS : count;
}

/* ---- Lifecycle ----------------------------------------------------------- */

jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine) {
    if (!config || !out_engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_engine = NULL;

    /* An explicit name must exist in the registry; NULL and "auto" do not. */
    const jfx_backend_entry_t *selected = NULL;
    if (config->backend_name && strcmp(config->backend_name, "auto") != 0) {
        selected = find_backend(config->backend_name);
        if (!selected) {
            tilly_log_simple(TILLY_LOG_ERROR,
                "jfx_engine_init: unknown backend '%s' (this build provides %s)",
                config->backend_name, kBackendNames);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
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
        goto fail_not_initialized;
    }

    // The scheduler owns every worker thread; callers size it explicitly
    // rather than by repurposing a resource limit.
    uint32_t worker_count = config->max_worker_threads
        ? config->max_worker_threads
        : default_worker_threads();
    if (worker_count > JFX_MAX_WORKER_THREADS) worker_count = JFX_MAX_WORKER_THREADS;
    if (!scheduler_init(worker_count)) {
        tilly_log_simple(TILLY_LOG_ERROR,
            "jfx_engine_init: could not start %u scheduler worker threads", worker_count);
        memory_shutdown();
        goto fail_not_initialized;
    }

    // Initialize event bus
    if (!event_bus_init()) {
        scheduler_shutdown();
        memory_shutdown();
        goto fail_not_initialized;
    }

    // Select the backend last so shutdown unwinds in reverse order.
    jfx_backend_config_t backend_config = {
        .probe_gpu = true,
        .memory_limit = 0,
    };
    if (selected) {
        engine->backend = NULL;
        jfx_result_t status = selected->create(&backend_config, &engine->backend);
        if (status != JFX_SUCCESS || !engine->backend) {
            event_bus_shutdown();
            scheduler_shutdown();
            memory_shutdown();
            goto fail_backend;
        }
        engine->ops = selected->ops();
        snprintf(engine->backend_name, sizeof(engine->backend_name), "%s", selected->name);
    } else {
        /* "auto": take the first backend that reports a usable device. If none
         * do, fall back to the highest-priority entry so the engine still
         * runs, on that backend's CPU path. */
        engine->backend = NULL;
        engine->ops = NULL;
        for (size_t i = 0; i < JFX_BACKEND_COUNT; ++i) {
            jfx_backend_handle_t handle = NULL;
            if (kBackendRegistry[i].create(&backend_config, &handle) != JFX_SUCCESS || !handle) {
                continue;
            }
            jfx_backend_caps_t caps;
            kBackendRegistry[i].ops()->query_caps(handle, &caps);
            if (!engine->backend) {
                /* Remember the first successfully created backend. */
                engine->backend = handle;
                engine->ops = kBackendRegistry[i].ops();
                snprintf(engine->backend_name, sizeof(engine->backend_name), "%s",
                    kBackendRegistry[i].name);
            }
            if (caps.gpu_available) {
                if (handle != engine->backend) {
                    engine->ops->destroy(engine->backend);
                    engine->backend = handle;
                    engine->ops = kBackendRegistry[i].ops();
                    snprintf(engine->backend_name, sizeof(engine->backend_name), "%s",
                        kBackendRegistry[i].name);
                }
                break;
            }
            /* Keep the first CPU fallback alive while probing later entries. */
            if (handle != engine->backend) kBackendRegistry[i].ops()->destroy(handle);
        }
        if (!engine->backend) {
            event_bus_shutdown();
            scheduler_shutdown();
            memory_shutdown();
            goto fail_backend;
        }
    }

    memcpy(&engine->config, config, sizeof(jfx_engine_config_t));
    engine->config.max_worker_threads = worker_count;
    engine->config.max_buffers = config->max_buffers ? config->max_buffers : JFX_DEFAULT_MAX_BUFFERS;
    engine->config.max_textures = config->max_textures ? config->max_textures : JFX_DEFAULT_MAX_TEXTURES;
    engine->config.max_kernels = config->max_kernels ? config->max_kernels : JFX_DEFAULT_MAX_KERNELS;
    engine->metrics.size = sizeof(engine->metrics);

    tilly_log_info("jfx_engine", "JoltFX engine initialized (backend %s)", engine->backend_name);

    *out_engine = engine;
    return JFX_SUCCESS;

fail_backend:
    tilly_shutdown(engine->ctx);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
    return JFX_ERROR_BACKEND_FAILURE;
fail_not_initialized:
    tilly_shutdown(engine->ctx);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
    return JFX_ERROR_NOT_INITIALIZED;
}

void jfx_engine_shutdown(jfx_engine_t *engine) {
    if (!engine) {
        return;
    }
    tilly_log_info("jfx_engine", "JoltFX engine shutting down");

    /* Leaked live objects would let callers keep using freed storage after
     * shutdown; the counts make that visible instead of silent. */
    if (engine->live_buffers || engine->live_textures || engine->live_kernels) {
        tilly_log_simple(TILLY_LOG_WARN,
            "engine shutdown with live objects: %u buffers, %u textures, %u kernels",
            engine->live_buffers, engine->live_textures, engine->live_kernels);
    }

    if (engine->ops && engine->backend) {
        engine->ops->destroy(engine->backend);
    }
    engine->backend = NULL;
    engine->ops = NULL;
    event_bus_shutdown();
    scheduler_shutdown();
    memory_shutdown();

    tilly_shutdown(engine->ctx);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), engine);
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

jfx_result_t jfx_engine_get_caps(const jfx_engine_t *engine, jfx_engine_caps_t *out_caps) {
    if (!engine || !out_caps || out_caps->size < sizeof(*out_caps)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_backend_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    if (engine->ops) {
        engine->ops->query_caps(engine->backend, &caps);
    }
    out_caps->gpu_available = caps.gpu_available;
    out_caps->used_gpu = caps.used_gpu;
    out_caps->api_version = caps.api_version;
    memcpy(out_caps->device_name, caps.device_name, sizeof(out_caps->device_name));
    out_caps->device_name[sizeof(out_caps->device_name) - 1u] = '\0';
    return JFX_SUCCESS;
}

bool jfx_engine_used_gpu(const jfx_engine_t *engine) {
    jfx_engine_caps_t caps = { .size = sizeof(caps) };
    if (!engine || jfx_engine_get_caps(engine, &caps) != JFX_SUCCESS) {
        return false;
    }
    return caps.used_gpu;
}

uint32_t jfx_engine_live_buffers(const jfx_engine_t *engine) { return engine_live_buffers(engine); }
uint32_t jfx_engine_live_textures(const jfx_engine_t *engine) { return engine_live_textures(engine); }
uint32_t jfx_engine_live_kernels(const jfx_engine_t *engine) { return engine_live_kernels(engine); }

/* ---- Execution ----------------------------------------------------------- */

jfx_result_t jfx_engine_execute_bytecode(jfx_engine_t *engine,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    if (!engine || !engine->ops || !bytecode || !bytecode_size || !input_rgba ||
        !output_rgba || !pixels || pixels > SIZE_MAX / (4u * sizeof(float)) ||
        (parameter_count && !parameters)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    event_publish(JFX_EVENT_KERNEL_SUBMIT, NULL);
    jfx_result_t status = engine->ops->execute_bytecode(engine->backend, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
    if (status == JFX_SUCCESS) {
        event_publish(JFX_EVENT_KERNEL_COMPLETE, NULL);
    } else {
        event_publish(JFX_EVENT_KERNEL_ERROR, NULL);
    }
    return status;
}

jfx_result_t jfx_engine_execute_source(jfx_engine_t *engine, const char *source,
    const float *input_rgba, size_t pixels, const float *parameters,
    size_t parameter_count, float *output_rgba, uint8_t **out_bytecode,
    size_t *out_bytecode_size) {
    if (!engine || !source || !input_rgba || !output_rgba || !pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diagnostic = { .size = sizeof(diagnostic) };
    jolt_status_t compiled = jolt_compile(source, &program, &diagnostic);
    if (compiled != JOLT_OK) {
        return map_jolt_status(compiled);
    }
    size_t bytecode_size = 0;
    const uint8_t *bytecode = jolt_program_data(program, &bytecode_size);
    if (!bytecode || !bytecode_size) {
        jolt_program_destroy(program);
        return JFX_ERROR_BACKEND_FAILURE;
    }

    uint8_t *owned = NULL;
    if (out_bytecode || out_bytecode_size) {
        owned = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytecode_size,
            _Alignof(max_align_t));
        if (!owned) {
            jolt_program_destroy(program);
            return JFX_ERROR_OUT_OF_MEMORY;
        }
        memcpy(owned, bytecode, bytecode_size);
        if (out_bytecode) *out_bytecode = owned;
        if (out_bytecode_size) *out_bytecode_size = bytecode_size;
    }

    jfx_result_t status = jfx_engine_execute_bytecode(engine, bytecode, bytecode_size,
        input_rgba, pixels, parameters, parameter_count, output_rgba);
    jolt_program_destroy(program);
    if (status != JFX_SUCCESS && owned) {
        if (out_bytecode) *out_bytecode = NULL;
        if (out_bytecode_size) *out_bytecode_size = 0;
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), owned);
    }
    return status;
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

/* ---- Live-object accounting, shared by buffers, textures and kernels ------ */

uint32_t engine_limit(const jfx_engine_t *engine, uint32_t configured, uint32_t fallback) {
    (void)engine;
    return configured ? configured : fallback;
}

static bool engine_take_slot(uint32_t *counter, uint32_t limit, const char *what) {
    if (*counter >= limit) {
        tilly_log_simple(TILLY_LOG_WARN, "engine %s limit reached (%u live)", what, limit);
        return false;
    }
    (*counter)++;
    return true;
}

static void engine_give_slot(uint32_t *counter) {
    if (*counter) {
        (*counter)--;
    }
}

bool engine_acquire_buffer(jfx_engine_t *engine) {
    return engine && engine_take_slot(&engine->live_buffers, engine->config.max_buffers, "buffer");
}

void engine_release_buffer(jfx_engine_t *engine) {
    if (engine) engine_give_slot(&engine->live_buffers);
}

uint32_t engine_live_buffers(const jfx_engine_t *engine) { return engine ? engine->live_buffers : 0u; }

bool engine_acquire_texture(jfx_engine_t *engine) {
    return engine && engine_take_slot(&engine->live_textures, engine->config.max_textures, "texture");
}

void engine_release_texture(jfx_engine_t *engine) {
    if (engine) engine_give_slot(&engine->live_textures);
}

uint32_t engine_live_textures(const jfx_engine_t *engine) { return engine ? engine->live_textures : 0u; }

bool engine_acquire_kernel(jfx_engine_t *engine) {
    return engine && engine_take_slot(&engine->live_kernels, engine->config.max_kernels, "kernel");
}

void engine_release_kernel(jfx_engine_t *engine) {
    if (engine) engine_give_slot(&engine->live_kernels);
}

uint32_t engine_live_kernels(const jfx_engine_t *engine) { return engine ? engine->live_kernels : 0u; }
