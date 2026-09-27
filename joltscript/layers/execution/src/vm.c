#include "bytecode_internal.h"
#include "tilly/containers.h"
#include <math.h>
struct jolt_vm { float stack[JOLT_MAX_STACK]; };
jolt_vm_t *jolt_vm_create(void) { return tilly_container_calloc(1, sizeof(jolt_vm_t)); }
void jolt_vm_destroy(jolt_vm_t *vm) { tilly_container_free(vm); }
jolt_status_t jolt_vm_run(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const float *inputs, size_t input_count, float *outputs, size_t output_count) {
    if (!vm || !code || !outputs || (input_count && !inputs)) return JOLT_ERR_ARGUMENT;
    jolt_status_t status = jolt_bytecode_validate(code, size);
    if (status != JOLT_OK) return status;
    if (input_count != jolt_read_u32(code + 8) || output_count != jolt_read_u32(code + 12))
        return JOLT_ERR_ARGUMENT;
    for (size_t i = 0; i < input_count; ++i) if (!isfinite(inputs[i])) return JOLT_ERR_NUMERIC;
    float result[JOLT_MAX_OUTPUTS]; unsigned n = 0;
    for (size_t pos = 16; pos < size; pos += 8) {
        uint32_t op = jolt_read_u32(code + pos), arg = jolt_read_u32(code + pos + 4);
        float a, b;
        switch (op) {
        case JOLT_OP_CONST: vm->stack[n++] = jolt_read_float(arg); break;
        case JOLT_OP_INPUT: vm->stack[n++] = inputs[arg]; break;
        case JOLT_OP_OUTPUT: result[arg] = vm->stack[--n]; break;
        case JOLT_OP_ABS: vm->stack[n-1] = fabsf(vm->stack[n-1]); break;
        case JOLT_OP_FLOOR: vm->stack[n-1] = floorf(vm->stack[n-1]); break;
        case JOLT_OP_SQRT: vm->stack[n-1] = sqrtf(vm->stack[n-1]); break;
        case JOLT_OP_SELECT:
            b = vm->stack[--n]; a = vm->stack[--n];
            vm->stack[n-1] = vm->stack[n-1] != 0.f ? a : b; break;
        default:
            b = vm->stack[--n]; a = vm->stack[n-1];
            switch (op) {
            case JOLT_OP_ADD: a += b; break;
            case JOLT_OP_SUB: a -= b; break;
            case JOLT_OP_MUL: a *= b; break;
            case JOLT_OP_DIV: if (b == 0.f) return JOLT_ERR_NUMERIC; a /= b; break;
            case JOLT_OP_MIN: a = fminf(a, b); break;
            case JOLT_OP_MAX: a = fmaxf(a, b); break;
            case JOLT_OP_POW: a = powf(a, b); break;
            case JOLT_OP_LT: a = a < b ? 1.f : 0.f; break;
            default: return JOLT_ERR_BYTECODE;
            }
            vm->stack[n-1] = a;
        }
        if (n && !isfinite(vm->stack[n-1])) return JOLT_ERR_NUMERIC;
    }
    memcpy(outputs, result, output_count * sizeof(float));
    return JOLT_OK;
}
int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *code, size_t size) {
    if (!vm || !code) return JOLT_ERR_ARGUMENT;
    jolt_status_t s = jolt_bytecode_validate(code, size);
    if (s != JOLT_OK) return s;
    float inputs[JOLT_MAX_INPUTS] = {0}, outputs[JOLT_MAX_OUTPUTS];
    return jolt_vm_run(vm, code, size, inputs, jolt_read_u32(code + 8), outputs, jolt_read_u32(code + 12));
}
