#ifndef JOLT_EFFECTS_H
#define JOLT_EFFECTS_H
#include <stdbool.h>
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jolt_effects jolt_effects_t;

/* Compile and cache the bundled sources. Each instance is externally synchronized. */
jolt_effects_t *jolt_effects_create(void);
void jolt_effects_destroy(jolt_effects_t *);
size_t jolt_effects_count(const jolt_effects_t *);
const char *jolt_effects_name(size_t index);

/* Looks up a bundled effect. Returns false for an unknown name. */
bool jolt_effects_exists(const jolt_effects_t *, const char *name);

/* The effect's compiled JBC1 program, owned by the catalog and valid until
 * jolt_effects_destroy. Callers that want to dispatch through a backend (for
 * example jfx_engine_execute_bytecode) read it here instead of re-compiling. */
const uint8_t *jolt_effects_bytecode(const jolt_effects_t *effects, const char *name,
    size_t *out_size);

/* Uniform parameter arity and the documented [min, max] clamp applied to the
 * first parameter. A count of 0 means the effect takes no parameters. */
size_t jolt_effects_parameter_count(const jolt_effects_t *effects, const char *name);
void jolt_effects_parameter_range(const jolt_effects_t *effects, const char *name,
    float *out_min, float *out_max);
float jolt_effects_parameter_default(const jolt_effects_t *effects, const char *name);

/* RGBA premultiplied floats. NULL/0 selects defaults. Supplied parameters are
 * clamped to documented ranges. Output is unchanged on error; in-place is safe.
 * memory_limit bounds temporary image storage. */
jolt_status_t jolt_effects_apply(jolt_effects_t *, const char *name,
    const float *input, size_t pixels, const float *parameters, size_t parameter_count,
    size_t memory_limit, float *output);
#ifdef __cplusplus
}
#endif
#endif
