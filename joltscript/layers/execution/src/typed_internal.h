#ifndef JOLT_TYPED_INTERNAL_H
#define JOLT_TYPED_INTERNAL_H
#include "joltscript/value.h"
#include <stdbool.h>
#define JT_NODES 4096u
#define JT_DEPTH 128u
#define JT_HEADER 32u
#define JT_NODE 28u
/* Version 2 is bounded tree bytecode: atom/list/vector/string nodes in preorder.
 * Node words: tag, first child, next sibling, text offset, text size, line, column.
 * Text is UTF-8 token data, not executable source. Headers add node count, pool
 * size, root index and reserved zero. The validator resolves lexical references
 * and infers/verifies all types before any node can execute. */
typedef struct { uint32_t tag, child, next, text, length, line, column; } jt_node;
typedef struct { size_t line, column; char message[160]; } jt_error;
jolt_status_t jt_check(const uint8_t *code, size_t size, jt_error *error);
jolt_status_t jt_prepare(uint8_t *code, size_t size, jt_error *error);
jolt_status_t jt_eval(const uint8_t *code, size_t size, const jolt_value_t *inputs,
    size_t ni, jolt_value_t *outputs, size_t no, uint64_t steps);
jolt_status_t jt_run_float(const uint8_t *code, size_t size, const float *inputs,
    size_t ni, float *outputs, size_t no);
jolt_status_t jt_compile(const char *source, uint8_t **code, size_t *size, jt_error *error);
bool jt_source_typed(const char *source);
#endif
