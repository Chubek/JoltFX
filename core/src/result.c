#include "jfx/jfx_result.h"

const char *jfx_result_to_string(jfx_result_t result) {
    switch (result) {
    case JFX_SUCCESS: return "JFX_SUCCESS";
    case JFX_ERROR_INVALID_ARGUMENT: return "JFX_ERROR_INVALID_ARGUMENT";
    case JFX_ERROR_OUT_OF_MEMORY: return "JFX_ERROR_OUT_OF_MEMORY";
    case JFX_ERROR_NOT_INITIALIZED: return "JFX_ERROR_NOT_INITIALIZED";
    case JFX_ERROR_BACKEND_FAILURE: return "JFX_ERROR_BACKEND_FAILURE";
    case JFX_ERROR_NOT_FOUND: return "JFX_ERROR_NOT_FOUND";
    case JFX_ERROR_ALREADY_EXISTS: return "JFX_ERROR_ALREADY_EXISTS";
    case JFX_ERROR_VERSION_MISMATCH: return "JFX_ERROR_VERSION_MISMATCH";
    case JFX_ERROR_PLUGIN_FAILURE: return "JFX_ERROR_PLUGIN_FAILURE";
    case JFX_ERROR_NOT_IMPLEMENTED: return "JFX_ERROR_NOT_IMPLEMENTED";
    default: return "UNKNOWN_ERROR";
    }
}
