/* Bundled effect catalog.
 *
 * Compiles the color effect sources embedded at configure time (see
 * kernels/CMakeLists.txt) once and keeps them addressable by name. Callers can
 * either run an effect directly with jolt_effects_apply, or take its bytecode
 * with jolt_effects_bytecode and dispatch it through a backend such as the
 * Core engine.
 *
 * The catalog is a fixed, immutable list built once at startup, so a linear
 * scan over the static parameter table is both the simplest and the fastest
 * lookup available: a hash map would add a dependency and indirection to save
 * nothing at this size. */

#include "joltscript/effects.h"

#include <stdbool.h>
#include <string.h>

#include "joltscript/compiler.h"
#include "joltscript/pipeline.h"
#include "tilly/containers.h"
#include <math.h>
#include "effect_sources.h"

static const struct {
    const char *name;
    size_t count;
    float default_value;
    float min;
    float max;
    const char *source;
} catalog[] = {
#include "effect_catalog.inc"
};

#define CATALOG_SIZE (sizeof(catalog) / sizeof(catalog[0]))

struct jolt_effects {
    /* Parallel to `catalog`: programs[i] is the compiled form of catalog[i]. */
    jolt_program_t *programs[CATALOG_SIZE];
};

/* Index of `name` in the catalog, or CATALOG_SIZE when absent. */
static size_t find(const char *name) {
    if (!name) {
        return CATALOG_SIZE;
    }
    for (size_t i = 0; i < CATALOG_SIZE; ++i) {
        if (strcmp(catalog[i].name, name) == 0) {
            return i;
        }
    }
    return CATALOG_SIZE;
}

jolt_effects_t *jolt_effects_create(void) {
    jolt_effects_t *effects = tilly_container_calloc(1, sizeof(*effects));
    if (!effects) {
        return NULL;
    }
    for (size_t i = 0; i < CATALOG_SIZE; ++i) {
        if (jolt_compile(catalog[i].source, &effects->programs[i], NULL) != JOLT_OK) {
            jolt_effects_destroy(effects);
            return NULL;
        }
    }
    return effects;
}

void jolt_effects_destroy(jolt_effects_t *effects) {
    if (!effects) {
        return;
    }
    for (size_t i = 0; i < CATALOG_SIZE; ++i) {
        jolt_program_destroy(effects->programs[i]);
        effects->programs[i] = NULL;
    }
    tilly_container_free(effects);
}

size_t jolt_effects_count(const jolt_effects_t *effects) {
    return effects ? CATALOG_SIZE : 0u;
}

const char *jolt_effects_name(size_t index) {
    return index < CATALOG_SIZE ? catalog[index].name : NULL;
}

bool jolt_effects_exists(const jolt_effects_t *effects, const char *name) {
    return effects && find(name) != CATALOG_SIZE;
}

const uint8_t *jolt_effects_bytecode(const jolt_effects_t *effects, const char *name,
    size_t *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    const size_t index = find(name);
    if (!effects || index == CATALOG_SIZE || !effects->programs[index]) {
        return NULL;
    }
    return jolt_program_data(effects->programs[index], out_size);
}

size_t jolt_effects_parameter_count(const jolt_effects_t *effects, const char *name) {
    (void)effects;
    const size_t index = find(name);
    return index == CATALOG_SIZE ? 0u : catalog[index].count;
}

void jolt_effects_parameter_range(const jolt_effects_t *effects, const char *name,
    float *out_min, float *out_max) {
    (void)effects;
    const size_t index = find(name);
    if (out_min) {
        *out_min = index == CATALOG_SIZE ? 0.0f : catalog[index].min;
    }
    if (out_max) {
        *out_max = index == CATALOG_SIZE ? 0.0f : catalog[index].max;
    }
}

float jolt_effects_parameter_default(const jolt_effects_t *effects, const char *name) {
    (void)effects;
    const size_t index = find(name);
    return index == CATALOG_SIZE ? 0.0f : catalog[index].default_value;
}

jolt_status_t jolt_effects_apply(jolt_effects_t *effects, const char *name,
    const float *input, size_t pixels, const float *parameters, size_t parameter_count,
    size_t memory_limit, float *output) {
    if (!effects || !input || !output || !pixels || (parameter_count && !parameters)) {
        return JOLT_ERR_ARGUMENT;
    }
    const size_t index = find(name);
    if (index == CATALOG_SIZE) {
        return JOLT_ERR_NOT_FOUND;
    }
    if (parameter_count && parameter_count != catalog[index].count) {
        return JOLT_ERR_ARGUMENT;
    }
    float value = parameter_count ? parameters[0] : catalog[index].default_value;
    if (!isfinite(value)) {
        return JOLT_ERR_NUMERIC;
    }
    value = fminf(catalog[index].max, fmaxf(catalog[index].min, value));

    size_t bytecode_size = 0;
    const uint8_t *bytecode = jolt_effects_bytecode(effects, name, &bytecode_size);
    if (!bytecode) {
        return JOLT_ERR_NOT_FOUND;
    }
    jolt_pipeline_t *pipeline = jolt_pipeline_create(memory_limit);
    if (!pipeline) {
        return JOLT_ERR_MEMORY;
    }
    size_t stage = 0;
    jolt_status_t status = jolt_pipeline_add(pipeline, bytecode, bytecode_size, -1, &value,
        catalog[index].count, &stage);
    if (status == JOLT_OK) {
        status = jolt_pipeline_run(pipeline, input, pixels, stage, output);
    }
    jolt_pipeline_destroy(pipeline);
    return status;
}
