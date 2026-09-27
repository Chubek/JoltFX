#include "jfx/jfx_engine.h"
#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "tilly/logger.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fallback when no .jolt path is supplied: identity passthrough in MVP syntax. */
static const char kFallbackSource[] =
    "(defkernel passthrough [r g b a] (rgba r g b a))";

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long tell = ftell(fp);
    if (tell < 0 || tell > 1048576) {
        fclose(fp);
        return NULL;
    }
    size_t len = (size_t)tell;
    rewind(fp);
    char *buf = (char *)malloc(len + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, len, fp) != len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    return buf;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    char *owned = NULL;
    const char *source = kFallbackSource;
    if (path != NULL) {
        owned = read_file(path);
        if (owned == NULL) {
            fprintf(stderr, "hello_world: cannot read '%s'\n", path);
            return 1;
        }
        source = owned;
    }
    printf("Hello from JoltFX!\n");

    jfx_engine_config_t config;
    memset(&config, 0, sizeof(config));
    config.max_buffers = 256;
    config.max_textures = 64;
    config.max_kernels = 32;
    config.backend_name = NULL;

    jfx_engine_t *engine = NULL;
    if (jfx_engine_init(&config, &engine) != JFX_SUCCESS) {
        fprintf(stderr, "hello_world: engine init failed\n");
        free(owned);
        return 1;
    }
    printf("Engine initialized (backend %s)\n", jfx_engine_backend_name(engine));

    jolt_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    diag.size = sizeof(diag);
    jolt_program_t *program = NULL;
    if (jolt_compile(source, &program, &diag) != JOLT_OK || program == NULL) {
        fprintf(stderr, "hello_world: compile failed: %s\n",
                diag.message[0] != '\0' ? diag.message : "unknown error");
        free(owned);
        jfx_engine_shutdown(engine);
        return 1;
    }
    size_t code_size = 0;
    const uint8_t *code = jolt_program_data(program, &code_size);
    printf("Kernel compiled: %zu bytes of JBC1\n", code_size);

    /* 2x2 premultiplied RGBA gradient. */
    float input[16] = {
        0.0f, 0.0f, 0.5f, 1.0f,  //
        1.0f, 0.0f, 0.5f, 1.0f,  //
        0.0f, 1.0f, 0.5f, 1.0f,  //
        1.0f, 1.0f, 0.5f, 1.0f,  //
    };
    float output[16];
    memset(output, 0, sizeof(output));

    jolt_pipeline_t *pipeline = jolt_pipeline_create(64 * 1024 * 1024);
    if (pipeline == NULL) {
        fprintf(stderr, "hello_world: pipeline create failed\n");
        jolt_program_destroy(program);
        free(owned);
        jfx_engine_shutdown(engine);
        return 1;
    }
    size_t stage = 0;
    jolt_status_t status =
        jolt_pipeline_add(pipeline, code, code_size, -1, NULL, 0, &stage);
    if (status != JOLT_OK) {
        fprintf(stderr, "hello_world: pipeline add failed (%d)\n", (int)status);
        jolt_pipeline_destroy(pipeline);
        jolt_program_destroy(program);
        free(owned);
        jfx_engine_shutdown(engine);
        return 1;
    }
    status = jolt_pipeline_run(pipeline, input, 4, stage, output);
    if (status != JOLT_OK) {
        fprintf(stderr, "hello_world: pipeline run failed (%d)\n", (int)status);
        jolt_pipeline_destroy(pipeline);
        jolt_program_destroy(program);
        free(owned);
        jfx_engine_shutdown(engine);
        return 1;
    }

    int ok = 1;
    for (size_t i = 0; i < 16; i++) {
        printf("pixel %zu: in=%.2f out=%.2f\n", i / 4, (double)input[i],
               (double)output[i]);
        if (fabsf(output[i] - input[i]) > 1e-6f) {
            ok = 0;
        }
    }
    printf(ok ? "Passthrough verified: output matches input\n"
              : "MISMATCH: output differs from input\n");

    jolt_pipeline_destroy(pipeline);
    jolt_program_destroy(program);
    free(owned);

    if (jfx_engine_tick(engine) != JFX_SUCCESS) {
        fprintf(stderr, "hello_world: engine tick failed\n");
        jfx_engine_shutdown(engine);
        return 1;
    }
    jfx_engine_shutdown(engine);
    return ok ? 0 : 1;
}
