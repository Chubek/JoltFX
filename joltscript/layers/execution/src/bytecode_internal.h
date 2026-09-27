#ifndef JOLT_BYTECODE_INTERNAL_H
#define JOLT_BYTECODE_INTERNAL_H
#include "joltscript/vm.h"
#include <string.h>
static inline uint32_t jolt_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void jolt_write_u32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static inline float jolt_read_float(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
/* For immutable bytecode owned by a pipeline after jolt_pipeline_add validated it. */
jolt_status_t jolt_vm_run_prevalidated(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const float *inputs, size_t input_count, float *outputs, size_t output_count);
#endif
