#ifndef JOLT_COMPILER_H
#define JOLT_COMPILER_H
#include "joltscript/vm.h"
#include "joltscript/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jolt_program jolt_program_t;
typedef struct {
    size_t size;
    size_t line, column;
    char message[160];
} jolt_diagnostic_t;
/* Joltscript compiler (execution ABI 0.9): legacy f32 JBC1 version 1 and typed
 * version 2 with exact primitives, loops, functions/closures and six built-in
 * library modules. Sources are NUL terminated, at most 1 MiB; programs own
 * generated storage. Built-in module interfaces compile with zero outputs.
 * See joltscript/docs/language.md and stdlib.md for typed semantics/budgets.
 *
 * Legacy version-1 profile: (defkernel name [input ...] expression), or a
 * final (rgba r g b a). Scalar f32 arithmetic, comparison, logical, and bitwise
 * operations, let/var/set!, lazy conditionals and nonempty do sequences. Scalar kernel bodies
 * are implicit sequences. Maximum 32 inputs, 128 active locals, depth 64.
 * Sources are NUL terminated and
 * limited to 1 MiB. Programs own all generated storage.
 *
 * Supported operations:
 *   Arithmetic: + - * / min max abs floor pow sqrt
 *   Comparison: < > <= >= = !=
 *   Logical:    and or not
 *   Bitwise:    bitwise-and bitwise-or bitwise-xor shl shr
 *   Control:    select (eager), (if condition then else),
 *              (cond [condition result] ... [else result]),
 *              (when condition forms...), (unless condition forms...)
 *   Bindings:   (let name value), (var name value), (set! name value)
 *   Scopes:     (do forms...), (let [name value ...] forms...)
 *              (var [name value ...] forms...)
 * Each form returns one f32; declarations and assignments return their value.
 * Initialization is eager, once, left-to-right. Inner scopes can shadow names;
 * duplicates in one scope and mutation of immutable/input bindings are errors.
 * if evaluates only its selected arm. cond evaluates tests until the first
 * nonzero one, then its result; else must be last. A nonmatching cond or skipped
 * when/unless returns 0. Tests and arms have private declaration scopes.
 */
jolt_status_t jolt_compile(const char *source, jolt_program_t **out_program, jolt_diagnostic_t *diagnostic);
void jolt_program_destroy(jolt_program_t *program);
const uint8_t *jolt_program_data(const jolt_program_t *program, size_t *out_size);
jolt_status_t jolt_program_run(jolt_vm_t *vm, const jolt_program_t *program,
    const float *inputs, size_t input_count, float *outputs, size_t output_count);
/* Typed programs use JBC1 container version 2. Primitive suffixes/annotations,
 * string/char/bool literals, imports or loop/function forms select this profile.
 * The legacy float API accepts only floating inputs; use values for exact ints,
 * bools, Unicode chars, UTF-8 strings and immutable collections. Output views
 * follow value.h lifetimes. Typed execution is CPU-based, including Vulkan fallback. */
jolt_status_t jolt_program_run_values(jolt_vm_t *vm, const jolt_program_t *program,
    const jolt_value_t *inputs, size_t input_count, jolt_value_t *outputs,
    size_t output_count, const jolt_run_options_t *options);

/* Formats `path:line:column: message` into out, truncating to out_size and
 * always NUL-terminating. Safe with a NULL diagnostic or an empty message,
 * in which case it reports a generic compilation failure. */
void jolt_diagnostic_format(const jolt_diagnostic_t *diagnostic, const char *path,
    char *out, size_t out_size);
#ifdef __cplusplus
}
#endif
#endif
