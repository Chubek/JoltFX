#include "joltscript/compiler.h"
#include "tilly/containers.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
KHASH_MAP_INIT_STR(symbols, uint32_t)
struct jolt_program { kvec_t(uint8_t) bytes; };
typedef struct {
    const char *start, *cursor;
    jolt_program_t *program;
    khash_t(symbols) *symbols;
    jolt_diagnostic_t *diagnostic;
    jolt_status_t status;
} parser_t;
static bool fail(parser_t *p, jolt_status_t status, const char *message) {
    if (p->status != JOLT_OK) return false;
    p->status = status;
    if (p->diagnostic) {
        size_t line = 1, column = 1;
        for (const char *s = p->start; s < p->cursor; ++s) {
            if (*s == '\n') { ++line; column = 1; } else ++column;
        }
        p->diagnostic->line = line; p->diagnostic->column = column;
        snprintf(p->diagnostic->message, sizeof(p->diagnostic->message), "%s", message);
    }
    return false;
}
static void space(parser_t *p) {
    for (;;) {
        while (isspace((unsigned char)*p->cursor)) ++p->cursor;
        if (*p->cursor != ';') break;
        while (*p->cursor && *p->cursor != '\n') ++p->cursor;
    }
}
static bool character(parser_t *p, char expected) {
    space(p);
    if (*p->cursor != expected) return fail(p, JOLT_ERR_SYNTAX, "unexpected delimiter");
    ++p->cursor; return true;
}
static bool token(parser_t *p, char out[64]) {
    space(p); size_t n = 0;
    while (*p->cursor && !isspace((unsigned char)*p->cursor) && !strchr("()[];", *p->cursor)) {
        if (n == 63) return fail(p, JOLT_ERR_SYNTAX, "token exceeds 63 bytes");
        out[n++] = *p->cursor++;
    }
    out[n] = 0;
    return n ? true : fail(p, JOLT_ERR_SYNTAX, "expected a symbol or number");
}
static bool word(parser_t *p, const char *expected) {
    char value[64];
    return token(p, value) && (strcmp(value, expected) == 0 || fail(p, JOLT_ERR_SYNTAX, "expected defkernel"));
}
static void put_u32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static bool emit(parser_t *p, uint32_t opcode, uint32_t operand) {
    if (p->program->bytes.n >= 16 + 8 * JOLT_MAX_INSTRUCTIONS)
        return fail(p, JOLT_ERR_BUDGET, "instruction limit exceeded");
    if (!tilly_vec_reserve(&p->program->bytes, p->program->bytes.n + 8))
        return fail(p, JOLT_ERR_MEMORY, "allocation failed");
    put_u32(p->program->bytes.a + p->program->bytes.n, opcode);
    put_u32(p->program->bytes.a + p->program->bytes.n + 4, operand);
    p->program->bytes.n += 8; return true;
}
static const struct { const char *name; uint32_t op; unsigned arity; } operations[] = {
    {"+",JOLT_OP_ADD,2}, {"-",JOLT_OP_SUB,2}, {"*",JOLT_OP_MUL,2}, {"/",JOLT_OP_DIV,2},
    {"min",JOLT_OP_MIN,2}, {"max",JOLT_OP_MAX,2}, {"abs",JOLT_OP_ABS,1},
    {"floor",JOLT_OP_FLOOR,1}, {"pow",JOLT_OP_POW,2}, {"sqrt",JOLT_OP_SQRT,1},
    {"<",JOLT_OP_LT,2}, {"select",JOLT_OP_SELECT,3}
};
static bool expression(parser_t *p, unsigned depth) {
    if (depth >= 64) return fail(p, JOLT_ERR_BUDGET, "expression nesting limit exceeded");
    space(p); char value[64];
    if (*p->cursor == '(') {
        ++p->cursor;
        if (!token(p, value)) return false;
        for (size_t i = 0; i < sizeof(operations)/sizeof(*operations); ++i) {
            if (strcmp(value, operations[i].name)) continue;
            for (unsigned j = 0; j < operations[i].arity; ++j)
                if (!expression(p, depth + 1)) return false;
            return character(p, ')') && emit(p, operations[i].op, 0);
        }
        return fail(p, JOLT_ERR_SYNTAX, "unknown operation");
    }
    if (!token(p, value)) return false;
    khiter_t k = kh_get(symbols, p->symbols, value);
    if (k != kh_end(p->symbols)) return emit(p, JOLT_OP_INPUT, kh_value(p->symbols,k));
    char *end; errno = 0;
    float number = strtof(value, &end);
    if (end == value || *end || errno || !isfinite(number))
        return fail(p, JOLT_ERR_SYNTAX, "undefined binding or invalid finite f32 literal");
    uint32_t bits; memcpy(&bits, &number, 4);
    return emit(p, JOLT_OP_CONST, bits);
}
jolt_status_t jolt_compile(const char *source, jolt_program_t **out, jolt_diagnostic_t *d) {
    if (out) *out = NULL;
    if (!source || !out || (d && d->size != sizeof(*d))) return JOLT_ERR_ARGUMENT;
    if (d) { d->line = d->column = 0; d->message[0] = 0; }
    size_t length = 0;
    while (source[length] && length <= 1024 * 1024) ++length;
    if (length > 1024 * 1024) return JOLT_ERR_BUDGET;
    parser_t p = {.start=source,.cursor=source,.diagnostic=d};
    p.program = tilly_container_calloc(1,sizeof(*p.program));
    p.symbols = kh_init(symbols);
    if (!p.program || !p.symbols) { p.status = JOLT_ERR_MEMORY; goto done; }
    if (!tilly_vec_reserve(&p.program->bytes,16)) { p.status=JOLT_ERR_MEMORY; goto done; }
    p.program->bytes.n = 16;
    char name[64]; uint32_t count = 0, outputs = 1;
    if (!character(&p,'(') || !word(&p,"defkernel") || !token(&p,name) || !character(&p,'[')) goto done;
    for (;;) {
        space(&p); if (*p.cursor == ']') { ++p.cursor; break; }
        if (count == JOLT_MAX_INPUTS) { fail(&p,JOLT_ERR_BUDGET,"too many inputs"); goto done; }
        if (!token(&p,name)) goto done;
        if (!isalpha((unsigned char)name[0]) && name[0] != '_') {
            fail(&p,JOLT_ERR_SYNTAX,"binding must start with a letter or underscore"); goto done;
        }
        char *owned = tilly_container_alloc(strlen(name)+1);
        if (!owned) { p.status=JOLT_ERR_MEMORY; goto done; }
        strcpy(owned,name); int ret;
        khiter_t k = kh_put(symbols,p.symbols,owned,&ret);
        if (ret <= 0) {
            tilly_container_free(owned);
            fail(&p,ret < 0 ? JOLT_ERR_MEMORY : JOLT_ERR_SYNTAX,"duplicate binding or allocation failure"); goto done;
        }
        kh_value(p.symbols,k) = count++;
    }
    space(&p);
    const char *body = p.cursor;
    if (*p.cursor == '(') {
        ++p.cursor;
        if (!token(&p,name)) goto done;
        if (!strcmp(name,"rgba")) outputs = 4; else p.cursor = body;
    }
    for (uint32_t i = 0; i < outputs; ++i)
        if (!expression(&p,0) || !emit(&p,JOLT_OP_OUTPUT,i)) goto done;
    if (outputs == 4 && !character(&p,')')) goto done;
    if (!character(&p,')')) goto done;
    space(&p);
    if (*p.cursor) { fail(&p,JOLT_ERR_SYNTAX,"trailing source after kernel"); goto done; }
    put_u32(p.program->bytes.a,JOLT_BYTECODE_MAGIC);
    put_u32(p.program->bytes.a+4,1);
    put_u32(p.program->bytes.a+8,count);
    put_u32(p.program->bytes.a+12,outputs);
    p.status = jolt_bytecode_validate(p.program->bytes.a,p.program->bytes.n);
done:
    if (p.symbols) {
        for (khiter_t k=kh_begin(p.symbols);k!=kh_end(p.symbols);++k)
            if (kh_exist(p.symbols,k)) tilly_container_free((void *)kh_key(p.symbols,k));
        kh_destroy(symbols,p.symbols);
    }
    if (p.status != JOLT_OK) jolt_program_destroy(p.program); else *out=p.program;
    return p.status;
}
void jolt_program_destroy(jolt_program_t *p) {
    if (p) { tilly_vec_destroy(p->bytes); tilly_container_free(p); }
}
const uint8_t *jolt_program_data(const jolt_program_t *p, size_t *size) {
    if (size) *size = p ? p->bytes.n : 0;
    return p && size ? p->bytes.a : NULL;
}
jolt_status_t jolt_program_run(jolt_vm_t *vm, const jolt_program_t *p,
    const float *in, size_t ni, float *out, size_t no) {
    return p ? jolt_vm_run(vm,p->bytes.a,p->bytes.n,in,ni,out,no) : JOLT_ERR_ARGUMENT;
}
