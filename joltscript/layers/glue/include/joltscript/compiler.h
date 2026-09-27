#ifndef JOLT_COMPILER_H
#define JOLT_COMPILER_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jolt_program jolt_program_t;
typedef struct {
    size_t size;
    size_t line, column;
    char message[160];
} jolt_diagnostic_t;
/* MVP: (defkernel name [input ...] expression), or (rgba r g b a).
 * Scalar f32 expressions, maximum 32 bindings, depth 64. Sources are NUL
 * terminated and limited to 1 MiB. Programs own all generated storage. */
jolt_status_t jolt_compile(const char *source, jolt_program_t **out_program, jolt_diagnostic_t *diagnostic);
void jolt_program_destroy(jolt_program_t *program);
const uint8_t *jolt_program_data(const jolt_program_t *program, size_t *out_size);
jolt_status_t jolt_program_run(jolt_vm_t *vm, const jolt_program_t *program,
    const float *inputs, size_t input_count, float *outputs, size_t output_count);
#ifdef __cplusplus
}
#endif
#endif
