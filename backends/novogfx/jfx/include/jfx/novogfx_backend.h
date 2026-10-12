#ifndef JFX_NOVOGFX_BACKEND_H
#define JFX_NOVOGFX_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/backend_interface.h"
#include "jfx/jfx_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_novogfx_backend jfx_novogfx_backend_t;
typedef jfx_backend_config_t jfx_novogfx_backend_config_t;
typedef jfx_backend_caps_t jfx_novogfx_caps_t;

const char *jfx_novogfx_backend_name(void);
const jfx_backend_ops_t *jfx_novogfx_backend_ops(void);
jfx_result_t jfx_novogfx_backend_create(const jfx_novogfx_backend_config_t *config,
    jfx_novogfx_backend_t **out_backend);
void jfx_novogfx_backend_destroy(jfx_novogfx_backend_t *backend);
void jfx_novogfx_query_caps(const jfx_novogfx_backend_t *backend,
    jfx_novogfx_caps_t *out_caps);
bool jfx_novogfx_used_gpu(const jfx_novogfx_backend_t *backend);
jfx_result_t jfx_novogfx_execute(jfx_novogfx_backend_t *backend, const char *source,
    const float *input_rgba, size_t pixels, const float *parameters,
    size_t parameter_count, float *output_rgba);
jfx_result_t jfx_novogfx_execute_bytecode(jfx_novogfx_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba);

#ifdef __cplusplus
}
#endif

#endif
