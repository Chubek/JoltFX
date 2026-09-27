#include "commands.h"

#include "jfx/jfx_engine.h"
#include "jfx/vk_backend.h"
#include "joltscript/compiler.h"
#include "joltscript/effects.h"
#include "tilly/logger.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOLTFX_CLI_VERSION "0.2.0"
/* Matches the compiler's 1 MiB source cap (see compiler.h). */
#define CLI_MAX_SOURCE ((size_t)1024 * 1024)
#define CLI_DEFAULT_MEMORY_LIMIT ((size_t)64 * 1024 * 1024)

void print_usage(void) {
    printf("joltfx %s - JoltFX effects engine CLI\n\n", JOLTFX_CLI_VERSION);
    printf("Usage: joltfx <command> [options]\n\n");
    printf("Commands:\n");
    printf("  compile FILE [-o OUTPUT]   Validate a .jolt kernel, optionally write bytecode\n");
    printf("  verify FILE                Validate a .jolt kernel without writing output\n");
    printf("  effects                    List bundled kernels\n");
    printf("  info EFFECT|FILE           Show effect or kernel details\n");
    printf("  render [options]           Render an effect to a PPM image\n");
    printf("  version                    Show version information\n");
    printf("  help [COMMAND]             Show help\n");
    printf("\nRun 'joltfx help COMMAND' for command-specific options.\n");
}

void print_command_help(const char *command) {
    if (command == NULL || strcmp(command, "compile") == 0) {
        printf("Usage: joltfx compile FILE [-o OUTPUT]\n\n");
        printf("Validates a Joltscript kernel (MVP defkernel form) and, with -o,\n");
        printf("writes the compiled JBC1 bytecode to OUTPUT.\n");
    }
    if (command == NULL || strcmp(command, "verify") == 0) {
        printf("Usage: joltfx verify FILE\n\n");
        printf("Validates a Joltscript kernel. Exits 0 when valid, 1 otherwise.\n");
    }
    if (command == NULL || strcmp(command, "effects") == 0) {
        printf("Usage: joltfx effects\n\n");
        printf("Lists the bundled effect kernels (12 color effects).\n");
    }
    if (command == NULL || strcmp(command, "info") == 0) {
        printf("Usage: joltfx info EFFECT|FILE\n\n");
        printf("Shows details for a bundled effect name or a .jolt source file.\n");
    }
    if (command == NULL || strcmp(command, "render") == 0) {
        printf("Usage: joltfx render [--effect NAME] [--param VALUE]\n");
        printf("                     [--width W --height H] [-o OUTPUT.ppm]\n");
        printf("                     [--backend NAME]\n\n");
        printf("Renders a bundled effect over a gradient to a binary PPM (P6).\n");
        printf("Defaults: --effect brightness --width 64 --height 64 -o render.ppm\n");
        printf("Backend NAME is validated by the Core engine (vulkan/auto).\n");
    }
}

/* Reads a whole file, NUL-terminated. Returns NULL on error. */
static char *read_source(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long tell = ftell(fp);
    if (tell < 0 || (size_t)tell > CLI_MAX_SOURCE) {
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
    size_t got = fread(buf, 1, len, fp);
    fclose(fp);
    if (got != len) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    if (out_len != NULL) {
        *out_len = len;
    }
    return buf;
}

/* Best-effort kernel name extraction for info output. */
static void extract_kernel_name(const char *source, char *out, size_t out_size) {
    const char *at = strstr(source, "defkernel");
    if (at == NULL || out_size == 0) {
        snprintf(out, out_size, "(unknown)");
        return;
    }
    at += strlen("defkernel");
    while (*at != '\0' && isspace((unsigned char)*at)) {
        at++;
    }
    size_t i = 0;
    while (*at != '\0' && !isspace((unsigned char)*at) && *at != '[' && *at != '(' &&
           *at != '\n' && i + 1 < out_size) {
        out[i++] = *at++;
    }
    out[i] = '\0';
    if (i == 0) {
        snprintf(out, out_size, "(unknown)");
    }
}

static int compile_source(const char *path, const char *source, jolt_program_t **out) {
    jolt_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    diag.size = sizeof(diag);
    jolt_program_t *program = NULL;
    jolt_status_t status = jolt_compile(source, &program, &diag);
    if (status != JOLT_OK || program == NULL) {
        if (diag.message[0] != '\0') {
            fprintf(stderr, "error: %s:%zu:%zu: %s\n", path, diag.line, diag.column,
                    diag.message);
        } else {
            fprintf(stderr, "error: %s: compilation failed (status %d)\n", path,
                    (int)status);
        }
        if (program != NULL) {
            jolt_program_destroy(program);
        }
        return 1;
    }
    *out = program;
    return 0;
}

int cmd_compile(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires a value\n\n", argv[i]);
                print_command_help("compile");
                return 1;
            }
            output = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("compile");
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
            print_command_help("compile");
            return 1;
        } else if (input == NULL) {
            input = argv[i];
        } else {
            fprintf(stderr, "error: expected one FILE, got several\n\n");
            print_command_help("compile");
            return 1;
        }
    }
    if (input == NULL) {
        fprintf(stderr, "error: missing FILE\n\n");
        print_command_help("compile");
        return 1;
    }
    char *source = read_source(input, NULL);
    if (source == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", input);
        return 1;
    }
    jolt_program_t *program = NULL;
    int rc = compile_source(input, source, &program);
    free(source);
    if (rc != 0) {
        return 1;
    }
    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    if (data == NULL) {
        fprintf(stderr, "error: %s: compiled program has no bytecode\n", input);
        jolt_program_destroy(program);
        return 1;
    }
    if (output != NULL) {
        FILE *fp = fopen(output, "wb");
        if (fp == NULL) {
            fprintf(stderr, "error: cannot write '%s'\n", output);
            jolt_program_destroy(program);
            return 1;
        }
        size_t wrote = fwrite(data, 1, size, fp);
        fclose(fp);
        if (wrote != size) {
            fprintf(stderr, "error: short write to '%s'\n", output);
            jolt_program_destroy(program);
            return 1;
        }
        printf("ok: %s: %zu bytes -> %s\n", input, size, output);
    } else {
        printf("ok: %s: %zu bytes\n", input, size);
    }
    jolt_program_destroy(program);
    return 0;
}

