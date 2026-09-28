/* test_conformance.c — Conformance tests for Joltscript
 *
 * Tests that the compiler and VM produce consistent results
 * across different execution paths.
 */

#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include "joltscript/pipeline.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static void test_compiler_vm_consistency(void) {
    /* Test that compiler output matches VM execution */
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel test [x y] (+ (* x 2.0) (* y 3.0)))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {1.0f, 2.0f};
    float out[1] = {0};
    assert(jolt_vm_run(vm, code, size, in, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 8.0f) < 1e-6f);
    jolt_vm_destroy(vm);
    jolt_program_destroy(p);
    printf("test_compiler_vm_consistency: PASSED\n");
}

static void test_pipeline_vm_consistency(void) {
    /* Test that pipeline execution matches single VM execution */
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel gain [r g b a gain] (rgba (* r gain) (* g gain) (* b gain) a))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);

    /* Single VM execution */
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {0.1f, 0.2f, 0.3f, 1.0f, 2.0f};
    float out[4] = {0};
    assert(jolt_program_run(vm, p, in, 5, out, 4) == JOLT_OK);
    jolt_vm_destroy(vm);

    /* Pipeline execution */
    jolt_pipeline_t *pipe = jolt_pipeline_create(4096);
    assert(pipe);
    float gain = 2.0f;
    float input[] = {0.1f, 0.2f, 0.3f, 1.0f};
    float pipe_out[4] = {0};
    size_t id;
    assert(jolt_pipeline_add(pipe, code, size, -1, &gain, 1, &id) == JOLT_OK);
    assert(jolt_pipeline_run(pipe, input, 1, 0, pipe_out) == JOLT_OK);

    /* Results should match */
    assert(fabsf(out[0] - pipe_out[0]) < 1e-6f);
    assert(fabsf(out[1] - pipe_out[1]) < 1e-6f);
    assert(fabsf(out[2] - pipe_out[2]) < 1e-6f);
    assert(fabsf(out[3] - pipe_out[3]) < 1e-6f);

    jolt_pipeline_destroy(pipe);
    jolt_program_destroy(p);
    printf("test_pipeline_vm_consistency: PASSED\n");
}

static void test_deterministic_compilation(void) {
    /* Test that compilation is deterministic */
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p1 = NULL, *p2 = NULL;
    const char *src = "(defkernel test [x y] (+ (* x 2.0) (* y 3.0)))";
    assert(jolt_compile(src, &p1, &d) == JOLT_OK);
    assert(jolt_compile(src, &p2, &d) == JOLT_OK);
    size_t size1 = 0, size2 = 0;
    const uint8_t *code1 = jolt_program_data(p1, &size1);
    const uint8_t *code2 = jolt_program_data(p2, &size2);
    assert(size1 == size2);
    assert(memcmp(code1, code2, size1) == 0);
    jolt_program_destroy(p1);
    jolt_program_destroy(p2);
    printf("test_deterministic_compilation: PASSED\n");
}

static void test_all_operations(void) {
    /* Test all supported operations */
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    jolt_vm_t *vm = jolt_vm_create();
    float out[1] = {0};
    float in2[] = {10.0f, 3.0f};

    /* Arithmetic */
    assert(jolt_compile("(defkernel t [x y] (+ x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 13.0f) < 1e-6f);
    jolt_program_destroy(p);

    assert(jolt_compile("(defkernel t [x y] (- x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 7.0f) < 1e-6f);
    jolt_program_destroy(p);

    assert(jolt_compile("(defkernel t [x y] (* x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 30.0f) < 1e-6f);
    jolt_program_destroy(p);

    assert(jolt_compile("(defkernel t [x y] (/ x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(fabsf(out[0] - 3.333333f) < 1e-5f);
    jolt_program_destroy(p);

    /* Comparison */
    assert(jolt_compile("(defkernel t [x y] (< x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 0.0f);
    jolt_program_destroy(p);

    assert(jolt_compile("(defkernel t [x y] (> x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    /* Logical */
    assert(jolt_compile("(defkernel t [x y] (and x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 1.0f);
    jolt_program_destroy(p);

    /* Bitwise */
    assert(jolt_compile("(defkernel t [x y] (bitwise-and x y))", &p, &d) == JOLT_OK);
    assert(jolt_program_run(vm, p, in2, 2, out, 1) == JOLT_OK);
    assert(out[0] == 2.0f);  /* 10 & 3 = 2 */
    jolt_program_destroy(p);

    jolt_vm_destroy(vm);
    printf("test_all_operations: PASSED\n");
}

int main(void) {
    test_compiler_vm_consistency();
    test_pipeline_vm_consistency();
    test_deterministic_compilation();
    test_all_operations();
    printf("\nAll conformance tests passed!\n");
    return 0;
}
