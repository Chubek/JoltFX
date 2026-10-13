#include "bytecode_internal.h"
#include "typed_internal.h"
#include <math.h>
#include <stdbool.h>
_Static_assert(sizeof(float) == 4, "JBC1 requires binary32 floats");
typedef struct {
    unsigned stack, written;
    uint32_t initialized[(JOLT_MAX_LOCALS + 31u) / 32u];
} flow_t;
/* All predecessors have smaller offsets, so a single forward traversal reaches
 * a fixed point. Locals are definitely initialized only on every incoming path. */
static bool merge(flow_t *into, const flow_t *from) {
    if (into->stack != from->stack || into->written != from->written) return false;
    for (size_t i = 0; i < sizeof(into->initialized)/sizeof(*into->initialized); ++i)
        into->initialized[i] &= from->initialized[i];
    return true;
}
jolt_status_t jolt_bytecode_validate(const uint8_t *code, size_t size) {
    if(code && size>=16 && jolt_read_u32(code+4)==2) return jt_check(code,size,NULL);
    if (!code || size < 24 || size > 16 + 8 * JOLT_MAX_INSTRUCTIONS || (size - 16) % 8)
        return JOLT_ERR_BYTECODE;
    uint32_t inputs = jolt_read_u32(code + 8), outputs = jolt_read_u32(code + 12);
    if (jolt_read_u32(code) != JOLT_BYTECODE_MAGIC || jolt_read_u32(code + 4) != 1 ||
        inputs > JOLT_MAX_INPUTS || !outputs || outputs > JOLT_MAX_OUTPUTS)
        return JOLT_ERR_BYTECODE;
    flow_t flow = {0};
    struct { uint32_t target; flow_t flow; } pending[JOLT_MAX_BRANCHES];
    unsigned pending_count = 0;
    bool live = true;
    for (size_t pos = 16; pos <= size; pos += 8) {
        for (unsigned i = 0; i < pending_count; ++i) {
            if (pending[i].target != pos) continue;
            if (live) {
                if (!merge(&flow, &pending[i].flow)) return JOLT_ERR_BYTECODE;
            } else { flow = pending[i].flow; live = true; }
            pending[i] = pending[--pending_count];
            break; /* Each pending target is unique. */
        }
        /* Reject unreachable instructions rather than letting malformed dead
         * code bypass validation. Compiler-emitted conditionals have none. */
        if (!live) return JOLT_ERR_BYTECODE;
        if (pos == size)
            return !pending_count && !flow.stack && flow.written == outputs ? JOLT_OK : JOLT_ERR_BYTECODE;
        uint32_t op = jolt_read_u32(code + pos), arg = jolt_read_u32(code + pos + 4);
        if (op == JOLT_OP_JUMP || op == JOLT_OP_BRANCH_IF_ZERO) {
            if (arg <= pos || arg > size || (arg - 16) % 8) return JOLT_ERR_BYTECODE;
            if (op == JOLT_OP_BRANCH_IF_ZERO) {
                if (!flow.stack || flow.written == outputs) return JOLT_ERR_BYTECODE;
                --flow.stack;
            }
            unsigned i = 0;
            while (i < pending_count && pending[i].target != arg) ++i;
            if (i == pending_count) {
                if (pending_count == JOLT_MAX_BRANCHES) return JOLT_ERR_BYTECODE;
                pending[i].target = arg;
                pending[i].flow = flow;
                ++pending_count;
            } else if (!merge(&pending[i].flow, &flow)) return JOLT_ERR_BYTECODE;
            if (op == JOLT_OP_JUMP) live = false;
            continue;
        }
        if (flow.written == outputs) return JOLT_ERR_BYTECODE;
        if (op == JOLT_OP_LOAD_LOCAL) {
            if (arg >= JOLT_MAX_LOCALS || !(flow.initialized[arg/32] & (UINT32_C(1) << (arg%32))) || flow.stack == JOLT_MAX_STACK)
                return JOLT_ERR_BYTECODE;
            ++flow.stack;
        } else if (op == JOLT_OP_STORE_LOCAL) {
            if (arg >= JOLT_MAX_LOCALS || !flow.stack) return JOLT_ERR_BYTECODE;
            flow.initialized[arg/32] |= UINT32_C(1) << (arg%32);
        } else if (op == JOLT_OP_DROP) {
            if (arg || !flow.stack) return JOLT_ERR_BYTECODE;
            --flow.stack;
        } else if (op == JOLT_OP_CONST || op == JOLT_OP_INPUT) {
            if ((op == JOLT_OP_INPUT && arg >= inputs) ||
                (op == JOLT_OP_CONST && !isfinite(jolt_read_float(arg))) || flow.stack == JOLT_MAX_STACK)
                return JOLT_ERR_BYTECODE;
            ++flow.stack;
        } else if (op == JOLT_OP_OUTPUT) {
            if (flow.stack != 1 || arg != flow.written) return JOLT_ERR_BYTECODE;
            --flow.stack; ++flow.written;
        } else {
            if (arg || op < JOLT_OP_ADD || op > JOLT_OP_SHR) return JOLT_ERR_BYTECODE;
            unsigned arity = op == JOLT_OP_SELECT ? 3u :
                (op == JOLT_OP_ABS || op == JOLT_OP_FLOOR || op == JOLT_OP_SQRT || op == JOLT_OP_NOT ? 1u : 2u);
            if (flow.stack < arity) return JOLT_ERR_BYTECODE;
            flow.stack = flow.stack - arity + 1;
        }
    }
    return JOLT_ERR_BYTECODE;
}
