#ifndef JFX_VK_BACKEND_H
#define JFX_VK_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "jfx/jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_vk_backend jfx_vk_backend_t;

typedef struct {
    /* Attempt a Vulkan driver probe with dlopen at create time. The probe
     * never links the SDK at build time, so the library builds and runs on
     * machines without Vulkan installed. Defaults to true when zeroed. */
    bool probe_gpu;
    /* Per-call scratch budget in bytes for temporary image storage.
     * 0 selects the default (64 MiB). */
    size_t memory_limit;
} jfx_vk_backend_config_t;

typedef struct {
    bool gpu_available;      /* Vulkan driver with instance API found. */
    bool cpu_fallback;       /* Always true in Phase 2: pixels run on CPU. */
    uint32_t api_version;    /* Probed Vulkan instance version, else 0. */
    char device_name[256];   /* Driver description or "cpu-fallback". */
} jfx_vk_caps_t;

#define JFX_VK_DEFAULT_MEMORY_LIMIT ((size_t)64u * 1024u * 1024u)

const char *jfx_vk_backend_name(void);

jfx_result_t jfx_vk_backend_create(const jfx_vk_backend_config_t *config,
    jfx_vk_backend_t **out_backend);
void jfx_vk_backend_destroy(jfx_vk_backend_t *backend);
void jfx_vk_query_caps(const jfx_vk_backend_t *backend, jfx_vk_caps_t *out_caps);

/* Reports whether the most recent execute ran on the device (true) or the
 * CPU fallback (false). Starts false; never set when creation disabled the
 * GPU probe. Useful for conformance diagnostics. */
bool jfx_vk_used_gpu(const jfx_vk_backend_t *backend);

/* Compile Joltscript source (MVP defkernel form) and run it over interleaved
 * premultiplied RGBA float pixels. parameters holds the kernel's trailing
 * uniform arguments (e.g. amount for brightness). Output is unchanged when
 * an error is returned; in-place execution (input == output) is safe. */
jfx_result_t jfx_vk_execute(jfx_vk_backend_t *backend, const char *source,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba);

/* Run precompiled JBC1 bytecode with explicit input/output counts. The
 * program must declare 4 outputs (RGBA); inputs are 4 + parameter_count. */
jfx_result_t jfx_vk_execute_bytecode(jfx_vk_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba);

#ifdef __cplusplus
}
#endif

#endif /* JFX_VK_BACKEND_H */
