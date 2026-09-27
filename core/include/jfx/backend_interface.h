#ifndef JFX_BACKEND_INTERFACE_H
#define JFX_BACKEND_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque, backend-native handle. The engine never dereferences this; it only
 * hands it back to the owning backend's operations table. */
typedef void *jfx_backend_handle_t;

/* Configuration every backend accepts. The per-backend config types
 * (jfx_vk_backend_config_t and friends) are aliases of this. */
typedef struct jfx_backend_config {
    /* Probe for a usable device at create time. Defaults to true when zeroed. */
    bool probe_gpu;
    /* Per-call scratch budget in bytes. 0 selects the backend default. */
    size_t memory_limit;
} jfx_backend_config_t;

/* Backend-reported capabilities.
 *
 * gpu_available : the backend holds a usable device/queue. Probed once at
 *                 creation time and cached.
 * used_gpu      : the most recent execute actually ran on that device. False
 *                 after a failed dispatch or when no device was available.
 * api_version   : native API version the backend is bound to (Vulkan instance
 *                 version, D3D12 feature level, Metal GPU family, ...), or 0
 *                 when the backend is running a CPU path.
 * device_name   : human-readable device/driver description, or a stable
 *                 placeholder naming the path actually in use. */
typedef struct jfx_backend_caps {
    bool gpu_available;
    bool used_gpu;
    uint32_t api_version;
    char device_name[256];
} jfx_backend_caps_t;

/* The HAL every backend implements. Native types never cross this boundary: a
 * backend receives its own handle and is the only code that knows what is
 * inside it.
 *
 * This header lives with the engine, not with the backends, because the engine
 * owns the contract: it selects a backend, drives it through this table and
 * reads the capabilities back. Backends implement it; they do not define it. */
typedef struct jfx_backend_ops {
    /* Stable backend identifier ("vulkan", "metal", "d3d12", "webgpu"). */
    const char *(*name)(void);
    /* Fills *out_caps. Must tolerate a NULL handle. */
    void (*query_caps)(jfx_backend_handle_t backend, jfx_backend_caps_t *out_caps);
    /* Runs precompiled JBC1 bytecode over interleaved RGBA float pixels.
     * Contract: output_rgba is untouched on error, in-place execution
     * (input_rgba == output_rgba) is safe, and parameters is non-NULL when
     * parameter_count is non-zero. */
    jfx_result_t (*execute_bytecode)(jfx_backend_handle_t backend,
        const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
        size_t pixels, const float *parameters, size_t parameter_count,
        float *output_rgba);
    void (*destroy)(jfx_backend_handle_t backend);
} jfx_backend_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* JFX_BACKEND_INTERFACE_H */
