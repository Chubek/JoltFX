#ifndef JFX_SOFTWARE_BACKEND_H
#define JFX_SOFTWARE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/backend_interface.h"
#include "jfx/jfx_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_software_backend jfx_software_backend_t;

jfx_result_t jfx_software_backend_create(const char *name, size_t memory_limit,
    jfx_software_backend_t **out_backend);
void jfx_software_backend_destroy(jfx_software_backend_t *backend);
void jfx_software_backend_query_caps(const jfx_software_backend_t *backend,
    bool *gpu_available, bool *cpu_fallback, uint32_t *api_version,
    char *device_name, size_t device_name_size);
bool jfx_software_backend_used_gpu(const jfx_software_backend_t *backend);
jfx_result_t jfx_software_backend_execute(jfx_software_backend_t *backend,
    const char *source, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba);
jfx_result_t jfx_software_backend_execute_bytecode(jfx_software_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba);

/* ---- Shared CPU-path backend adapter -------------------------------------
 *
 * The Metal, D3D12 and WebGPU backends ship as portable, SDK-independent
 * translation units: they own lifecycle and capability reporting for their
 * platform but execute through the validated CPU pipeline above, because the
 * native API for that platform is not reachable from a plain C compiler on
 * every host. They are therefore the same object with three different names,
 * and that object lives here.
 *
 * Adding native dispatch to one of them means replacing
 * `jfx_software_adapter_execute_bytecode` for that name and reporting
 * `gpu_available`/`used_gpu` truthfully; the lifecycle, caps and HAL surface
 * stay exactly as they are. */

typedef struct jfx_software_adapter jfx_software_adapter_t;

jfx_result_t jfx_software_adapter_create(const char *name, size_t memory_limit,
    jfx_software_adapter_t **out_backend);
void jfx_software_adapter_destroy(jfx_software_adapter_t *backend);
void jfx_software_adapter_query_caps(const jfx_software_adapter_t *backend,
    jfx_backend_caps_t *out_caps);
bool jfx_software_adapter_used_gpu(const jfx_software_adapter_t *backend);
jfx_result_t jfx_software_adapter_execute(jfx_software_adapter_t *backend,
    const char *source, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba);
jfx_result_t jfx_software_adapter_execute_bytecode(jfx_software_adapter_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba);

/* Stable device_name reported while a CPU path is in use. */
#define JFX_SOFTWARE_FALLBACK_NAME "software-fallback"

#ifdef __cplusplus
}
#endif

#endif
