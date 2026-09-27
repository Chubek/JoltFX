#ifndef JOLT_EFFECTS_H
#define JOLT_EFFECTS_H
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
