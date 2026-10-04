#ifndef JFX_FFI_BRIDGE_H
#define JFX_FFI_BRIDGE_H
#include "jfx/script_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Uniform embedding surface. Engine clients use this table or the matching
 * jfx_script_runtime_* functions; interpreter APIs stay inside extif adapters. */
typedef struct {
    size_t size;
    jfx_script_status_t (*init)(jfx_script_language_t, const jfx_script_desc_t *, jfx_script_runtime_t **);
    void (*shutdown)(jfx_script_runtime_t *);
    jfx_script_status_t (*load_script)(jfx_script_runtime_t *, const void *, size_t, const char *);
    jfx_script_status_t (*call_function)(jfx_script_runtime_t *, const char *, const jfx_value_t *, size_t, jfx_value_t *);
    jfx_script_status_t (*register_kernel)(jfx_script_runtime_t *, const char *, const char *);
    jfx_script_status_t (*invoke_kernel)(jfx_script_runtime_t *, const char *, const jfx_value_t *, size_t, jfx_value_t *);
    jfx_script_status_t (*to_native)(jfx_script_runtime_t *, jfx_script_value_t, jfx_value_t *);
    jfx_script_status_t (*from_native)(jfx_script_runtime_t *, const jfx_value_t *, jfx_script_value_t *);
    jfx_script_status_t (*register_callback)(jfx_script_runtime_t *, const char *, const char *, uint32_t *);
    void (*gc_collect)(jfx_script_runtime_t *);
    void (*gc_pause)(jfx_script_runtime_t *);
    void (*gc_resume)(jfx_script_runtime_t *);
} jfx_ffi_bridge_t;
const jfx_ffi_bridge_t *jfx_script_ffi_bridge(void);
#ifdef __cplusplus
}
#endif
#endif
