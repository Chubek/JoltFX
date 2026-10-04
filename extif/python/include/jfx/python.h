#ifndef JFX_PYTHON_H
#define JFX_PYTHON_H
#include "jfx/script_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
jfx_script_status_t jfx_python_script_create(const jfx_script_desc_t *desc,
    jfx_script_runtime_t **out_runtime);
#ifdef __cplusplus
}
#endif
#endif
