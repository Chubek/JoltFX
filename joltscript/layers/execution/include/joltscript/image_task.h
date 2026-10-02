#ifndef JOLT_IMAGE_TASK_H
#define JOLT_IMAGE_TASK_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous CPU image runner. The callback receives borrowed buffers and the
 * remaining scratch budget. Only successful finite output is published. This
 * permits Glue/library kernels without an upward dependency on the compiler. */
typedef jolt_status_t (*jolt_image_task_fn)(void *user, const float *src,
    size_t width, size_t height, size_t scratch_limit, float *out);
jolt_status_t jolt_image_task_run(jolt_image_task_fn task, void *user,
    const float *src, size_t width, size_t height, size_t memory_limit, float *out);
#ifdef __cplusplus
}
#endif
#endif
