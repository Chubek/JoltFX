#include "jit_sljit.h"
#include "bytecode_internal.h"
#include "tilly/containers.h"

#include <math.h>
#include <string.h>

#include <sljitLir.h>

#define JOLT_SLJIT_CACHE_LIMIT 8u
#define JOLT_SLJIT_FAILURE_TARGET SIZE_MAX

typedef struct {
    const float *inputs;
    float result[JOLT_MAX_OUTPUTS];
    float stack[JOLT_MAX_STACK];
    float locals[JOLT_MAX_LOCALS];
    uint32_t sp;
} jolt_sljit_context_t;

typedef sljit_sw (SLJIT_FUNC *jolt_sljit_entry_t)(jolt_sljit_context_t *ctx);

typedef struct {
    uint8_t *bytecode;
    size_t size;
    uint64_t hash;
    unsigned last_used;
    void *code;
} jolt_sljit_program_t;

_Static_assert(sizeof(jolt_sljit_entry_t) == sizeof(void *),
    "SLJIT C-callable code pointer must fit a C function pointer");

static jolt_sljit_entry_t jolt_sljit_entry_from_code(void *code)
{
    jolt_sljit_entry_t entry;
    memcpy(&entry, &code, sizeof(entry));
    return entry;
}

struct jolt_sljit_cache {
    jolt_sljit_program_t programs[JOLT_SLJIT_CACHE_LIMIT];
    unsigned tick;
};

typedef struct {
    struct sljit_jump *jump;
    size_t target;
} jolt_sljit_jump_t;

