#ifndef JFX_SCRIPT_RUNTIME_H
#define JFX_SCRIPT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JFX_SCRIPT_OK = 0,
    JFX_SCRIPT_INVALID_ARGUMENT = -1,
    JFX_SCRIPT_OUT_OF_MEMORY = -2,
    JFX_SCRIPT_ERROR = -3,
    JFX_SCRIPT_NOT_FOUND = -4,
    JFX_SCRIPT_BUDGET = -5,
} jfx_script_status_t;

typedef struct {
    size_t memory_limit;
    uint64_t instruction_limit;
} jfx_script_config_t;

#ifdef __cplusplus
}
#endif

#endif
