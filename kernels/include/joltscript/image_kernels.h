#ifndef JOLT_IMAGE_KERNELS_H
#define JOLT_IMAGE_KERNELS_H
#include "joltscript/image_program.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Frame-aware catalog; separate from the legacy single-pixel effects API. */
typedef struct jolt_image_kernels jolt_image_kernels_t;
jolt_image_kernels_t *jolt_image_kernels_create(jolt_diagnostic_t *diagnostic);
void jolt_image_kernels_destroy(jolt_image_kernels_t *kernels);
size_t jolt_image_kernels_count(void);
const char *jolt_image_kernels_name(size_t index);
/* Compile only the requested bundled source. Caller owns the immutable program. */
jolt_status_t jolt_image_kernel_compile(const char *name, jolt_image_program_t **out,
    jolt_diagnostic_t *diagnostic);
size_t jolt_image_kernels_parameter_count(const jolt_image_kernels_t *, const char *name);
const jolt_image_parameter_info_t *jolt_image_kernels_parameter_info(
    const jolt_image_kernels_t *, const char *name, size_t index);
/* Runs the bundled Joltscript source with the image_program contract. */
jolt_status_t jolt_image_kernels_apply(const jolt_image_kernels_t *kernels,
    const char *name, const float *src, size_t width, size_t height,
    const jolt_image_parameter_t *parameters, size_t parameter_count,
    const float *data, size_t data_count, size_t memory_limit,
    size_t step_limit, float *dst);
#ifdef __cplusplus
}
#endif
#endif
