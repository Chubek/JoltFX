/* bench_compiler.c — Focused compiler benchmark smoke test.
 *
 * This benchmark is intentionally deterministic and threshold-free: CTest uses
 * it to keep the Phase 4 benchmark target building/running without making CI
 * depend on host-specific timing.
 */

#include "joltscript/compiler.h"
#include "joltscript/vm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    const char *name;
    const char *source;
    uint32_t version;
} bench_case_t;

static double monotime_seconds(void)
{
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned parse_iterations(int argc, char **argv)
{
    if (argc < 2) return 512u;
    char *end = NULL;
    unsigned long parsed = strtoul(argv[1], &end, 10);
    if (!end || *end != '\0' || parsed == 0ul) return 512u;
    if (parsed > 100000ul) parsed = 100000ul;
    return (unsigned)parsed;
}

static void compile_once(const bench_case_t *bench)
{
    jolt_diagnostic_t diagnostic = {.size = sizeof(diagnostic)};
    jolt_program_t *program = NULL;
    jolt_status_t status = jolt_compile(bench->source, &program, &diagnostic);
    if (status != JOLT_OK) {
        char formatted[256];
        jolt_diagnostic_format(&diagnostic, bench->name, formatted, sizeof(formatted));
        fprintf(stderr, "%s\n", formatted);
    }
    assert(status == JOLT_OK);
    size_t size = 0;
    const uint8_t *bytecode = jolt_program_data(program, &size);
    assert(size >= JOLT_BYTECODE_HEADER_SIZE && read_u32_le(bytecode) == JOLT_BYTECODE_MAGIC);
    assert(read_u32_le(bytecode + 4) == bench->version);
    jolt_program_destroy(program);
}

int main(int argc, char **argv)
{
    const bench_case_t benches[] = {
        {"legacy-f32",
         "(defkernel k [x y t] (rgba (+ x y) (if t x y) (cond [(< x y) y] [else x]) 1.0))",
         JOLT_BYTECODE_VERSION},
        {"typed-core",
         "(defkernel k [] (var n 0) (for [i 0 16] (set! n (+ n i))) n)",
         JOLT_TYPED_BYTECODE_VERSION},
        {"typed-stdlib",
         "(import jolt.math (sin HALF-PI)) (defkernel k [] (sin HALF-PI))",
         JOLT_TYPED_BYTECODE_VERSION},
    };
    const unsigned iterations = parse_iterations(argc, argv);
    for (size_t i = 0; i < sizeof(benches) / sizeof(*benches); ++i) compile_once(&benches[i]);

    const double start = monotime_seconds();
    for (unsigned i = 0; i < iterations; ++i) {
        for (size_t j = 0; j < sizeof(benches) / sizeof(*benches); ++j) compile_once(&benches[j]);
    }
    const double elapsed = monotime_seconds() - start;
    const double total = (double)iterations * (double)(sizeof(benches) / sizeof(*benches));
    printf("bench_compiler: %.0f compiles in %.3f seconds (%.1f/sec)\n",
           total, elapsed, total / elapsed);
    return 0;
}
