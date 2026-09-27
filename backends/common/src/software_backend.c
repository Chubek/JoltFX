#include "jfx/software_backend.h"

#include <stdio.h>
#include <string.h>

#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "tilly/allocator.h"

#define JFX_SOFTWARE_DEFAULT_MEMORY_LIMIT ((size_t)64u * 1024u * 1024u)

struct jfx_software_backend {
    size_t memory_limit;
    char name[16];
};

static jfx_result_t map_status(jolt_status_t status) {
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

static void *software_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

jfx_result_t jfx_software_backend_create(const char *name, size_t memory_limit,
    jfx_software_backend_t **out_backend) {
    if (!name || !out_backend || !name[0]) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_backend = NULL;
    jfx_software_backend_t *backend = software_alloc(sizeof(*backend));
    if (!backend) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    backend->memory_limit = memory_limit ? memory_limit : JFX_SOFTWARE_DEFAULT_MEMORY_LIMIT;
    snprintf(backend->name, sizeof(backend->name), "%s", name);
    *out_backend = backend;
    return JFX_SUCCESS;
}

void jfx_software_backend_destroy(jfx_software_backend_t *backend) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), backend);
}

void jfx_software_backend_query_caps(const jfx_software_backend_t *backend,
    bool *gpu_available, bool *cpu_fallback, uint32_t *api_version,
    char *device_name, size_t device_name_size) {
    if (gpu_available) {
        *gpu_available = false;
    }
    if (cpu_fallback) {
        *cpu_fallback = true;
    }
    if (api_version) {
        *api_version = 0;
    }
    if (device_name && device_name_size) {
        /* Name the backend whose CPU path is in use, so a capability report
         * from three different adapters is distinguishable. */
        snprintf(device_name, device_name_size, "%s: %s",
            backend ? backend->name : "unknown", JFX_SOFTWARE_FALLBACK_NAME);
    }
}

bool jfx_software_backend_used_gpu(const jfx_software_backend_t *backend) {
    (void)backend;
    return false;
}

jfx_result_t jfx_software_backend_execute_bytecode(jfx_software_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    if (!backend || !bytecode || !input_rgba || !output_rgba || !pixels ||
        pixels > SIZE_MAX / (4u * sizeof(float)) ||
        (parameter_count && !parameters)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
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

jfx_result_t jfx_software_backend_execute(jfx_software_backend_t *backend,
    const char *source, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba) {
    if (!backend || !source || !input_rgba || !output_rgba || !pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diagnostic = { .size = sizeof(diagnostic) };
    jolt_status_t status = jolt_compile(source, &program, &diagnostic);
    if (status != JOLT_OK) {
        return map_status(status);
    }
    size_t bytecode_size = 0;
    const uint8_t *bytecode = jolt_program_data(program, &bytecode_size);
    jfx_result_t result = jfx_software_backend_execute_bytecode(backend, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
    jolt_program_destroy(program);
    return result;
}

/* ---- Shared CPU-path adapter -------------------------------------------- */

struct jfx_software_adapter {
    jfx_software_backend_t *pipeline;
    char name[16];
};

jfx_result_t jfx_software_adapter_create(const char *name, size_t memory_limit,
    jfx_software_adapter_t **out_backend) {
    if (!name || !out_backend || !name[0]) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_backend = NULL;
    jfx_software_backend_t *pipeline = NULL;
    jfx_result_t status = jfx_software_backend_create(name, memory_limit, &pipeline);
    if (status != JFX_SUCCESS) {
        return status;
    }
    jfx_software_adapter_t *backend = software_alloc(sizeof(*backend));
    if (!backend) {
        jfx_software_backend_destroy(pipeline);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    backend->pipeline = pipeline;
    snprintf(backend->name, sizeof(backend->name), "%s", name);
    *out_backend = backend;
    return JFX_SUCCESS;
}

void jfx_software_adapter_destroy(jfx_software_adapter_t *backend) {
    if (!backend) {
        return;
    }
    jfx_software_backend_destroy(backend->pipeline);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), backend);
}

void jfx_software_adapter_query_caps(const jfx_software_adapter_t *backend,
    jfx_backend_caps_t *out_caps) {
    if (!out_caps) {
        return;
    }
    memset(out_caps, 0, sizeof(*out_caps));
    jfx_software_backend_query_caps(backend ? backend->pipeline : NULL,
        &out_caps->gpu_available, NULL, &out_caps->api_version,
        out_caps->device_name, sizeof(out_caps->device_name));
    out_caps->used_gpu = jfx_software_adapter_used_gpu(backend);
}

bool jfx_software_adapter_used_gpu(const jfx_software_adapter_t *backend) {
    return backend && jfx_software_backend_used_gpu(backend->pipeline);
}

jfx_result_t jfx_software_adapter_execute(jfx_software_adapter_t *backend,
    const char *source, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba) {
    if (!backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_software_backend_execute(backend->pipeline, source, input_rgba, pixels,
        parameters, parameter_count, output_rgba);
}

jfx_result_t jfx_software_adapter_execute_bytecode(jfx_software_adapter_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    if (!backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_software_backend_execute_bytecode(backend->pipeline, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}
