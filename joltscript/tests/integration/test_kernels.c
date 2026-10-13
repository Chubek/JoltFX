/* test_kernels.c — Integration tests for bundled Joltscript kernels.
 *
 * Phase 4 coverage for real kernel sources: compile selected shipped kernels,
 * execute them through the VM, and compose them through the pipeline runner.
 */

#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "joltscript/vm.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *relative_path;
    const float inputs[JOLT_MAX_INPUTS];
    size_t input_count;
    const float expected[4];
} kernel_case_t;

static char *read_text(const char *root, const char *relative_path)
{
    char path[1024];
    int written = snprintf(path, sizeof(path), "%s/%s", root, relative_path);
    assert(written > 0 && (size_t)written < sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && length < 1024 * 1024);
    rewind(file);
    size_t bytes = (size_t)length;
    char *text = malloc(bytes + 1u);
    assert(text);
    assert(fread(text, 1, bytes, file) == bytes);
    assert(fclose(file) == 0);
    text[bytes] = '\0';
    return text;
}

static jolt_program_t *compile_kernel_file(const char *root, const char *relative_path)
{
    char *source = read_text(root, relative_path);
    jolt_diagnostic_t diagnostic = {.size = sizeof(diagnostic)};
    jolt_program_t *program = NULL;
    jolt_status_t status = jolt_compile(source, &program, &diagnostic);
    if (status != JOLT_OK) {
        char formatted[256];
        jolt_diagnostic_format(&diagnostic, relative_path, formatted, sizeof(formatted));
        fprintf(stderr, "%s\n", formatted);
    }
    free(source);
    assert(status == JOLT_OK);
    assert(program);
    return program;
}

static void assert_rgba_close(const float *actual, const float *expected)
{
    for (size_t i = 0; i < 4; ++i) {
        assert(isfinite(actual[i]));
        assert(fabsf(actual[i] - expected[i]) < 1e-5f);
    }
}

static void test_shipped_color_kernels(const char *kernel_root)
{
    const kernel_case_t cases[] = {
        {"color/brightness.jolt", {0.2f, 0.3f, 0.4f, 0.8f, 2.0f}, 5, {0.4f, 0.6f, 0.8f, 0.8f}},
        {"color/contrast.jolt", {0.2f, 0.4f, 0.6f, 1.0f, 2.0f}, 5, {0.0f, 0.3f, 0.7f, 1.0f}},
        {"color/opacity.jolt", {0.2f, 0.4f, 0.6f, 0.8f, 0.25f}, 5, {0.05f, 0.1f, 0.15f, 0.2f}},
        {"color/threshold.jolt", {0.1f, 0.1f, 0.1f, 1.0f, 0.5f}, 5, {0.0f, 0.0f, 0.0f, 1.0f}},
        {"color/grayscale.jolt", {0.2f, 0.4f, 0.6f, 1.0f}, 4, {0.37192f, 0.37192f, 0.37192f, 1.0f}},
        {"color/invert.jolt", {0.2f, 0.4f, 0.6f, 1.0f}, 4, {0.8f, 0.6f, 0.4f, 1.0f}},
    };
    jolt_vm_t *vm = jolt_vm_create();
    assert(vm);
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        jolt_program_t *program = compile_kernel_file(kernel_root, cases[i].relative_path);
        float out[4] = {0};
        assert(jolt_program_run(vm, program, cases[i].inputs, cases[i].input_count, out, 4) == JOLT_OK);
        assert_rgba_close(out, cases[i].expected);
        jolt_program_destroy(program);
    }
    jolt_vm_destroy(vm);
    puts("test_shipped_color_kernels: PASSED");
}

static void test_color_kernel_pipeline(const char *kernel_root)
{
    jolt_program_t *brightness = compile_kernel_file(kernel_root, "color/brightness.jolt");
    jolt_program_t *opacity = compile_kernel_file(kernel_root, "color/opacity.jolt");
    size_t brightness_size = 0, opacity_size = 0;
    const uint8_t *brightness_code = jolt_program_data(brightness, &brightness_size);
    const uint8_t *opacity_code = jolt_program_data(opacity, &opacity_size);
    jolt_pipeline_t *pipeline = jolt_pipeline_create(4096);
    assert(pipeline);
    float brightness_amount = 2.0f;
    float opacity_amount = 0.5f;
    size_t stage_brightness = 0, stage_opacity = 0;
    assert(jolt_pipeline_add(pipeline, brightness_code, brightness_size, -1,
                             &brightness_amount, 1, &stage_brightness) == JOLT_OK);
    assert(jolt_pipeline_add(pipeline, opacity_code, opacity_size, (int)stage_brightness,
                             &opacity_amount, 1, &stage_opacity) == JOLT_OK);
    const float input[] = {
        0.1f, 0.2f, 0.3f, 1.0f,
        0.6f, 0.7f, 0.8f, 1.0f,
    };
    float output[8] = {0};
    assert(jolt_pipeline_run(pipeline, input, 2, stage_opacity, output) == JOLT_OK);
    const float expected[] = {
        0.1f, 0.2f, 0.3f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.5f,
    };
    for (size_t i = 0; i < 8; ++i) assert(fabsf(output[i] - expected[i]) < 1e-5f);
    jolt_pipeline_destroy(pipeline);
    jolt_program_destroy(brightness);
    jolt_program_destroy(opacity);
    puts("test_color_kernel_pipeline: PASSED");
}

int main(int argc, char **argv)
{
    const char *kernel_root = argc > 1 ? argv[1] : "kernels";
    test_shipped_color_kernels(kernel_root);
    test_color_kernel_pipeline(kernel_root);
    puts("All bundled-kernel integration tests passed!");
    return 0;
}
