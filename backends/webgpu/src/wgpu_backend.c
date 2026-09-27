#include "jfx/wgpu_backend.h"

#include <string.h>

#include "jfx/software_backend.h"
#include "tilly/allocator.h"

struct jfx_wgpu_backend { jfx_software_backend_t *software; };

const char *jfx_wgpu_backend_name(void) { return "webgpu"; }
jfx_result_t jfx_wgpu_backend_create(const jfx_wgpu_backend_config_t *config,
    jfx_wgpu_backend_t **out_backend) {
    if (!out_backend) return JFX_ERROR_INVALID_ARGUMENT;
    *out_backend = NULL;
    jfx_software_backend_t *software = NULL;
    jfx_result_t status = jfx_software_backend_create("webgpu",
        config ? config->memory_limit : 0, &software);
    if (status != JFX_SUCCESS) return status;
    jfx_wgpu_backend_t *backend = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*backend), _Alignof(jfx_wgpu_backend_t));
    if (!backend) { jfx_software_backend_destroy(software); return JFX_ERROR_OUT_OF_MEMORY; }
    backend->software = software;
    *out_backend = backend;
    return JFX_SUCCESS;
}
void jfx_wgpu_backend_destroy(jfx_wgpu_backend_t *backend) {
    if (backend) { jfx_software_backend_destroy(backend->software); tilly_free(
        (tilly_allocator_t *)tilly_default_allocator(), backend); }
}
void jfx_wgpu_query_caps(const jfx_wgpu_backend_t *backend, jfx_wgpu_caps_t *caps) {
    if (!caps) return;
    memset(caps, 0, sizeof(*caps));
    jfx_software_backend_query_caps(backend ? backend->software : NULL, &caps->gpu_available,
        &caps->cpu_fallback, &caps->api_version, caps->device_name, sizeof(caps->device_name));
}
bool jfx_wgpu_used_gpu(const jfx_wgpu_backend_t *backend) {
    return backend && jfx_software_backend_used_gpu(backend->software);
}
jfx_result_t jfx_wgpu_execute(jfx_wgpu_backend_t *backend, const char *source,
    const float *input, size_t pixels, const float *parameters, size_t count, float *output) {
    return jfx_software_backend_execute(backend ? backend->software : NULL, source, input, pixels,
        parameters, count, output);
}
jfx_result_t jfx_wgpu_execute_bytecode(jfx_wgpu_backend_t *backend, const uint8_t *code,
    size_t size, const float *input, size_t pixels, const float *parameters, size_t count,
    float *output) {
    return jfx_software_backend_execute_bytecode(backend ? backend->software : NULL, code, size,
        input, pixels, parameters, count, output);
}
