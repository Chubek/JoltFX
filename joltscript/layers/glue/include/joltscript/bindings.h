#ifndef JOLTSCRIPT_BINDINGS_H
#define JOLTSCRIPT_BINDINGS_H
#include "joltscript/vm.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JOLT_GLUE_ABI_MAJOR 0
#define JOLT_GLUE_ABI_MINOR 3
#define JOLT_GLUE_ABI_PATCH 0
#define JOLT_CAP_COMPUTE UINT64_C(1)
typedef struct jolt_registry jolt_registry_t;
typedef jolt_status_t (*jolt_binding_fn)(const float *, size_t, float *, void *);
typedef struct {
    size_t size;
    const char *symbol;
    size_t argument_count;
    uint64_t required_capabilities;
    jolt_binding_fn function;
    void *userdata;
} jolt_binding_desc_t;
/* Register during setup, then freeze before sharing between threads. Registry
 * owns symbol copies; userdata remains borrowed until registry destruction. */
jolt_registry_t *jolt_registry_create(void);
void jolt_registry_destroy(jolt_registry_t *registry);
jolt_status_t jolt_registry_register(jolt_registry_t *, const jolt_binding_desc_t *);
jolt_status_t jolt_registry_freeze(jolt_registry_t *);
jolt_status_t jolt_registry_call(const jolt_registry_t *, const char *symbol,
    uint64_t capabilities, const float *arguments, size_t count, float *out_result);
/* Compatibility entry point initializes the process-wide core registry. */
void jolt_register_core_bindings(void);
const jolt_registry_t *jolt_core_bindings(void);
#ifdef __cplusplus
}
#endif
#endif
