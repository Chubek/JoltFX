#ifndef JOLTSCRIPT_VM_H
#define JOLTSCRIPT_VM_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JOLT_EXEC_ABI_MAJOR 0
#define JOLT_EXEC_ABI_MINOR 3
#define JOLT_MAX_INPUTS 32u
#define JOLT_MAX_OUTPUTS 4u
#define JOLT_MAX_STACK 128u
#define JOLT_MAX_INSTRUCTIONS 4096u
/* JBC1: little-endian u32 magic, version, input count, output count;
 * followed by pairs of u32 opcode/operand. Float constants are IEEE binary32.
 * No jumps: validation provides a hard execution bound.
 *
 *   offset  size  field
 *   0       4     magic
 *   4       4     version
 *   8       4     input binding count
 *   12      4     output count
 *   16      8*n   instruction pairs: u32 opcode, u32 operand
 *
 * These constants are the format's definition. zoltan/src/bytecode.rs is a
 * second implementation of the same emitter and must agree byte for byte;
 * scripts/check-bytecode-parity.sh enforces that. */
#define JOLT_BYTECODE_MAGIC UINT32_C(0x3143424a)
#define JOLT_BYTECODE_VERSION 1u
#define JOLT_BYTECODE_HEADER_SIZE 16u
#define JOLT_BYTECODE_INSTRUCTION_SIZE 8u
typedef enum {
    JOLT_OK = 0, JOLT_ERR_ARGUMENT = -1, JOLT_ERR_MEMORY = -2,
    JOLT_ERR_SYNTAX = -3, JOLT_ERR_BYTECODE = -4, JOLT_ERR_NUMERIC = -5,
    JOLT_ERR_CAPABILITY = -6, JOLT_ERR_NOT_FOUND = -7,
    JOLT_ERR_DUPLICATE = -8, JOLT_ERR_BUDGET = -9, JOLT_ERR_CYCLE = -10
} jolt_status_t;
typedef enum {
    JOLT_OP_CONST = 1, JOLT_OP_INPUT, JOLT_OP_ADD, JOLT_OP_SUB,
    JOLT_OP_MUL, JOLT_OP_DIV, JOLT_OP_MIN, JOLT_OP_MAX,
    JOLT_OP_ABS, JOLT_OP_FLOOR, JOLT_OP_POW, JOLT_OP_SQRT,
    JOLT_OP_LT, JOLT_OP_SELECT, JOLT_OP_OUTPUT,
    /* Extended operations (ABI 0.3) */
    JOLT_OP_GT = 16, JOLT_OP_LE, JOLT_OP_GE, JOLT_OP_EQ, JOLT_OP_NE,
    JOLT_OP_AND, JOLT_OP_OR, JOLT_OP_NOT,
    JOLT_OP_BITWISE_AND, JOLT_OP_BITWISE_OR, JOLT_OP_BITWISE_XOR,
    JOLT_OP_SHL, JOLT_OP_SHR
} jolt_opcode_t;
typedef struct jolt_vm jolt_vm_t;
jolt_vm_t *jolt_vm_create(void);
void jolt_vm_destroy(jolt_vm_t *vm);
/* Legacy entry executes with zero inputs; use run for explicit bindings. */
int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *bytecode, size_t size);
jolt_status_t jolt_bytecode_validate(const uint8_t *code, size_t size);
jolt_status_t jolt_vm_run(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const float *inputs, size_t input_count, float *outputs, size_t output_count);
#ifdef __cplusplus
}
#endif
#endif
