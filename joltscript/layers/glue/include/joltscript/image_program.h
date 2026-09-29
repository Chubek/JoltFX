#ifndef JOLT_IMAGE_PROGRAM_H
#define JOLT_IMAGE_PROGRAM_H
#include "joltscript/compiler.h"
#ifdef __cplusplus
extern "C" {
#endif
/* CPU image-source interpreter ABI, independently versioned from JBC1.
 * Programs are immutable and reusable concurrently; calls own their scratch. */
#define JOLT_IMAGE_ABI_MAJOR 0
#define JOLT_IMAGE_ABI_MINOR 1
#define JOLT_IMAGE_ABI_PATCH 0
typedef struct jolt_image_program jolt_image_program_t;
typedef struct { const char *name; double value; } jolt_image_parameter_t;
typedef struct {
    const char *name;
    double default_value, minimum, maximum;
    int integer;
} jolt_image_parameter_info_t;
/* library and source are bounded, NUL-terminated Joltscript. No filesystem,
 * FFI, imports, allocation, or system resources are accessible to programs. */
jolt_status_t jolt_image_compile(const char *library, const char *source,
    jolt_image_program_t **out, jolt_diagnostic_t *diagnostic);
void jolt_image_program_destroy(jolt_image_program_t *program);
size_t jolt_image_parameter_count(const jolt_image_program_t *program);
const jolt_image_parameter_info_t *jolt_image_parameter_info(
    const jolt_image_program_t *program, size_t index);
/* Same-size, tightly packed, premultiplied float RGBA. Named scalar uniforms;
 * vector/matrix fields are flattened (see kernels/README.md). Values clamp to
 * their source-declared ranges; integer uniforms reject fractional values.
 * data is an optional read-only float array for curves/LUTs. All buffers must
 * have the declared lengths. Output is untouched on error; aliasing is safe.
 * memory_limit bounds frame scratch, step_limit bounds AST evaluations across
 * the entire call. Two temporary frames are required, including for one pass.
 * CPU only: these programs are not JBC1 and cannot be sent to a GPU backend. */
jolt_status_t jolt_image_program_run(const jolt_image_program_t *program,
    const float *src, size_t width, size_t height,
    const jolt_image_parameter_t *parameters, size_t parameter_count,
    const float *data, size_t data_count, size_t memory_limit,
    size_t step_limit, float *dst);
#ifdef __cplusplus
}
#endif
#endif
