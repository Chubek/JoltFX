#ifndef JFX_KERNEL_H
#define JFX_KERNEL_H

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_kernel jfx_kernel_t;

jfx_result_t jfx_kernel_load(
    jfx_engine_t *engine,
    const char *path,
    jfx_kernel_t **out_kernel
);

void jfx_kernel_destroy(jfx_kernel_t *kernel);

jfx_result_t jfx_kernel_execute(jfx_kernel_t *kernel);

#ifdef __cplusplus
}
#endif

#endif // JFX_KERNEL_H
