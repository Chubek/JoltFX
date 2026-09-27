#include "jfx/vk_backend.h"

#include <stdio.h>
#include <string.h>

#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "joltscript/vm.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "vk_compute.h"

struct jfx_vk_backend {
    size_t memory_limit;
    bool gpu_available;
    uint32_t api_version;
    char device_name[256];
    jvk_compute_device_t *gpu;
    bool used_gpu;
};

static void *backend_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static void backend_free(void *ptr) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), ptr);
}

const char *jfx_vk_backend_name(void) {
    return "vulkan";
}

/* ---- HAL ---- */

static void ops_query_caps(jfx_backend_handle_t handle, jfx_backend_caps_t *out_caps) {
    jfx_vk_query_caps((jfx_vk_backend_t *)handle, out_caps);
}

static jfx_result_t ops_execute_bytecode(jfx_backend_handle_t handle,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    return jfx_vk_execute_bytecode((jfx_vk_backend_t *)handle, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}

static void ops_destroy(jfx_backend_handle_t handle) {
    jfx_vk_backend_destroy((jfx_vk_backend_t *)handle);
}

static const jfx_backend_ops_t kVulkanOps = {
    .name = jfx_vk_backend_name,
    .query_caps = ops_query_caps,
    .execute_bytecode = ops_execute_bytecode,
    .destroy = ops_destroy,
};

const jfx_backend_ops_t *jfx_vk_backend_ops(void) {
    return &kVulkanOps;
}

static jfx_result_t map_status(jolt_status_t status) {
    switch (status) {
    case JOLT_OK:
        return JFX_SUCCESS;
    case JOLT_ERR_ARGUMENT:
    case JOLT_ERR_SYNTAX:
    case JOLT_ERR_BYTECODE:
    case JOLT_ERR_NOT_FOUND:
    case JOLT_ERR_DUPLICATE:
        return JFX_ERROR_INVALID_ARGUMENT;
    case JOLT_ERR_MEMORY:
        return JFX_ERROR_OUT_OF_MEMORY;
    case JOLT_ERR_NUMERIC:
    case JOLT_ERR_CAPABILITY:
    case JOLT_ERR_BUDGET:
    case JOLT_ERR_CYCLE:
    default:
        return JFX_ERROR_BACKEND_FAILURE;
    }
}

jfx_result_t jfx_vk_backend_create(const jfx_vk_backend_config_t *config,
    jfx_vk_backend_t **out_backend) {
    if (!out_backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_backend = NULL;
    jfx_vk_backend_t *backend = backend_alloc(sizeof(*backend));
    if (!backend) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(backend, 0, sizeof(*backend));
    backend->memory_limit = config && config->memory_limit
        ? config->memory_limit
        : JFX_VK_DEFAULT_MEMORY_LIMIT;
    bool probe = !config || config->probe_gpu;
    if (probe) {
        backend->gpu = jvk_device_create(backend->device_name,
            sizeof(backend->device_name), &backend->api_version);
    } else {
        snprintf(backend->device_name, sizeof(backend->device_name), "cpu-fallback");
    }
    backend->gpu_available = backend->gpu != NULL;
    if (backend->gpu_available) {
        tilly_log_simple(TILLY_LOG_INFO, "Vulkan backend ready (device dispatch enabled)");
    } else {
        tilly_log_simple(TILLY_LOG_INFO, "Vulkan backend ready (using CPU fallback)");
    }
    *out_backend = backend;
    return JFX_SUCCESS;
}

void jfx_vk_backend_destroy(jfx_vk_backend_t *backend) {
    if (!backend) {
        return;
    }
    jvk_device_destroy(backend->gpu);
    backend_free(backend);
}

void jfx_vk_query_caps(const jfx_vk_backend_t *backend, jfx_vk_caps_t *out_caps) {
    if (!out_caps) {
        return;
    }
    memset(out_caps, 0, sizeof(*out_caps));
    if (!backend) {
        snprintf(out_caps->device_name, sizeof(out_caps->device_name), "cpu-fallback");
        return;
    }
    out_caps->gpu_available = backend->gpu_available;
    out_caps->used_gpu = backend->used_gpu;
    out_caps->api_version = backend->api_version;
    snprintf(out_caps->device_name, sizeof(out_caps->device_name), "%s",
        backend->device_name);
}

bool jfx_vk_used_gpu(const jfx_vk_backend_t *backend) {
    return backend && backend->used_gpu;
}

static jfx_result_t execute_cpu(jfx_vk_backend_t *backend, const uint8_t *bytecode,
    size_t bytecode_size, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba) {
    jolt_pipeline_t *pipeline = jolt_pipeline_create(backend->memory_limit);
    if (!pipeline) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    size_t stage = 0;
    jolt_status_t status = jolt_pipeline_add(pipeline, bytecode, bytecode_size, -1,
        parameters, parameter_count, &stage);
    if (status == JOLT_OK) {
        status = jolt_pipeline_run(pipeline, input_rgba, pixels, stage, output_rgba);
    }
    jolt_pipeline_destroy(pipeline);
    return map_status(status);
}

jfx_result_t jfx_vk_execute_bytecode(jfx_vk_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba) {
    if (!backend || !bytecode || !input_rgba || !output_rgba || !pixels ||
        pixels > SIZE_MAX / (4 * sizeof(float)) ||
        (parameter_count && !parameters)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    backend->used_gpu = backend->gpu != NULL;
    if (backend->gpu) {
        int status = jvk_compute_run(backend->gpu, bytecode, bytecode_size,
            input_rgba, pixels, parameters, parameter_count, output_rgba,
            backend->memory_limit);
        if (status != 0) {
            tilly_log_simple(TILLY_LOG_ERROR, "Vulkan backend device execution failed");
            return JFX_ERROR_BACKEND_FAILURE;
        }
        return JFX_SUCCESS;
    }
    jfx_result_t result = execute_cpu(backend, bytecode, bytecode_size, input_rgba,
        pixels, parameters, parameter_count, output_rgba);
    if (result != JFX_SUCCESS) {
        tilly_log_simple(TILLY_LOG_ERROR, "Vulkan backend execution failed");
    }
    return result;
}

jfx_result_t jfx_vk_execute(jfx_vk_backend_t *backend, const char *source,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba) {
    if (!backend || !source || !input_rgba || !output_rgba || !pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diagnostic = { .size = sizeof(diagnostic) };
    jolt_status_t status = jolt_compile(source, &program, &diagnostic);
    if (status != JOLT_OK) {
        tilly_log_simple(TILLY_LOG_ERROR, "Vulkan backend compile failed");
        return map_status(status);
    }
    size_t bytecode_size = 0;
    const uint8_t *bytecode = jolt_program_data(program, &bytecode_size);
    jfx_result_t result = jfx_vk_execute_bytecode(backend, bytecode, bytecode_size,
        input_rgba, pixels, parameters, parameter_count, output_rgba);
    jolt_program_destroy(program);
    return result;
}
