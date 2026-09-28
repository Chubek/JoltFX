/* test_integration.c — Integration tests for Joltscript
 *
 * Tests end-to-end kernel execution through the full pipeline.
 */

#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include "joltscript/pipeline.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static void test_single_kernel(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))", &p, &d) == JOLT_OK);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {0.2f, 0.3f, 0.4f, 0.5f, 2.0f};
    float out[4] = {0};
    assert(jolt_program_run(vm, p, in, 5, out, 4) == JOLT_OK);
    assert(fabsf(out[0] - 0.4f) < 1e-6f);
    assert(fabsf(out[1] - 0.6f) < 1e-6f);
    assert(fabsf(out[2] - 0.8f) < 1e-6f);
    assert(fabsf(out[3] - 0.5f) < 1e-6f);
    jolt_program_destroy(p);
    jolt_vm_destroy(vm);
    printf("test_single_kernel: PASSED\n");
}

static void test_pipeline_two_stages(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel gain [r g b a gain] (rgba (* r gain) (* g gain) (* b gain) a))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);
    jolt_pipeline_t *pipe = jolt_pipeline_create(4096);
    assert(pipe);
    float gain = 2.0f;
    float input[] = {0.1f, 0.2f, 0.3f, 1.0f};
    float out[4] = {0};
    size_t id1, id2;
    assert(jolt_pipeline_add(pipe, code, size, -1, &gain, 1, &id1) == JOLT_OK);
    assert(jolt_pipeline_add(pipe, code, size, 0, &gain, 1, &id2) == JOLT_OK);
    assert(jolt_pipeline_run(pipe, input, 1, 1, out) == JOLT_OK);
    assert(fabsf(out[0] - 0.4f) < 1e-6f);
    assert(fabsf(out[1] - 0.8f) < 1e-6f);
    assert(fabsf(out[2] - 1.2f) < 1e-6f);
    jolt_pipeline_destroy(pipe);
    jolt_program_destroy(p);
    printf("test_pipeline_two_stages: PASSED\n");
}

static void test_pipeline_cycle_detection(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel gain [r g b a gain] (rgba (* r gain) (* g gain) (* b gain) a))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);
    jolt_pipeline_t *pipe = jolt_pipeline_create(4096);
    assert(pipe);
    float gain = 2.0f;
    float input[] = {0.1f, 0.2f, 0.3f, 1.0f};
    float out[4] = {0};
    size_t id1, id2;
    assert(jolt_pipeline_add(pipe, code, size, 1, &gain, 1, &id1) == JOLT_OK);
    assert(jolt_pipeline_add(pipe, code, size, 0, &gain, 1, &id2) == JOLT_OK);
    assert(jolt_pipeline_run(pipe, input, 1, 0, out) == JOLT_ERR_CYCLE);
    jolt_pipeline_destroy(pipe);
    jolt_program_destroy(p);
    printf("test_pipeline_cycle_detection: PASSED\n");
}

static void test_pipeline_budget(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel gain [r g b a gain] (rgba (* r gain) (* g gain) (* b gain) a))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);
    jolt_pipeline_t *pipe = jolt_pipeline_create(1);
    assert(pipe);
    float gain = 2.0f;
    float input[] = {0.1f, 0.2f, 0.3f, 1.0f};
    float out[4] = {0};
    size_t id;
    assert(jolt_pipeline_add(pipe, code, size, -1, &gain, 1, &id) == JOLT_OK);
    assert(jolt_pipeline_run(pipe, input, 1, 0, out) == JOLT_ERR_BUDGET);
    jolt_pipeline_destroy(pipe);
    jolt_program_destroy(p);
    printf("test_pipeline_budget: PASSED\n");
}

static void test_multiple_pixels(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))", &p, &d) == JOLT_OK);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {0.1f, 0.2f, 0.3f, 1.0f, 2.0f};
    float out[4] = {0};
    assert(jolt_program_run(vm, p, in, 5, out, 4) == JOLT_OK);
    assert(fabsf(out[0] - 0.2f) < 1e-6f);
    assert(fabsf(out[1] - 0.4f) < 1e-6f);
    assert(fabsf(out[2] - 0.6f) < 1e-6f);
    assert(fabsf(out[3] - 1.0f) < 1e-6f);
    jolt_program_destroy(p);
    jolt_vm_destroy(vm);
    printf("test_multiple_pixels: PASSED\n");
}

static void test_complex_kernel(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    /* A more complex kernel with multiple operations */
    assert(jolt_compile("(defkernel complex [r g b a threshold] (rgba (select (> r threshold) r 0.0) (select (> g threshold) g 0.0) (select (> b threshold) b 0.0) a))", &p, &d) == JOLT_OK);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {0.6f, 0.4f, 0.8f, 1.0f, 0.5f};
    float out[4] = {0};
    assert(jolt_program_run(vm, p, in, 5, out, 4) == JOLT_OK);
    assert(fabsf(out[0] - 0.6f) < 1e-6f);
    assert(fabsf(out[1] - 0.0f) < 1e-6f);
    assert(fabsf(out[2] - 0.8f) < 1e-6f);
    assert(fabsf(out[3] - 1.0f) < 1e-6f);
    jolt_program_destroy(p);
    jolt_vm_destroy(vm);
    printf("test_complex_kernel: PASSED\n");
}

int main(void) {
    test_single_kernel();
    test_pipeline_two_stages();
    test_pipeline_cycle_detection();
    test_pipeline_budget();
    test_multiple_pixels();
    test_complex_kernel();
    printf("\nAll integration tests passed!\n");
    return 0;
}
