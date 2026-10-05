#ifndef JFX_KERNEL_H
#define JFX_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_kernel jfx_kernel_t;

/* Compiles a Joltscript source file and registers the result with `engine` as a
 * jfx_kernel_t. The kernel holds immutable JBC1 bytecode owned by the engine.
 *
 * A `.jolt` file must hold exactly one `(defkernel ...)` form with four RGBA
 * outputs. `out_diagnostic`, when non-NULL, receives a NUL-terminated
 * file:line:column message describing the first syntax or type error. */
jfx_result_t jfx_kernel_load(
    jfx_engine_t *engine,
    const char *path,
    jfx_kernel_t **out_kernel
);

/* Compiles Joltscript source held in memory. See jfx_kernel_load. */
jfx_result_t jfx_kernel_load_source(
    jfx_engine_t *engine,
    const char *source,
    jfx_kernel_t **out_kernel
);

void jfx_kernel_destroy(jfx_kernel_t *kernel);

/* Runs the kernel through the owning engine's backend over interleaved RGBA
 * float pixels. `parameters` supplies the kernel's trailing uniform arguments
 * in declaration order; pass NULL/0 to use the bytecode's declared defaults.
 *
 * Contract shared with every backend: output_rgba is left untouched when an
 * error is returned, and in-place execution is safe. */
jfx_result_t jfx_kernel_execute(jfx_kernel_t *kernel,
    const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba);

size_t jfx_kernel_bytecode_size(const jfx_kernel_t *kernel);
jfx_engine_t *jfx_kernel_engine(const jfx_kernel_t *kernel);

#ifdef __cplusplus
}
#endif

#endif // JFX_KERNEL_H
