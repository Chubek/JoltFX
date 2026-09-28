#include "bytecode_internal.h"
#include <math.h>
_Static_assert(sizeof(float) == 4, "JBC1 requires binary32 floats");
jolt_status_t jolt_bytecode_validate(const uint8_t *code, size_t size) {
    if (!code || size < 24 || size > 16 + 8 * JOLT_MAX_INSTRUCTIONS || (size - 16) % 8)
        return JOLT_ERR_BYTECODE;
    uint32_t inputs = jolt_read_u32(code + 8), outputs = jolt_read_u32(code + 12);
    if (jolt_read_u32(code) != JOLT_BYTECODE_MAGIC || jolt_read_u32(code + 4) != 1 ||
        inputs > JOLT_MAX_INPUTS || !outputs || outputs > JOLT_MAX_OUTPUTS)
        return JOLT_ERR_BYTECODE;
    unsigned stack = 0, written = 0;
    for (size_t pos = 16; pos < size; pos += 8) {
        uint32_t op = jolt_read_u32(code + pos), arg = jolt_read_u32(code + pos + 4);
        if (written == outputs) return JOLT_ERR_BYTECODE;
        if (op == JOLT_OP_CONST || op == JOLT_OP_INPUT) {
            if ((op == JOLT_OP_INPUT && arg >= inputs) ||
                (op == JOLT_OP_CONST && !isfinite(jolt_read_float(arg))) || stack == JOLT_MAX_STACK)
                return JOLT_ERR_BYTECODE;
            ++stack;
        } else if (op == JOLT_OP_OUTPUT) {
            if (stack != 1 || arg != written) return JOLT_ERR_BYTECODE;
            --stack; ++written;
        } else {
            if (arg || op < JOLT_OP_ADD || op > JOLT_OP_SHR) return JOLT_ERR_BYTECODE;
            unsigned arity = op == JOLT_OP_SELECT ? 3u :
                (op == JOLT_OP_ABS || op == JOLT_OP_FLOOR || op == JOLT_OP_SQRT || op == JOLT_OP_NOT ? 1u : 2u);
            if (stack < arity) return JOLT_ERR_BYTECODE;
            stack = stack - arity + 1;
        }
    }
    return stack == 0 && written == outputs ? JOLT_OK : JOLT_ERR_BYTECODE;
}
