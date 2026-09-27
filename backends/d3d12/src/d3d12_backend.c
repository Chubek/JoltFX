/* D3D12 backend.
 *
 * Direct3D 12 is reachable from a plain C compiler on Windows, but only when
 * the Windows SDK is present. This file is the portable half of the backend: it
 * owns the D3D12 lifecycle, capability reporting and the HAL operations table,
 * and dispatches execution through the shared CPU path in
 * jfx_software_adapter. Building it on a host without the SDK still produces a
 * working, honest backend rather than a build failure.
 *
 * Going native means reimplementing `ops_execute_bytecode` with
 * ID3D12Device/root-signature/PSO dispatch, SPIR-V -> HLSL -> DXIL via DXC, and
 * making `ops_query_caps` report the real adapter and feature level; nothing
 * else here changes. */

#include "jfx/d3d12_backend.h"
#include "jfx/software_backend.h"
#include "tilly/allocator.h"

struct jfx_d3d12_backend { jfx_software_adapter_t *adapter; };

const char *jfx_d3d12_backend_name(void) {
    return "d3d12";
}

jfx_result_t jfx_d3d12_backend_create(const jfx_d3d12_backend_config_t *config,
    jfx_d3d12_backend_t **out_backend) {
    if (!out_backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_backend = NULL;
    jfx_software_adapter_t *adapter = NULL;
    jfx_result_t status = jfx_software_adapter_create(jfx_d3d12_backend_name(),
        config ? config->memory_limit : 0, &adapter);
    if (status != JFX_SUCCESS) {
        return status;
    }
    struct jfx_d3d12_backend *backend =
        tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), sizeof(*backend),
            _Alignof(struct jfx_d3d12_backend));
    if (!backend) {
        jfx_software_adapter_destroy(adapter);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    backend->adapter = adapter;
    *out_backend = backend;
    return JFX_SUCCESS;
}

void jfx_d3d12_backend_destroy(jfx_d3d12_backend_t *backend) {
    if (!backend) {
        return;
    }
    jfx_software_adapter_destroy(backend->adapter);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), backend);
}

void jfx_d3d12_query_caps(const jfx_d3d12_backend_t *backend, jfx_d3d12_caps_t *out_caps) {
    jfx_software_adapter_query_caps(backend ? backend->adapter : NULL, out_caps);
}

bool jfx_d3d12_used_gpu(const jfx_d3d12_backend_t *backend) {
    return jfx_software_adapter_used_gpu(backend ? backend->adapter : NULL);
}

jfx_result_t jfx_d3d12_execute(jfx_d3d12_backend_t *backend, const char *source,
    const float *input_rgba, size_t pixels, const float *parameters,
    size_t parameter_count, float *output_rgba) {
    if (!backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_software_adapter_execute(backend->adapter, source, input_rgba, pixels,
        parameters, parameter_count, output_rgba);
}

jfx_result_t jfx_d3d12_execute_bytecode(jfx_d3d12_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    if (!backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_software_adapter_execute_bytecode(backend->adapter, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}

/* ---- HAL ---- */

static void ops_query_caps(jfx_backend_handle_t handle, jfx_backend_caps_t *out_caps) {
    jfx_d3d12_query_caps((jfx_d3d12_backend_t *)handle, out_caps);
}

static jfx_result_t ops_execute_bytecode(jfx_backend_handle_t handle,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    return jfx_d3d12_execute_bytecode((jfx_d3d12_backend_t *)handle, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}

static void ops_destroy(jfx_backend_handle_t handle) {
    jfx_d3d12_backend_destroy((jfx_d3d12_backend_t *)handle);
}

static const jfx_backend_ops_t kD3D12Ops = {
    .name = jfx_d3d12_backend_name,
    .query_caps = ops_query_caps,
    .execute_bytecode = ops_execute_bytecode,
    .destroy = ops_destroy,
};

const jfx_backend_ops_t *jfx_d3d12_backend_ops(void) {
    return &kD3D12Ops;
}
