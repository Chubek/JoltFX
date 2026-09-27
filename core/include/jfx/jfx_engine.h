#ifndef JFX_ENGINE_H
#define JFX_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_engine jfx_engine_t;

typedef struct {
    /* Maximum number of live jfx_buffer_t objects. 0 selects the default (256). */
    uint32_t max_buffers;
    /* Maximum number of live jfx_texture_t objects. 0 selects the default (256). */
    uint32_t max_textures;
    /* Maximum number of live jfx_kernel_t objects. 0 selects the default (64). */
    uint32_t max_kernels;
    /* Maximum number of worker threads the scheduler owns. 0 selects a value
     * derived from the hardware concurrency (clamped to [1, 64]). */
    uint32_t max_worker_threads;
    /* "vulkan", "metal", "d3d12", "webgpu", "auto", or NULL for auto. */
    const char *backend_name;
} jfx_engine_config_t;

/* Backend capability snapshot, as reported by the selected backend. */
typedef struct {
    size_t size;                 /* set to sizeof(jfx_engine_caps_t) by the caller */
    bool gpu_available;          /* the backend holds a usable device/queue */
    bool used_gpu;               /* the last execute ran on that device */
    uint32_t api_version;        /* native API version, 0 on a CPU path */
    char device_name[256];       /* device/driver description or path name */
} jfx_engine_caps_t;

/* Snapshot of the engine's lightweight frame timing instrumentation. */
typedef struct {
    size_t size;
    uint64_t frame_count;
    uint64_t last_frame_ns;
    uint64_t max_frame_ns;
    uint64_t total_frame_ns;
} jfx_engine_metrics_t;

// Initialize the engine
jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine);

// Shutdown the engine
void jfx_engine_shutdown(jfx_engine_t *engine);

// Process one frame: fire the frame events, reset the frame arena, and drain
// the scheduler queue. Rendering is driven explicitly through
// jfx_engine_execute_bytecode, not here.
jfx_result_t jfx_engine_tick(jfx_engine_t *engine);

// Resolved backend name ("vulkan" for NULL/"auto"), NULL for a NULL engine.
const char *jfx_engine_backend_name(const jfx_engine_t *engine);

// Copies timing metrics into out_metrics. out_metrics->size must be set.
jfx_result_t jfx_engine_get_metrics(const jfx_engine_t *engine,
    jfx_engine_metrics_t *out_metrics);

/* Runs precompiled JBC1 bytecode through the engine's backend over interleaved
 * RGBA float pixels. The program must declare four outputs (RGBA); its inputs
 * are the four pixel channels followed by parameter_count parameters.
 *
 * Contract shared with every backend: output_rgba is left untouched when an
 * error is returned, in-place execution (input_rgba == output_rgba) is safe,
 * and parameters must be non-NULL whenever parameter_count is non-zero. */
jfx_result_t jfx_engine_execute_bytecode(jfx_engine_t *engine,
    const uint8_t *bytecode, size_t bytecode_size,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba);

/* Compiles Joltscript source and runs it through the engine's backend.
 * bytecode_out, when non-NULL, receives an engine-owned copy of the compiled
 * JBC1 program that stays valid until the engine is shut down. */
jfx_result_t jfx_engine_execute_source(jfx_engine_t *engine, const char *source,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba,
    uint8_t **out_bytecode, size_t *out_bytecode_size);

// Copies the selected backend's capabilities into out_caps.
// out_caps->size must be set to sizeof(jfx_engine_caps_t).
jfx_result_t jfx_engine_get_caps(const jfx_engine_t *engine,
    jfx_engine_caps_t *out_caps);

// True when the most recent execute on this engine ran on a GPU device rather
// than a CPU fallback path.
bool jfx_engine_used_gpu(const jfx_engine_t *engine);

// Live-object accounting against the limits in jfx_engine_config_t.
uint32_t jfx_engine_live_buffers(const jfx_engine_t *engine);
uint32_t jfx_engine_live_textures(const jfx_engine_t *engine);
uint32_t jfx_engine_live_kernels(const jfx_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif // JFX_ENGINE_H
