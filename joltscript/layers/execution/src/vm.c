#include "bytecode_internal.h"
#include "typed_internal.h"
#if defined(JFX_JOLTSCRIPT_SLJIT)
#include "jit_sljit.h"
#endif
#include "tilly/containers.h"
#include <math.h>
struct jolt_vm {
    float stack[JOLT_MAX_STACK];
    float locals[JOLT_MAX_LOCALS];
    char *strings[4];
    jolt_vm_backend_t backend;
#if defined(JFX_JOLTSCRIPT_SLJIT)
    jolt_sljit_cache_t *sljit;
#endif
};
jolt_vm_t *jolt_vm_create(void) {
    jolt_vm_t *vm = tilly_container_calloc(1, sizeof(jolt_vm_t));
    if (vm) vm->backend = JOLT_VM_BACKEND_AUTO;
    return vm;
}
void jolt_vm_destroy(jolt_vm_t *vm) {
    if(vm) for(unsigned i=0;i<4;++i) tilly_container_free(vm->strings[i]);
#if defined(JFX_JOLTSCRIPT_SLJIT)
    if(vm) jolt_sljit_cache_destroy(vm->sljit);
#endif
    tilly_container_free(vm);
}
int jolt_vm_backend_available(jolt_vm_backend_t backend) {
    switch (backend) {
    case JOLT_VM_BACKEND_AUTO:
    case JOLT_VM_BACKEND_INTERPRETER:
        return 1;
    case JOLT_VM_BACKEND_SLJIT:
#if defined(JFX_JOLTSCRIPT_SLJIT)
        return jolt_sljit_available();
#else
        return 0;
#endif
    default:
        return 0;
    }
}
const char *jolt_vm_backend_name(jolt_vm_backend_t backend) {
    switch (backend) {
    case JOLT_VM_BACKEND_AUTO: return "auto";
    case JOLT_VM_BACKEND_INTERPRETER: return "interpreter";
    case JOLT_VM_BACKEND_SLJIT: return "sljit";
    default: return "unknown";
    }
}
jolt_status_t jolt_vm_set_backend(jolt_vm_t *vm, jolt_vm_backend_t backend) {
    if (!vm) return JOLT_ERR_ARGUMENT;
    if (!jolt_vm_backend_available(backend)) return JOLT_ERR_CAPABILITY;
    vm->backend = backend;
    return JOLT_OK;
}
jolt_vm_backend_t jolt_vm_get_backend(const jolt_vm_t *vm) {
    return vm ? vm->backend : JOLT_VM_BACKEND_INTERPRETER;
}
static jolt_status_t jolt_vm_interpret_prevalidated(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const float *inputs, size_t input_count, float *outputs, size_t output_count) {
    for (size_t i = 0; i < input_count; ++i) if (!isfinite(inputs[i])) return JOLT_ERR_NUMERIC;
    float result[JOLT_MAX_OUTPUTS]; unsigned n = 0;
    for (size_t pos = 16; pos < size; pos += 8) {
        uint32_t op = jolt_read_u32(code + pos), arg = jolt_read_u32(code + pos + 4);
        float a, b;
        switch (op) {
        case JOLT_OP_JUMP: pos = arg - 8u; break;
        case JOLT_OP_BRANCH_IF_ZERO:
            if (vm->stack[--n] == 0.f) pos = arg - 8u;
            break;
        case JOLT_OP_CONST: vm->stack[n++] = jolt_read_float(arg); break;
        case JOLT_OP_INPUT: vm->stack[n++] = inputs[arg]; break;
        case JOLT_OP_LOAD_LOCAL: vm->stack[n++] = vm->locals[arg]; break;
        case JOLT_OP_STORE_LOCAL: vm->locals[arg] = vm->stack[n-1]; break;
        case JOLT_OP_DROP: --n; break;
        case JOLT_OP_OUTPUT: result[arg] = vm->stack[--n]; break;
        case JOLT_OP_ABS: vm->stack[n-1] = fabsf(vm->stack[n-1]); break;
        case JOLT_OP_FLOOR: vm->stack[n-1] = floorf(vm->stack[n-1]); break;
        case JOLT_OP_SQRT: vm->stack[n-1] = sqrtf(vm->stack[n-1]); break;
        case JOLT_OP_NOT: vm->stack[n-1] = vm->stack[n-1] == 0.f ? 1.f : 0.f; break;
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
            case JOLT_OP_GT: a = a > b ? 1.f : 0.f; break;
            case JOLT_OP_LE: a = a <= b ? 1.f : 0.f; break;
            case JOLT_OP_GE: a = a >= b ? 1.f : 0.f; break;
            case JOLT_OP_EQ: a = a == b ? 1.f : 0.f; break;
            case JOLT_OP_NE: a = a != b ? 1.f : 0.f; break;
            case JOLT_OP_AND: a = (a != 0.f && b != 0.f) ? 1.f : 0.f; break;
            case JOLT_OP_OR: a = (a != 0.f || b != 0.f) ? 1.f : 0.f; break;
            case JOLT_OP_BITWISE_AND: a = (float)((uint32_t)a & (uint32_t)b); break;
            case JOLT_OP_BITWISE_OR: a = (float)((uint32_t)a | (uint32_t)b); break;
            case JOLT_OP_BITWISE_XOR: a = (float)((uint32_t)a ^ (uint32_t)b); break;
            case JOLT_OP_SHL: a = (float)((uint32_t)a << ((uint32_t)b & 31)); break;
            case JOLT_OP_SHR: a = (float)((uint32_t)a >> ((uint32_t)b & 31)); break;
            default: return JOLT_ERR_BYTECODE;
            }
            vm->stack[n-1] = a;
        }
        if (n && !isfinite(vm->stack[n-1])) return JOLT_ERR_NUMERIC;
    }
    memcpy(outputs, result, output_count * sizeof(float));
    return JOLT_OK;
}
jolt_status_t jolt_vm_run_prevalidated(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const float *inputs, size_t input_count, float *outputs, size_t output_count) {
    if (!vm || !code || !outputs || (input_count && !inputs)) return JOLT_ERR_ARGUMENT;
    if(size>=16 && jolt_read_u32(code+4)==2) return jt_run_float(code,size,inputs,input_count,outputs,output_count);
    for (size_t i = 0; i < input_count; ++i) if (!isfinite(inputs[i])) return JOLT_ERR_NUMERIC;
#if defined(JFX_JOLTSCRIPT_SLJIT)
    if (vm->backend != JOLT_VM_BACKEND_INTERPRETER) {
        if (!vm->sljit) vm->sljit = jolt_sljit_cache_create();
        jolt_status_t jit_status = vm->sljit ?
            jolt_sljit_cache_run(vm->sljit, code, size, inputs, input_count, outputs, output_count) :
            JOLT_ERR_MEMORY;
        if (vm->backend == JOLT_VM_BACKEND_SLJIT ||
            (jit_status != JOLT_ERR_MEMORY && jit_status != JOLT_ERR_CAPABILITY))
            return jit_status;
    }
#else
    if (vm->backend == JOLT_VM_BACKEND_SLJIT) return JOLT_ERR_CAPABILITY;
#endif
    return jolt_vm_interpret_prevalidated(vm, code, size, inputs, input_count, outputs, output_count);
}
jolt_status_t jolt_vm_run(jolt_vm_t *vm, const uint8_t *code, size_t size,
    const float *inputs, size_t input_count, float *outputs, size_t output_count) {
    if (!vm || !code || !outputs || (input_count && !inputs)) return JOLT_ERR_ARGUMENT;
    jolt_status_t status = jolt_bytecode_validate(code, size);
    if (status != JOLT_OK) return status;
    if (input_count != jolt_read_u32(code + 8) || output_count != jolt_read_u32(code + 12)) {
        return JOLT_ERR_ARGUMENT;
    }
    return jolt_vm_run_prevalidated(vm, code, size, inputs, input_count, outputs, output_count);
}
int jolt_vm_execute(jolt_vm_t *vm, const uint8_t *code, size_t size) {
    if (!vm || !code) return JOLT_ERR_ARGUMENT;
    jolt_status_t s = jolt_bytecode_validate(code, size);
    if (s != JOLT_OK) return s;
    float inputs[JOLT_MAX_INPUTS] = {0}, outputs[JOLT_MAX_OUTPUTS];
    return jolt_vm_run(vm, code, size, inputs, jolt_read_u32(code + 8), outputs, jolt_read_u32(code + 12));
}

