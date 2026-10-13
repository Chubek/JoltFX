#ifndef JOLT_JIT_SLJIT_H
#define JOLT_JIT_SLJIT_H

#include "joltscript/vm.h"

typedef struct jolt_sljit_cache jolt_sljit_cache_t;

int jolt_sljit_available(void);
jolt_sljit_cache_t *jolt_sljit_cache_create(void);
void jolt_sljit_cache_destroy(jolt_sljit_cache_t *cache);
jolt_status_t jolt_sljit_cache_run(jolt_sljit_cache_t *cache,
    const uint8_t *code, size_t size, const float *inputs, size_t input_count,
    float *outputs, size_t output_count);

#endif