int cmd_verify(int argc, char **argv) {
    if (argc == 1 && (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0)) {
        print_command_help("verify");
        return 0;
    }
    if (argc != 1) {
        fprintf(stderr, "error: verify takes exactly one FILE\n\n");
        print_command_help("verify");
        return 1;
    }
    const char *input = argv[0];
    char *source = read_source(input, NULL);
    if (source == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", input);
        return 1;
    }
    jolt_program_t *program = NULL;
    int rc = compile_source(input, source, &program);
    free(source);
    if (rc != 0) {
        return 1;
    }
    size_t size = 0;
    (void)jolt_program_data(program, &size);
    printf("ok: %s: %zu bytes\n", input, size);
    jolt_program_destroy(program);
    return 0;
}

int cmd_effects(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("effects");
            return 0;
        }
        fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
        print_command_help("effects");
        return 1;
    }
    jolt_effects_t *effects = jolt_effects_create();
    if (effects == NULL) {
        fprintf(stderr, "error: failed to load bundled effects\n");
        return 1;
    }
    size_t count = jolt_effects_count(effects);
    for (size_t i = 0; i < count; i++) {
        printf("%s\n", jolt_effects_name(i));
    }
    jolt_effects_destroy(effects);
    return 0;
}

static int info_for_file(const char *path) {
    char *source = read_source(path, NULL);
    if (source == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        return 1;
    }
    jolt_program_t *program = NULL;
    int rc = compile_source(path, source, &program);
    if (rc != 0) {
        free(source);
        return 1;
    }
    size_t size = 0;
    (void)jolt_program_data(program, &size);
    char name[64];
    extract_kernel_name(source, name, sizeof(name));
    printf("file: %s\nkernel: %s\nbytecode: %zu bytes\n", path, name, size);
    free(source);
    jolt_program_destroy(program);
    return 0;
}

static int info_for_effect(const char *name) {
    jolt_effects_t *effects = jolt_effects_create();
    if (effects == NULL) {
        fprintf(stderr, "error: failed to load bundled effects\n");
        return 1;
    }
    float input[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    float output[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    jolt_status_t status = jolt_effects_apply(effects, name, input, 1, NULL, 0,
                                              CLI_DEFAULT_MEMORY_LIMIT, output);
    if (status != JOLT_OK) {
        fprintf(stderr, "error: unknown effect '%s'\n", name);
        jolt_effects_destroy(effects);
        return 1;
    }
    printf("effect: %s\n", name);
    printf("sample: in=(%.3f %.3f %.3f %.3f) out=(%.3f %.3f %.3f %.3f)\n",
           (double)input[0], (double)input[1], (double)input[2], (double)input[3],
           (double)output[0], (double)output[1], (double)output[2],
           (double)output[3]);
    jolt_effects_destroy(effects);
    return 0;
}

int cmd_info(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "error: info takes exactly one EFFECT or FILE\n\n");
        print_command_help("info");
        return 1;
    }
    if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
        print_command_help("info");
        return 0;
    }
    FILE *probe = fopen(argv[0], "rb");
    if (probe != NULL) {
        fclose(probe);
        return info_for_file(argv[0]);
    }
    return info_for_effect(argv[0]);
}

static float clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