jolt_status_t jolt_vm_run_values(jolt_vm_t *vm,const uint8_t *code,size_t size,
    const jolt_value_t *inputs,size_t ni,jolt_value_t *outputs,size_t no,const jolt_run_options_t *options) {
    if(!vm||!code||!outputs||(ni&&!inputs)||ni>32||no>4||!no ||
       (options&&(options->size!=sizeof(*options)||options->steps>10000000))) return JOLT_ERR_ARGUMENT;
    jolt_status_t s=jolt_bytecode_validate(code,size);if(s) return s;
    jolt_value_t results[4];
    if(jolt_read_u32(code+4)==2) s=jt_eval(code,size,inputs,ni,results,no,options?options->steps:0);
    else {
        float in[32],out[4];
        for(size_t i=0;i<ni;++i) {
            if(inputs[i].type!=JOLT_TYPE_F32 || !isfinite(inputs[i].as.number) ||
               (double)(float)inputs[i].as.number!=inputs[i].as.number) return JOLT_ERR_TYPE;
            in[i]=(float)inputs[i].as.number;
        }
        s=jolt_vm_run(vm,code,size,in,ni,out,no);
        if(!s) for(size_t i=0;i<no;++i) { results[i]=(jolt_value_t){.type=JOLT_TYPE_F32};results[i].as.number=out[i]; }
    }
    if(!s) {
        for(unsigned i=0;i<4;++i) { tilly_container_free(vm->strings[i]);vm->strings[i]=NULL; }
        for(size_t i=0;i<no;++i) if(results[i].type>=JOLT_TYPE_STRING) vm->strings[i]=(char *)results[i].as.string.data;
        memcpy(outputs,results,no*sizeof(*outputs));
    }
    return s;
}
