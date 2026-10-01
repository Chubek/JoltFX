#ifndef JFX_RESULT_H
#define JFX_RESULT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes shared by the engine and by every backend.
 *
 * This header is deliberately a leaf: it has no dependencies beyond <stddef.h>
 * so the backend HAL can return jfx_result_t without depending on the engine
 * that owns it. */
typedef enum {
    JFX_SUCCESS = 0,
    JFX_ERROR_INVALID_ARGUMENT = -1,
    JFX_ERROR_OUT_OF_MEMORY = -2,
    JFX_ERROR_NOT_INITIALIZED = -3,
    JFX_ERROR_BACKEND_FAILURE = -4,
    JFX_ERROR_NOT_FOUND = -5,
    JFX_ERROR_ALREADY_EXISTS = -6,
    JFX_ERROR_VERSION_MISMATCH = -7,
    JFX_ERROR_PLUGIN_FAILURE = -8,
    /* The operation exists in the contract but this implementation does not
     * provide it. Distinct from NOT_FOUND, which means "not present in the
     * data" rather than "not implemented here". */
    JFX_ERROR_NOT_IMPLEMENTED = -9
} jfx_result_t;

const char *jfx_result_to_string(jfx_result_t result);

#ifdef __cplusplus
}
#endif

#endif // JFX_RESULT_H
