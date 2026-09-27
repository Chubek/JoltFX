/* Engine-owned kernels.
 *
 * A jfx_kernel_t owns one immutable JBC1 program. Loading compiles Joltscript
 * once; executing runs the same bytes through the owning engine's backend, so
 * a kernel registered once is dispatched on whichever path (GPU or CPU) the
 * backend resolved. */

#include "jfx/jfx_kernel.h"
#include "jfx/jfx_events.h"
#include "jfx/jfx_memory.h"
#include "joltscript/compiler.h"
#include "engine_internal.h"
#include "tilly/logger.h"
#include <stdio.h>
#include <string.h>

#define JFX_KERNEL_SOURCE_MAX_BYTES ((size_t)1u << 20)

struct jfx_kernel {
    jfx_engine_t *engine;
    uint8_t *bytecode;
    size_t bytecode_size;
};

static jfx_result_t map_jolt_status(jolt_status_t status) {
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

jfx_result_t jfx_kernel_load_source(jfx_engine_t *engine, const char *source,
    jfx_kernel_t **out_kernel) {
    if (!engine || !source || !source[0] || !out_kernel) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_kernel = NULL;
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diagnostic = { .size = sizeof(diagnostic) };
    jolt_status_t status = jolt_compile(source, &program, &diagnostic);
    if (status != JOLT_OK) {
        char message[256];
        jolt_diagnostic_format(&diagnostic, "<source>", message, sizeof(message));
        tilly_log_simple(TILLY_LOG_ERROR, "jfx_kernel_load: %s", message);
        return map_jolt_status(status);
    }
    size_t bytecode_size = 0;
    const uint8_t *bytecode = jolt_program_data(program, &bytecode_size);
    if (!bytecode || !bytecode_size) {
        jolt_program_destroy(program);
        return JFX_ERROR_BACKEND_FAILURE;
    }
    if (!engine_acquire_kernel(engine)) {
        jolt_program_destroy(program);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    jfx_kernel_t *kernel = jfx_heap_alloc(sizeof(*kernel), _Alignof(jfx_kernel_t));
    if (!kernel) {
        engine_release_kernel(engine);
        jolt_program_destroy(program);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    kernel->bytecode = jfx_heap_alloc(bytecode_size, 16u);
    if (!kernel->bytecode) {
        jfx_heap_free(kernel);
        engine_release_kernel(engine);
        jolt_program_destroy(program);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memcpy(kernel->bytecode, bytecode, bytecode_size);
    kernel->bytecode_size = bytecode_size;
    kernel->engine = engine;
    jolt_program_destroy(program);
    *out_kernel = kernel;
    return JFX_SUCCESS;
}

/* Reads a whole file into `out`, bounded by `max_bytes`. */
static jfx_result_t read_source_file(const char *path, char **out, size_t *out_size) {
    *out = NULL;
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return JFX_ERROR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    long length = ftell(file);
    if (length <= 0 || (unsigned long)length > JFX_KERNEL_SOURCE_MAX_BYTES) {
        fclose(file);
        return length == 0 ? JFX_ERROR_INVALID_ARGUMENT : JFX_ERROR_OUT_OF_MEMORY;
    }
    rewind(file);
    char *buffer = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), (size_t)length + 1u,
        _Alignof(max_align_t));
    if (!buffer) {
        fclose(file);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    size_t read = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), buffer);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    buffer[read] = '\0';
    *out = buffer;
    *out_size = read;
    return JFX_SUCCESS;
}

jfx_result_t jfx_kernel_load(jfx_engine_t *engine, const char *path, jfx_kernel_t **out_kernel) {
    if (!engine || !path || !path[0] || !out_kernel) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_kernel = NULL;
    char *source = NULL;
    size_t source_size = 0;
    jfx_result_t status = read_source_file(path, &source, &source_size);
    if (status != JFX_SUCCESS) {
        return status;
    }
    status = jfx_kernel_load_source(engine, source, out_kernel);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), source);
    (void)source_size;
    return status;
}

void jfx_kernel_destroy(jfx_kernel_t *kernel) {
    if (!kernel) {
        return;
    }
    jfx_engine_t *engine = kernel->engine;
    jfx_heap_free(kernel->bytecode);
    jfx_heap_free(kernel);
    engine_release_kernel(engine);
}

jfx_result_t jfx_kernel_execute(jfx_kernel_t *kernel, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba) {
    if (!kernel || !input_rgba || !output_rgba || !pixels ||
        (parameter_count && !parameters)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_engine_execute_bytecode(kernel->engine, kernel->bytecode,
        kernel->bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}

size_t jfx_kernel_bytecode_size(const jfx_kernel_t *kernel) {
    return kernel ? kernel->bytecode_size : 0u;
}

jfx_engine_t *jfx_kernel_engine(const jfx_kernel_t *kernel) {
    return kernel ? kernel->engine : NULL;
}