static uint64_t jolt_sljit_hash(const uint8_t *code, size_t size)
{
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; ++i) {
        h ^= (uint64_t)code[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static void jolt_sljit_program_destroy(jolt_sljit_program_t *program)
{
    if (!program) return;
    if (program->code) sljit_free_code(program->code, NULL);
    tilly_container_free(program->bytecode);
    memset(program, 0, sizeof(*program));
}

static sljit_sw SLJIT_FUNC jolt_sljit_push_const(jolt_sljit_context_t *ctx, sljit_sw bits)
{
    ctx->stack[ctx->sp++] = jolt_read_float((uint32_t)bits);
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_push_input(jolt_sljit_context_t *ctx, sljit_sw index)
{
    ctx->stack[ctx->sp++] = ctx->inputs[(size_t)index];
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_load_local(jolt_sljit_context_t *ctx, sljit_sw index)
{
    ctx->stack[ctx->sp++] = ctx->locals[(size_t)index];
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_store_local(jolt_sljit_context_t *ctx, sljit_sw index)
{
    ctx->locals[(size_t)index] = ctx->stack[ctx->sp - 1u];
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_drop(jolt_sljit_context_t *ctx)
{
    --ctx->sp;
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_output(jolt_sljit_context_t *ctx, sljit_sw index)
{
    ctx->result[(size_t)index] = ctx->stack[--ctx->sp];
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_branch_if_zero(jolt_sljit_context_t *ctx)
{
    return ctx->stack[--ctx->sp] == 0.0f ? 1 : 0;
}

static sljit_sw SLJIT_FUNC jolt_sljit_unary(jolt_sljit_context_t *ctx, sljit_sw opcode)
{
    float a = ctx->stack[ctx->sp - 1u];
    switch ((uint32_t)opcode) {
    case JOLT_OP_ABS: a = fabsf(a); break;
    case JOLT_OP_FLOOR: a = floorf(a); break;
    case JOLT_OP_SQRT: a = sqrtf(a); break;
    case JOLT_OP_NOT: a = a == 0.0f ? 1.0f : 0.0f; break;
    default: return (sljit_sw)JOLT_ERR_BYTECODE;
    }
    if (!isfinite(a)) return (sljit_sw)JOLT_ERR_NUMERIC;
    ctx->stack[ctx->sp - 1u] = a;
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_binary(jolt_sljit_context_t *ctx, sljit_sw opcode)
{
    float b = ctx->stack[--ctx->sp];
    float a = ctx->stack[ctx->sp - 1u];
    switch ((uint32_t)opcode) {
    case JOLT_OP_ADD: a += b; break;
    case JOLT_OP_SUB: a -= b; break;
    case JOLT_OP_MUL: a *= b; break;
    case JOLT_OP_DIV:
        if (b == 0.0f) return (sljit_sw)JOLT_ERR_NUMERIC;
        a /= b;
        break;
    case JOLT_OP_MIN: a = fminf(a, b); break;
    case JOLT_OP_MAX: a = fmaxf(a, b); break;
    case JOLT_OP_POW: a = powf(a, b); break;
    case JOLT_OP_LT: a = a < b ? 1.0f : 0.0f; break;
    case JOLT_OP_GT: a = a > b ? 1.0f : 0.0f; break;
    case JOLT_OP_LE: a = a <= b ? 1.0f : 0.0f; break;
    case JOLT_OP_GE: a = a >= b ? 1.0f : 0.0f; break;
    case JOLT_OP_EQ: a = a == b ? 1.0f : 0.0f; break;
    case JOLT_OP_NE: a = a != b ? 1.0f : 0.0f; break;
    case JOLT_OP_AND: a = (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f; break;
    case JOLT_OP_OR: a = (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f; break;
    case JOLT_OP_BITWISE_AND: a = (float)((uint32_t)a & (uint32_t)b); break;
    case JOLT_OP_BITWISE_OR: a = (float)((uint32_t)a | (uint32_t)b); break;
    case JOLT_OP_BITWISE_XOR: a = (float)((uint32_t)a ^ (uint32_t)b); break;
    case JOLT_OP_SHL: a = (float)((uint32_t)a << ((uint32_t)b & 31u)); break;
    case JOLT_OP_SHR: a = (float)((uint32_t)a >> ((uint32_t)b & 31u)); break;
    default: return (sljit_sw)JOLT_ERR_BYTECODE;
    }
    if (!isfinite(a)) return (sljit_sw)JOLT_ERR_NUMERIC;
    ctx->stack[ctx->sp - 1u] = a;
    return (sljit_sw)JOLT_OK;
}

static sljit_sw SLJIT_FUNC jolt_sljit_select(jolt_sljit_context_t *ctx)
{
    float b = ctx->stack[--ctx->sp];
    float a = ctx->stack[--ctx->sp];
    ctx->stack[ctx->sp - 1u] = ctx->stack[ctx->sp - 1u] != 0.0f ? a : b;
    return (sljit_sw)JOLT_OK;
}

static void jolt_sljit_emit_call1(struct sljit_compiler *compiler, sljit_sw func)
{
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(W, P), SLJIT_IMM, func);
}

static void jolt_sljit_emit_call2(struct sljit_compiler *compiler, sljit_sw func, sljit_sw arg)
{
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, arg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, P, W), SLJIT_IMM, func);
}

static void jolt_sljit_add_failure_check(struct sljit_compiler *compiler,
    jolt_sljit_jump_t *jumps, size_t *jump_count)
{
    jumps[*jump_count].jump = sljit_emit_cmp(compiler, SLJIT_SIG_LESS,
        SLJIT_R0, 0, SLJIT_IMM, 0);
    jumps[*jump_count].target = JOLT_SLJIT_FAILURE_TARGET;
    ++*jump_count;
}

static size_t jolt_sljit_target_index(uint32_t byte_offset)
{
    return (size_t)((byte_offset - JOLT_BYTECODE_HEADER_SIZE) / JOLT_BYTECODE_INSTRUCTION_SIZE);
}

static jolt_status_t jolt_sljit_compile(const uint8_t *code, size_t size,
    void **out_code)
{
    *out_code = NULL;
    if (jolt_read_u32(code + 4) != JOLT_BYTECODE_VERSION) return JOLT_ERR_CAPABILITY;

    const size_t instruction_count = (size - JOLT_BYTECODE_HEADER_SIZE) / JOLT_BYTECODE_INSTRUCTION_SIZE;
    struct sljit_label **labels = tilly_container_calloc(instruction_count + 1u, sizeof(*labels));
    jolt_sljit_jump_t *jumps = tilly_container_calloc(instruction_count * 2u + 1u, sizeof(*jumps));
    if (!labels || !jumps) {
        tilly_container_free(labels);
        tilly_container_free(jumps);
        return JOLT_ERR_MEMORY;
    }

    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) {
        tilly_container_free(labels);
        tilly_container_free(jumps);
        return JOLT_ERR_MEMORY;
    }

    size_t jump_count = 0;
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1(W, P), 2, 1, 0);
    for (size_t i = 0; i < instruction_count; ++i) {
        const size_t pos = JOLT_BYTECODE_HEADER_SIZE + i * JOLT_BYTECODE_INSTRUCTION_SIZE;
        const uint32_t op = jolt_read_u32(code + pos);
        const uint32_t arg = jolt_read_u32(code + pos + 4u);
        labels[i] = sljit_emit_label(compiler);
        switch (op) {
        case JOLT_OP_JUMP:
            jumps[jump_count].jump = sljit_emit_jump(compiler, SLJIT_JUMP);
            jumps[jump_count].target = jolt_sljit_target_index(arg);
            ++jump_count;
            break;
        case JOLT_OP_BRANCH_IF_ZERO:
            jolt_sljit_emit_call1(compiler, SLJIT_FUNC_ADDR(jolt_sljit_branch_if_zero));
            jumps[jump_count].jump = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
                SLJIT_R0, 0, SLJIT_IMM, 0);
            jumps[jump_count].target = jolt_sljit_target_index(arg);
            ++jump_count;
            break;
        case JOLT_OP_CONST:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_push_const), (sljit_sw)arg);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_INPUT:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_push_input), (sljit_sw)arg);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_LOAD_LOCAL:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_load_local), (sljit_sw)arg);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_STORE_LOCAL:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_store_local), (sljit_sw)arg);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_DROP:
            jolt_sljit_emit_call1(compiler, SLJIT_FUNC_ADDR(jolt_sljit_drop));
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_OUTPUT:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_output), (sljit_sw)arg);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_ABS:
        case JOLT_OP_FLOOR:
        case JOLT_OP_SQRT:
        case JOLT_OP_NOT:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_unary), (sljit_sw)op);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        case JOLT_OP_SELECT:
            jolt_sljit_emit_call1(compiler, SLJIT_FUNC_ADDR(jolt_sljit_select));
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        default:
            jolt_sljit_emit_call2(compiler, SLJIT_FUNC_ADDR(jolt_sljit_binary), (sljit_sw)op);
            jolt_sljit_add_failure_check(compiler, jumps, &jump_count);
            break;
        }
    }
    labels[instruction_count] = sljit_emit_label(compiler);
    sljit_emit_return(compiler, SLJIT_MOV, SLJIT_IMM, (sljit_sw)JOLT_OK);
    struct sljit_label *failure_label = sljit_emit_label(compiler);
    sljit_emit_return(compiler, SLJIT_MOV, SLJIT_R0, 0);

    if (sljit_get_compiler_error(compiler) == SLJIT_SUCCESS && failure_label) {
        for (size_t i = 0; i < jump_count; ++i) {
            struct sljit_label *target = jumps[i].target == JOLT_SLJIT_FAILURE_TARGET ?
                failure_label : labels[jumps[i].target];
            if (!target) {
                sljit_set_compiler_memory_error(compiler);
                break;
            }
            sljit_set_label(jumps[i].jump, target);
        }
    }

    void *generated = NULL;
    if (sljit_get_compiler_error(compiler) == SLJIT_SUCCESS)
        generated = sljit_generate_code(compiler, 0, NULL);
    sljit_s32 error = sljit_get_compiler_error(compiler);
    sljit_free_compiler(compiler);
    tilly_container_free(labels);
    tilly_container_free(jumps);
    if (!generated) {
        return (error == SLJIT_ERR_ALLOC_FAILED || error == SLJIT_ERR_EX_ALLOC_FAILED) ?
            JOLT_ERR_MEMORY : JOLT_ERR_CAPABILITY;
    }
    *out_code = generated;
    return JOLT_OK;
}

