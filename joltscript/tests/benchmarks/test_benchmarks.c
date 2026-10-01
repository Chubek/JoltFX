/* test_benchmarks.c — Performance benchmarks for Joltscript
 *
 * Measures compilation and execution performance.
 */

#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include "joltscript/pipeline.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void benchmark_compile(void) {
    const char *src = "(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";
    int iterations = 10000;
    double start = get_time();
    for (int i = 0; i < iterations; ++i) {
        jolt_program_t *p = NULL;
        jolt_diagnostic_t d = {.size = sizeof(d)};
        assert(jolt_compile(src, &p, &d) == JOLT_OK);
        jolt_program_destroy(p);
    }
    double elapsed = get_time() - start;
    printf("benchmark_compile: %d iterations in %.3f seconds (%.1f/sec)\n",
           iterations, elapsed, iterations / elapsed);
}

static void benchmark_vm_execute(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {0.2f, 0.3f, 0.4f, 0.5f, 2.0f};
    float out[4] = {0};
    int iterations = 100000;
    double start = get_time();
    for (int i = 0; i < iterations; ++i) {
        assert(jolt_vm_run(vm, code, size, in, 5, out, 4) == JOLT_OK);
    }
    double elapsed = get_time() - start;
    printf("benchmark_vm_execute: %d iterations in %.3f seconds (%.1f/sec)\n",
           iterations, elapsed, iterations / elapsed);
    jolt_vm_destroy(vm);
    jolt_program_destroy(p);
}

static void benchmark_pipeline(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel gain [r g b a gain] (rgba (* r gain) (* g gain) (* b gain) a))", &p, &d) == JOLT_OK);
    size_t size = 0;
    const uint8_t *code = jolt_program_data(p, &size);
    jolt_pipeline_t *pipe = jolt_pipeline_create(1024 * 1024);
    assert(pipe);
    float gain = 2.0f;
    float input[] = {0.1f, 0.2f, 0.3f, 1.0f};
    float out[4] = {0};
    size_t id;
    assert(jolt_pipeline_add(pipe, code, size, -1, &gain, 1, &id) == JOLT_OK);
    int iterations = 10000;
    double start = get_time();
    for (int i = 0; i < iterations; ++i) {
        assert(jolt_pipeline_run(pipe, input, 1, 0, out) == JOLT_OK);
    }
    double elapsed = get_time() - start;
    printf("benchmark_pipeline: %d iterations in %.3f seconds (%.1f/sec)\n",
           iterations, elapsed, iterations / elapsed);
    jolt_pipeline_destroy(pipe);
    jolt_program_destroy(p);
}

static void benchmark_complex_kernel(void) {
    jolt_diagnostic_t d = {.size = sizeof(d)};
    jolt_program_t *p = NULL;
    assert(jolt_compile("(defkernel complex [r g b a t] (rgba (select (> r t) r 0.0) (select (> g t) g 0.0) (select (> b t) b 0.0) a))", &p, &d) == JOLT_OK);
    jolt_vm_t *vm = jolt_vm_create();
    float in[] = {0.6f, 0.4f, 0.8f, 1.0f, 0.5f};
    float out[4] = {0};
    int iterations = 100000;
    double start = get_time();
    for (int i = 0; i < iterations; ++i) {
        assert(jolt_program_run(vm, p, in, 5, out, 4) == JOLT_OK);
    }
    double elapsed = get_time() - start;
    printf("benchmark_complex_kernel: %d iterations in %.3f seconds (%.1f/sec)\n",
           iterations, elapsed, iterations / elapsed);
    jolt_vm_destroy(vm);
    jolt_program_destroy(p);
}

int main(void) {
    printf("Joltscript Performance Benchmarks\n");
    printf("================================\n");
    benchmark_compile();
    benchmark_vm_execute();
    benchmark_pipeline();
    benchmark_complex_kernel();
    printf("\nAll benchmarks completed.\n");
    return 0;
}
