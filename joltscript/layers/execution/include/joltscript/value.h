#ifndef JOLT_VALUE_H
#define JOLT_VALUE_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Typed execution ABI 0.9. Integer payloads retain all 64 bits. Strings and
 * collections are immutable views. Output views live until the next successful typed run
 * on the same VM, or VM destruction; caller-owned inputs are borrowed. */
typedef enum {
    JOLT_TYPE_VOID, JOLT_TYPE_I32, JOLT_TYPE_I64, JOLT_TYPE_U8,
    JOLT_TYPE_U16, JOLT_TYPE_U32, JOLT_TYPE_U64, JOLT_TYPE_F32,
    JOLT_TYPE_F64, JOLT_TYPE_BOOL, JOLT_TYPE_CHAR, JOLT_TYPE_STRING,
    JOLT_TYPE_LIST, JOLT_TYPE_VECTOR, JOLT_TYPE_MAP, JOLT_TYPE_SET,
    /* Signature-only dynamic data type; runtime values carry a concrete tag. */
    JOLT_TYPE_VALUE = 22
} jolt_type_t;
typedef struct jolt_value {
    jolt_type_t type;
    union {
        int64_t integer;
        uint64_t unsigned_integer;
        double number;
        struct { const char *data; size_t size; } string;
        /* Immutable items; maps contain alternating key/value entries. */
        struct { const struct jolt_value *items; size_t size; } collection;
    } as;
} jolt_value_t;
typedef struct {
    size_t size;
    uint64_t steps; /* 0 selects 100000; maximum 10000000. */
} jolt_run_options_t;
#define JOLT_TYPED_BYTECODE_VERSION 2u
#define JOLT_MAX_PROGRAM_BYTES (2u * 1024u * 1024u)
jolt_status_t jolt_vm_run_values(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const jolt_value_t *inputs, size_t input_count, jolt_value_t *outputs,
    size_t output_count, const jolt_run_options_t *options);
/* Query the exact typed signature without executing a program. */
jolt_status_t jolt_bytecode_signature(const uint8_t *code, size_t size,
    jolt_type_t *inputs, size_t input_capacity, jolt_type_t *outputs, size_t output_capacity);
#ifdef __cplusplus
}
#endif
#endif