int jolt_sljit_available(void)
{
    return 1;
}

jolt_sljit_cache_t *jolt_sljit_cache_create(void)
{
    return tilly_container_calloc(1, sizeof(jolt_sljit_cache_t));
}

void jolt_sljit_cache_destroy(jolt_sljit_cache_t *cache)
{
    if (!cache) return;
    for (size_t i = 0; i < JOLT_SLJIT_CACHE_LIMIT; ++i)
        jolt_sljit_program_destroy(&cache->programs[i]);
    tilly_container_free(cache);
}

static jolt_sljit_program_t *jolt_sljit_cache_find(jolt_sljit_cache_t *cache,
    const uint8_t *code, size_t size, uint64_t hash)
{
    for (size_t i = 0; i < JOLT_SLJIT_CACHE_LIMIT; ++i) {
        jolt_sljit_program_t *program = &cache->programs[i];
        if (program->code && program->size == size && program->hash == hash &&
            memcmp(program->bytecode, code, size) == 0) {
            program->last_used = ++cache->tick;
            return program;
        }
    }
    return NULL;
}

static jolt_sljit_program_t *jolt_sljit_cache_slot(jolt_sljit_cache_t *cache)
{
    size_t victim = 0;
    unsigned oldest = cache->programs[0].last_used;
    for (size_t i = 0; i < JOLT_SLJIT_CACHE_LIMIT; ++i) {
        if (!cache->programs[i].code) return &cache->programs[i];
        if (cache->programs[i].last_used < oldest) {
            oldest = cache->programs[i].last_used;
            victim = i;
        }
    }
    jolt_sljit_program_destroy(&cache->programs[victim]);
    return &cache->programs[victim];
}

jolt_status_t jolt_sljit_cache_run(jolt_sljit_cache_t *cache,
    const uint8_t *code, size_t size, const float *inputs, size_t input_count,
    float *outputs, size_t output_count)
{
    (void)input_count;
    if (!cache || !code || !outputs) return JOLT_ERR_ARGUMENT;
    const uint64_t hash = jolt_sljit_hash(code, size);
    jolt_sljit_program_t *program = jolt_sljit_cache_find(cache, code, size, hash);
    if (!program) {
        void *compiled_code = NULL;
        jolt_status_t status = jolt_sljit_compile(code, size, &compiled_code);
        if (status != JOLT_OK) return status;
        uint8_t *bytecode = tilly_container_alloc(size);
        if (!bytecode) {
            sljit_free_code(compiled_code, NULL);
            return JOLT_ERR_MEMORY;
        }
        memcpy(bytecode, code, size);
        program = jolt_sljit_cache_slot(cache);
        program->bytecode = bytecode;
        program->size = size;
        program->hash = hash;
        program->last_used = ++cache->tick;
        program->code = compiled_code;
    }

    jolt_sljit_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.inputs = inputs;
    jolt_sljit_entry_t entry = jolt_sljit_entry_from_code(program->code);
    sljit_sw result = entry(&ctx);
    if (result != (sljit_sw)JOLT_OK) return (jolt_status_t)result;
    memcpy(outputs, ctx.result, output_count * sizeof(float));
    return JOLT_OK;
}