int cmd_render(int argc, char **argv) {
    const char *effect = "brightness";
    const char *output = "render.ppm";
    const char *backend = NULL;
    float param_value = 0.0f;
    int have_param = 0;
    long width = 64;
    long height = 64;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--effect") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --effect requires a value\n");
                return 1;
            }
            effect = argv[i];
        } else if (strcmp(argv[i], "--param") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --param requires a value\n");
                return 1;
            }
            char *end = NULL;
            double v = strtod(argv[i], &end);
            if (end == argv[i] || *end != '\0') {
                fprintf(stderr, "error: invalid --param '%s'\n", argv[i]);
                return 1;
            }
            param_value = (float)v;
            have_param = 1;
        } else if (strcmp(argv[i], "--width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --width requires a value\n");
                return 1;
            }
            width = strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "--height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --height requires a value\n");
                return 1;
            }
            height = strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: %s requires a value\n", argv[i - 1]);
                return 1;
            }
            output = argv[i];
        } else if (strcmp(argv[i], "--backend") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --backend requires a value\n");
                return 1;
            }
            backend = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("render");
            return 0;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
            print_command_help("render");
            return 1;
        }
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        fprintf(stderr, "error: width/height must be in [1, 4096]\n");
        return 1;
    }

    jfx_engine_config_t config;
    memset(&config, 0, sizeof(config));
    config.max_buffers = 256;
    config.max_textures = 64;
    config.max_kernels = 32;
    config.backend_name = backend;
    jfx_engine_t *engine = NULL;
    jfx_result_t init = jfx_engine_init(&config, &engine);
    if (init != JFX_SUCCESS || engine == NULL) {
        fprintf(stderr, "error: engine init failed (%d)\n", (int)init);
        return 1;
    }

    size_t pixels = (size_t)width * (size_t)height;
    float *input = (float *)malloc(pixels * 4 * sizeof(float));
    float *out = (float *)malloc(pixels * 4 * sizeof(float));
    if (input == NULL || out == NULL) {
        fprintf(stderr, "error: out of memory\n");
        free(input);
        free(out);
        jfx_engine_shutdown(engine);
        return 1;
    }
    for (long y = 0; y < height; y++) {
        for (long x = 0; x < width; x++) {
            size_t idx = ((size_t)y * (size_t)width + (size_t)x) * 4;
            float fx = width > 1 ? (float)x / (float)(width - 1) : 0.0f;
            float fy = height > 1 ? (float)y / (float)(height - 1) : 0.0f;
            input[idx] = fx;
            input[idx + 1] = fy;
            input[idx + 2] = 0.5f;
            input[idx + 3] = 1.0f;
        }
    }

    jolt_effects_t *effects = jolt_effects_create();
    if (effects == NULL) {
        fprintf(stderr, "error: failed to load bundled effects\n");
        free(input);
        free(out);
        jfx_engine_shutdown(engine);
        return 1;
    }
    const float *params = have_param ? &param_value : NULL;
    size_t param_count = have_param ? 1 : 0;
    jolt_status_t status = jolt_effects_apply(effects, effect, input, pixels, params,
                                              param_count, CLI_DEFAULT_MEMORY_LIMIT, out);
    jolt_effects_destroy(effects);
    if (status != JOLT_OK) {
        fprintf(stderr, "error: render failed for effect '%s' (status %d)\n", effect,
                (int)status);
        free(input);
        free(out);
        jfx_engine_shutdown(engine);
        return 1;
    }
    free(input);

    if (jfx_engine_tick(engine) != JFX_SUCCESS) {
        fprintf(stderr, "error: engine tick failed\n");
        free(out);
        jfx_engine_shutdown(engine);
        return 1;
    }
    const char *backend_name = jfx_engine_backend_name(engine);

    FILE *fp = fopen(output, "wb");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot write '%s'\n", output);
        free(out);
        jfx_engine_shutdown(engine);
        return 1;
    }
    fprintf(fp, "P6\n%ld %ld\n255\n", width, height);
    for (size_t i = 0; i < pixels; i++) {
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(clamp01(out[i * 4]) * 255.0f + 0.5f);
        rgb[1] = (unsigned char)(clamp01(out[i * 4 + 1]) * 255.0f + 0.5f);
        rgb[2] = (unsigned char)(clamp01(out[i * 4 + 2]) * 255.0f + 0.5f);
        if (fwrite(rgb, 1, 3, fp) != 3) {
            fprintf(stderr, "error: short write to '%s'\n", output);
            fclose(fp);
            free(out);
            jfx_engine_shutdown(engine);
            return 1;
        }
    }
    fclose(fp);
    free(out);
    printf("rendered %ldx%ld %s -> %s (backend %s)\n", width, height, effect, output,
           backend_name != NULL ? backend_name : "unknown");
    tilly_log_simple(TILLY_LOG_INFO, "CLI render complete");
    jfx_engine_shutdown(engine);
    return 0;
}
