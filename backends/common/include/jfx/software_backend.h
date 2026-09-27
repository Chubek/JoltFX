#ifndef JFX_SOFTWARE_BACKEND_H
#define JFX_SOFTWARE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

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

#ifdef __cplusplus
}
#endif

#endif
