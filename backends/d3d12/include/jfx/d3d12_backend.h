#ifndef JFX_D3D12_BACKEND_H
#define JFX_D3D12_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_d3d12_backend jfx_d3d12_backend_t;
typedef struct { bool probe_gpu; size_t memory_limit; } jfx_d3d12_backend_config_t;
typedef struct {
    bool gpu_available;
    bool cpu_fallback;
    uint32_t api_version;
    char device_name[256];
} jfx_d3d12_caps_t;

const char *jfx_d3d12_backend_name(void);
jfx_result_t jfx_d3d12_backend_create(const jfx_d3d12_backend_config_t *config,
    jfx_d3d12_backend_t **out_backend);
void jfx_d3d12_backend_destroy(jfx_d3d12_backend_t *backend);
void jfx_d3d12_query_caps(const jfx_d3d12_backend_t *backend, jfx_d3d12_caps_t *out_caps);
bool jfx_d3d12_used_gpu(const jfx_d3d12_backend_t *backend);
jfx_result_t jfx_d3d12_execute(jfx_d3d12_backend_t *backend, const char *source,
    const float *input_rgba, size_t pixels, const float *parameters,
    size_t parameter_count, float *output_rgba);
jfx_result_t jfx_d3d12_execute_bytecode(jfx_d3d12_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count, float *output_rgba);

#ifdef __cplusplus
}
#endif

#endif
