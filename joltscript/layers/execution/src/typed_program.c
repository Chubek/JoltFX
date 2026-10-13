#include "bytecode_internal.h"
#include "typed_internal.h"
#ifdef JOLT_TYPED_HOST
#include <stddef.h>
#include <string.h>
extern void *jt_host_alloc(size_t size);
extern void jt_host_free(void *pointer);
#define tilly_mem_alloc jt_host_alloc
#define tilly_mem_free jt_host_free
static void *tilly_mem_calloc(size_t n, size_t width)
{
    if (!width || n > SIZE_MAX / width)
        return NULL;
    void *p = jt_host_alloc(n * width);
    if (p)
        memset(p, 0, n * width);
    return p;
}
#else
#include "tilly/memory.h"
#endif
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { T_NEVER = 20, T_RGBA = 21, T_ANY = 22, T_FUNCTION = 23, T_FN = 4096, T_NATIVE = 8192 };
enum {
    O_LITERAL = 1,
    O_LOAD,
    O_BIND,
    O_SET,
    O_SEQ,
    O_IF,
    O_COND,
    O_WHEN,
    O_UNLESS,
    O_WHILE,
    O_FOR,
    O_LOOP,
    O_BREAK,
    O_CONTINUE,
    O_FN,
    O_CALL,
    O_RGBA,
    O_CAST,
    O_ADD,
    O_SUB,
    O_MUL,
    O_DIV,
    O_MIN,
    O_MAX,
    O_ABS,
    O_FLOOR,
    O_POW,
    O_SQRT,
    O_LT,
    O_GT,
    O_LE,
    O_GE,
    O_EQ,
    O_NE,
    O_AND,
    O_OR,
    O_NOT,
    O_BAND,
    O_BOR,
    O_BXOR,
    O_SHL,
    O_SHR,
    O_VECTOR,
    O_NATIVE,
    O_NATIVE_REF
};
typedef struct {
    const char *module, *name;
    unsigned group, min, max, result, constant, variadic, params;
    unsigned types[8];
} std_entry;
#include "stdlib_catalog.h"
#define STD_COUNT (sizeof(stdlib) / sizeof(*stdlib))
typedef struct {
    uint32_t id, type;
    bool mutable;
} binding;
typedef struct {
    const uint8_t *code;
    const char *pool;
    uint32_t count, root, kernel, ni, no;
    jt_node nodes[JT_NODES];
    uint32_t type[JT_NODES], ref[JT_NODES], op[JT_NODES], ret[JT_NODES];
    uint8_t fn_state[JT_NODES];
    bool imports[STD_COUNT];
    int library;
    jolt_value_t literals[JT_NODES];
    char *strings[JT_NODES];
    binding bindings[JT_NODES];
    unsigned nb, globals_count, loop_depth;
    jt_error *error;
    jolt_status_t status;
} module;
static uint32_t fail(module *m, uint32_t n, jolt_status_t s, const char *msg)
{
    if (!m->status) {
        m->status = s;
        if (m->error) {
            m->error->line = m->nodes[n].line;
            m->error->column = m->nodes[n].column;
            snprintf(m->error->message, sizeof(m->error->message), "%s", msg);
        }
    }
    return 0;
}
static bool is(module *m, uint32_t n, const char *s)
{
    return n && m->nodes[n].tag == 0 && strlen(s) == m->nodes[n].length &&
           !memcmp(m->pool + m->nodes[n].text, s, m->nodes[n].length);
}
static uint32_t next(module *m, uint32_t n)
{
    return m->nodes[n].next;
}
static uint32_t child(module *m, uint32_t n)
{
    return m->nodes[n].child;
}
static uint32_t nth(module *m, uint32_t n, unsigned i)
{
    n = child(m, n);
    while (n && i--)
        n = next(m, n);
    return n;
}
static unsigned length(module *m, uint32_t n)
{
    unsigned count = 0;
    for (n = child(m, n); n; n = next(m, n))
        ++count;
    return count;
}
static const char *const type_names[] = {"void", "i32",    "i64", "u8",   "u16",  "u32",
                                         "u64",  "f32",    "f64", "bool", "char", "string",
                                         "list", "vector", "map", "set"};
static int type_name(module *m, uint32_t n)
{
    for (unsigned i = 0; i < 16; ++i)
        if (is(m, n, type_names[i]))
            return (int) i;
    if (is(m, n, "value"))
        return T_ANY;
    if (is(m, n, "function"))
        return T_FUNCTION;
    if (is(m, n, "number"))
        return 100;
    if (is(m, n, "sequence"))
        return 101;
    return -1;
}
static bool numeric(uint32_t t)
{
    return t >= 1 && t <= 8;
}
static bool integer(uint32_t t)
{
    return t >= 1 && t <= 6;
}
static bool signed_type(uint32_t t)
{
    return t == 1 || t == 2;
}
static unsigned width(uint32_t t)
{
    return t == 3 ? 8u : t == 4 ? 16u : t == 1 || t == 5 || t == 7 ? 32u : 64u;
}
static uint64_t mask(uint32_t t)
{
    return width(t) == 64 ? UINT64_MAX : (UINT64_C(1) << width(t)) - 1;
}
static int64_t signed_bits(uint64_t bits, unsigned w)
{
    uint64_t sign = UINT64_C(1) << (w - 1);
    bits &= w == 64 ? UINT64_MAX : (UINT64_C(1) << w) - 1;
    if (!(bits & sign))
        return (int64_t) bits;
    return -1 - (int64_t) (~bits & (w == 64 ? UINT64_MAX : (UINT64_C(1) << w) - 1));
}
static double real(jolt_value_t v)
{
    if (v.type == 7 || v.type == 8)
        return v.as.number;
    if (signed_type(v.type))
        return (double) v.as.integer;
    return (double) v.as.unsigned_integer;
}
static uint32_t common(module *m, uint32_t n, uint32_t a, uint32_t b)
{
    if (a == T_ANY || b == T_ANY)
        return T_ANY;
    if (a == T_NEVER)
        return b;
    if (b == T_NEVER || a == b)
        return a;
    if (!numeric(a) || !numeric(b))
        return fail(m, n, JOLT_ERR_TYPE, "incompatible expression types");
    if (a == 8 || b == 8)
        return 8;
    if (a == 7 || b == 7)
        return 7;
    unsigned w = width(a) > width(b) ? width(a) : width(b);
    if (signed_type(a) != signed_type(b)) {
        unsigned uw = width(signed_type(a) ? b : a), sw = width(signed_type(a) ? a : b);
        if (uw >= sw) {
            if (uw == 64)
                return fail(m, n, JOLT_ERR_TYPE, "u64/signed arithmetic requires an explicit cast");
            w = uw == 32 ? 64 : 32;
        }
        return w == 64 ? 2 : 1;
    }
    return signed_type(a) ? (w == 64 ? 2u : 1u) : (w == 64 ? 6u : w == 32 ? 5u : w == 16 ? 4u : 3u);
}
static bool assignable(uint32_t dst, uint32_t src)
{
    if (dst == T_ANY || src == T_ANY)
        return true;
    if (dst == T_FUNCTION)
        return src >= T_FN || src == T_FUNCTION;
    if (dst == 100)
        return numeric(src);
    if (dst == 101)
        return src == 12 || src == 13 || src == 15;
    if (dst == src || src == T_NEVER)
        return true;
    if (!numeric(dst) || !numeric(src))
        return false;
    if (dst == 8)
        return true;
    if (dst == 7)
        return src != 8;
    if (!integer(src))
        return false;
    if (signed_type(dst) == signed_type(src))
        return width(dst) >= width(src);
    return signed_type(dst) && width(dst) > width(src);
}
static int std_lookup(module *m, uint32_t n)
{
    int found = -1;
    for (unsigned i = 0; i < STD_COUNT; ++i)
        if (m->imports[i]) {
            char qualified[128];
            snprintf(qualified, sizeof(qualified), "%s/%s", stdlib[i].module, stdlib[i].name);
            if (is(m, n, qualified))
                return (int) i;
            if (is(m, n, stdlib[i].name)) {
                if (found >= 0) {
                    fail(m, n, JOLT_ERR_SYNTAX, "ambiguous import; use a module-qualified name");
                    return -1;
                }
                found = (int) i;
            }
        }
    return found;
}
static bool std_import(module *m, uint32_t n)
{
    uint32_t mod = nth(m, n, 1), names = nth(m, n, 2);
    bool known = false;
    if (length(m, n) < 2 || length(m, n) > 3 || (names && m->nodes[names].tag != 1)) {
        fail(m, n, JOLT_ERR_SYNTAX, "expected (import jolt.module (names...))");
        return false;
    }
    for (unsigned i = 0; i < STD_COUNT; ++i)
        if (is(m, mod, stdlib[i].module)) {
            known = true;
            if (!names)
                m->imports[i] = true;
            for (uint32_t p = child(m, names); p; p = next(m, p))
                if (is(m, p, stdlib[i].name))
                    m->imports[i] = true;
        }
    if (!known) {
        fail(m, mod, JOLT_ERR_NOT_FOUND, "unknown standard-library module");
        return false;
    }
    for (uint32_t p = child(m, names); p; p = next(m, p)) {
        bool exported = false;
        for (unsigned i = 0; i < STD_COUNT; ++i)
            if (is(m, mod, stdlib[i].module) && is(m, p, stdlib[i].name))
                exported = true;
        if (!exported) {
            fail(m, p, JOLT_ERR_NOT_FOUND, "name is not exported by this module");
            return false;
        }
    }
    return true;
}
static bool same_name(module *m, uint32_t a, uint32_t b)
{
    return m->nodes[a].length == m->nodes[b].length &&
           !memcmp(m->pool + m->nodes[a].text, m->pool + m->nodes[b].text, m->nodes[a].length);
}
static int lookup(module *m, uint32_t name)
{
    for (unsigned i = m->nb; i; --i)
        if (same_name(m, m->bindings[i - 1].id, name))
            return (int) i - 1;
    return -1;
}
static bool name_ok(module *m, uint32_t n)
{
    return n && m->nodes[n].tag == 0 && m->nodes[n].length &&
           (isalpha((unsigned char) m->pool[m->nodes[n].text]) || m->pool[m->nodes[n].text] == '_');
}
static bool bind(module *m, uint32_t name, uint32_t type, bool mut, unsigned base)
{
    if (!name_ok(m, name)) {
        fail(m, name, JOLT_ERR_SYNTAX, "binding must start with a letter or underscore");
        return false;
    }
    for (unsigned i = base; i < m->nb; ++i)
        if (same_name(m, m->bindings[i].id, name)) {
            fail(m, name, JOLT_ERR_SYNTAX, "duplicate binding");
            return false;
        }
    if (m->nb == JT_NODES) {
        fail(m, name, JOLT_ERR_BUDGET, "binding limit exceeded");
        return false;
    }
    m->bindings[m->nb++] = (binding){name, type, mut};
    m->type[name] = type;
    return true;
}
static bool utf8(const char *s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char) s[i++];
        if (c < 128)
            continue;
        unsigned n = c >= 0xc2 && c <= 0xdf   ? 1u
                     : c >= 0xe0 && c <= 0xef ? 2u
                     : c >= 0xf0 && c <= 0xf4 ? 3u
                                              : 99u;
        if (n > 3 || n > len - i)
            return false;
        uint32_t v = c & ((1u << (6 - n)) - 1u);
        for (unsigned j = 0; j < n; ++j) {
            c = (unsigned char) s[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            v = (v << 6) | (c & 63u);
        }
        if (v < (n == 1   ? 0x80u
                 : n == 2 ? 0x800u
                          : 0x10000u) ||
            v > 0x10ffffu || (v >= 0xd800u && v <= 0xdfffu))
            return false;
    }
    return true;
}
static unsigned encode(char *p, uint32_t c)
{
    if (c < 128) {
        p[0] = (char) c;
        return 1;
    }
    if (c < 2048) {
        p[0] = (char) (0xc0u | (c >> 6));
        p[1] = (char) (0x80u | (c & 63));
        return 2;
    }
    if (c < 65536) {
        p[0] = (char) (0xe0u | (c >> 12));
        p[1] = (char) (0x80u | ((c >> 6) & 63));
        p[2] = (char) (0x80u | (c & 63));
        return 3;
    }
    p[0] = (char) (0xf0u | (c >> 18));
    p[1] = (char) (0x80u | ((c >> 12) & 63));
    p[2] = (char) (0x80u | ((c >> 6) & 63));
    p[3] = (char) (0x80u | (c & 63));
    return 4;
}
static bool unicode_escape(const char *s, size_t size, size_t *i, uint32_t *v)
{
    if (*i >= size || s[(*i)++] != '{')
        return false;
    unsigned digits = 0;
    *v = 0;
    while (*i < size && s[*i] != '}') {
        char c = s[(*i)++];
        unsigned d = c >= '0' && c <= '9'   ? (unsigned) (c - '0')
                     : c >= 'a' && c <= 'f' ? (unsigned) (c - 'a' + 10)
                     : c >= 'A' && c <= 'F' ? (unsigned) (c - 'A' + 10)
                                            : 99u;
        if (d > 15 || ++digits > 6)
            return false;
        *v = (*v << 4) | d;
    }
    return digits && *i < size && s[(*i)++] == '}' && *v <= 0x10ffffu &&
           !(*v >= 0xd800u && *v <= 0xdfffu);
}
static bool literal(module *m, uint32_t n)
{
    jt_node node = m->nodes[n];
    const char *s = m->pool + node.text;
    size_t len = node.length;
    jolt_value_t v = {0};
    if (node.tag == 3) {
        if (len < 2 || s[0] != '"' || s[len - 1] != '"')
            goto invalid;
        char *p = tilly_mem_alloc(len + 1);
        if (!p) {
            fail(m, n, JOLT_ERR_MEMORY, "allocation failed");
            return false;
        }
        m->strings[n] = p;
        size_t out = 0;
        for (size_t i = 1; i < len - 1;) {
            char c = s[i++];
            if (c == '\\') {
                if (i >= len - 1)
                    goto invalid;
                c = s[i++];
                if (c == 'u') {
                    uint32_t cp;
                    if (!unicode_escape(s, len - 1, &i, &cp))
                        goto invalid;
                    out += encode(p + out, cp);
                    continue;
                }
                if (c == 'n')
                    c = '\n';
                else if (c == 'r')
                    c = '\r';
                else if (c == 't')
                    c = '\t';
                else if (c == '0')
                    c = 0;
                else if (c != '\\' && c != '"')
                    goto invalid;
            }
            p[out++] = c;
        }
        p[out] = 0;
        if (!utf8(p, out))
            goto invalid;
        v.type = JOLT_TYPE_STRING;
        v.as.string.data = p;
        v.as.string.size = out;
    } else if (is(m, n, "true") || is(m, n, "false")) {
        v.type = JOLT_TYPE_BOOL;
        v.as.unsigned_integer = is(m, n, "true") ? 1u : 0u;
    } else if (len && s[0] == '\\') {
        uint32_t cp = 0;
        if (len >= 3 && s[1] == 'u') {
            size_t i = 2;
            if (!unicode_escape(s, len, &i, &cp) || i != len)
                goto invalid;
        } else if (len == 8 && !memcmp(s, "\\newline", 8))
            cp = 10;
        else if (len == 6 && !memcmp(s, "\\space", 6))
            cp = 32;
        else if (len >= 2 && utf8(s + 1, len - 1)) {
            unsigned char c = (unsigned char) s[1];
            unsigned k = c < 128 ? 1u : c < 224 ? 2u : c < 240 ? 3u : 4u;
            if (len != k + 1u)
                goto invalid;
            cp = k == 1 ? c : c & ((1u << (7 - k)) - 1u);
            for (unsigned j = 2; j <= k; ++j)
                cp = (cp << 6) | ((unsigned char) s[j] & 63u);
        } else
            goto invalid;
        v.type = JOLT_TYPE_CHAR;
        v.as.unsigned_integer = cp;
    } else {
        if (len >= 128 || !len)
            return false;
        char text[128];
        memcpy(text, s, len);
        text[len] = 0;
        int t = -1;
        for (unsigned i = 1; i <= 8; ++i) {
            size_t suffix = strlen(type_names[i]);
            if (len > suffix && !strcmp(text + len - suffix, type_names[i])) {
                t = (int) i;
                text[len - suffix] = 0;
                break;
            }
        }
        if (t < 0)
            t = strpbrk(text, ".eE") ? 8 : 1;
        char *end = NULL;
        errno = 0;
        if (t == 7 || t == 8) {
            double x = t == 7 ? (double) strtof(text, &end) : strtod(text, &end);
            if (end == text || *end || errno || !isfinite(x))
                return false;
            v.type = (jolt_type_t) t;
            v.as.number = x;
        } else {
            bool neg = text[0] == '-';
            const char *digits = text + (text[0] == '-' || text[0] == '+');
            if (!isdigit((unsigned char) *digits))
                return false;
            uint64_t u = strtoull(digits, &end, 10);
            if (end == digits || *end || errno)
                goto invalid;
            unsigned w = width((uint32_t) t);
            uint64_t max = mask((uint32_t) t);
            if (signed_type((uint32_t) t))
                max = (UINT64_C(1) << (w - 1)) - (neg ? 0u : 1u);
            if (u > max || (neg && !signed_type((uint32_t) t)))
                goto invalid;
            v.type = (jolt_type_t) t;
            if (signed_type((uint32_t) t))
                v.as.integer = signed_bits(neg ? 0 - u : u, w);
            else
                v.as.unsigned_integer = u;
        }
    }
    m->literals[n] = v;
    m->type[n] = (uint32_t) v.type;
    m->op[n] = O_LITERAL;
    return true;
invalid:
    fail(m, n, JOLT_ERR_SYNTAX, "invalid or out-of-range literal");
    return false;
}
static uint32_t check(module *m, uint32_t n, unsigned depth, unsigned base);
static uint32_t scoped(module *m, uint32_t n, unsigned depth)
{
    unsigned saved = m->nb;
    uint32_t t = check(m, n, depth, saved);
    m->nb = saved;
    return t;
}
static uint32_t sequence(module *m, uint32_t first, unsigned depth, unsigned base)
{
    if (!first)
        return fail(m, 0, JOLT_ERR_SYNTAX, "expected nonempty body");
    uint32_t t = 0;
    for (uint32_t n = first; n && !m->status; n = next(m, n))
        t = check(m, n, depth, base);
    return t;
}
static uint32_t parameter(module *m, uint32_t n, uint32_t *name)
{
    *name = n;
    if (m->nodes[n].tag == 1) {
        *name = nth(m, n, 0);
        int t = type_name(m, nth(m, n, 1));
        if (length(m, n) != 2 || t <= 0 || t >= 100)
            return fail(m, n, JOLT_ERR_TYPE, "expected (name value-type)");
        return (uint32_t) t;
    }
    return JOLT_TYPE_F64;
}
static uint32_t fn_params(module *m, uint32_t fn)
{
    return nth(m, fn, is(m, nth(m, fn, 0), "defn") ? 2u : 1u);
}
static uint32_t fn_body(module *m, uint32_t fn)
{
    uint32_t n = next(m, fn_params(m, fn));
    return is(m, n, "->") ? next(m, next(m, n)) : n;
}
static void signature(module *m, uint32_t fn)
{
    uint32_t params = fn_params(m, fn);
    if (!params || m->nodes[params].tag != 2 || length(m, params) > JOLT_MAX_INPUTS) {
        fail(m, fn, JOLT_ERR_SYNTAX, "expected parameter vector (at most 32 parameters)");
        return;
    }
    for (uint32_t p = child(m, params); p; p = next(m, p)) {
        uint32_t name;
        uint32_t t = parameter(m, p, &name);
        m->type[name] = t;
    }
    uint32_t n = next(m, params);
    m->ret[fn] = JOLT_TYPE_F64;
    if (is(m, n, "->")) {
        int t = type_name(m, next(m, n));
        if (t < 0 || t >= 100) {
            fail(m, n, JOLT_ERR_TYPE, "unknown return type");
            return;
        }
        m->ret[fn] = (uint32_t) t;
    }
}
static uint32_t function(module *m, uint32_t fn, unsigned depth)
{
    if (m->fn_state[fn] == 2)
        return T_FN + fn;
    if (m->fn_state[fn] == 1) {
        if (!is(m, next(m, fn_params(m, fn)), "->"))
            fail(m, fn, JOLT_ERR_TYPE, "recursive functions require an explicit return type");
        return T_FN + fn;
    }
    m->fn_state[fn] = 1;
    signature(m, fn);
    unsigned saved = m->nb, loops = m->loop_depth;
    m->loop_depth = 0;
    binding *caller = NULL;
    if (is(m, child(m, fn), "defn") && saved > m->globals_count) {
        caller = tilly_mem_alloc((saved - m->globals_count) * sizeof(binding));
        if (!caller)
            return fail(m, fn, JOLT_ERR_MEMORY, "allocation failed");
        memcpy(caller, m->bindings + m->globals_count,
               (saved - m->globals_count) * sizeof(binding));
        m->nb = m->globals_count;
    }
    unsigned base = m->nb;
    for (uint32_t p = child(m, fn_params(m, fn)); p && !m->status; p = next(m, p)) {
        uint32_t name;
        uint32_t t = parameter(m, p, &name);
        bind(m, name, t, false, base);
    }
    uint32_t t = sequence(m, fn_body(m, fn), depth + 1, base);
    if (is(m, next(m, fn_params(m, fn)), "->")) {
        if (!assignable(m->ret[fn], t))
            fail(m, fn, JOLT_ERR_TYPE, "function return type mismatch");
    } else
        m->ret[fn] = t;
    if (caller) {
        memcpy(m->bindings + m->globals_count, caller,
               (saved - m->globals_count) * sizeof(binding));
        tilly_mem_free(caller);
    }
    m->nb = saved;
    m->loop_depth = loops;
    m->op[fn] = O_FN;
    m->type[fn] = T_FN + fn;
    m->fn_state[fn] = 2;
    return T_FN + fn;
}
static const struct {
    const char *name;
    uint32_t op;
    unsigned arity;
} ops[] = {{"+", O_ADD, 2},          {"-", O_SUB, 2},
           {"*", O_MUL, 2},          {"/", O_DIV, 2},
           {"min", O_MIN, 2},        {"max", O_MAX, 2},
           {"abs", O_ABS, 1},        {"floor", O_FLOOR, 1},
           {"pow", O_POW, 2},        {"sqrt", O_SQRT, 1},
           {"<", O_LT, 2},           {">", O_GT, 2},
           {"<=", O_LE, 2},          {">=", O_GE, 2},
           {"=", O_EQ, 2},           {"!=", O_NE, 2},
           {"and", O_AND, 2},        {"or", O_OR, 2},
           {"not", O_NOT, 1},        {"bitwise-and", O_BAND, 2},
           {"bitwise-or", O_BOR, 2}, {"bitwise-xor", O_BXOR, 2},
           {"shl", O_SHL, 2},        {"shr", O_SHR, 2}};
static bool truth_type(uint32_t t)
{
    return numeric(t) || t == JOLT_TYPE_BOOL || t == T_ANY;
}
static uint32_t check(module *m, uint32_t n, unsigned depth, unsigned base)
{
    if (m->status)
        return 0;
    if (!n)
        return fail(m, n, JOLT_ERR_SYNTAX, "missing expression");
    if (depth >= JT_DEPTH)
        return fail(m, n, JOLT_ERR_BUDGET, "expression nesting limit exceeded");
    jt_node node = m->nodes[n];
    if (node.tag == 0 || node.tag == 3) {
        if (literal(m, n))
            return m->type[n];
        int b = lookup(m, n);
        if (b < 0) {
            int id = std_lookup(m, n);
            if (id >= 0) {
                m->op[n] = stdlib[id].constant ? O_NATIVE : O_NATIVE_REF;
                m->ref[n] = (uint32_t) id;
                return m->type[n] =
                           stdlib[id].constant ? stdlib[id].result : T_NATIVE + (uint32_t) id;
            }
            for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); ++i)
                if (is(m, n, ops[i].name)) {
                    m->op[n] = O_NATIVE_REF;
                    m->ref[n] = (uint32_t) STD_COUNT + 100 + ops[i].op;
                    return m->type[n] = T_FUNCTION;
                }
            return fail(m, n, JOLT_ERR_SYNTAX, "undefined binding or invalid literal");
        }
        m->op[n] = O_LOAD;
        m->ref[n] = m->bindings[b].id;
        return m->type[n] = m->bindings[b].type;
    }
    if (node.tag == 2) {
        for (uint32_t p = child(m, n); p && !m->status; p = next(m, p))
            scoped(m, p, depth + 1);
        m->op[n] = O_VECTOR;
        return m->type[n] = 13;
    }
    uint32_t h = child(m, n), a = next(m, h), b = next(m, a), c = next(m, b);
    unsigned argc = length(m, n);
    uint32_t t = 0;
    if (!h) {
        m->op[n] = O_LITERAL;
        return 0;
    }
    if (is(m, h, "fn"))
        return function(m, n, depth);
    if (is(m, h, "do")) {
        unsigned saved = m->nb;
        t = sequence(m, a, depth + 1, saved);
        m->nb = saved;
        m->op[n] = O_SEQ;
    } else if (is(m, h, "let") || is(m, h, "var")) {
        bool mut = is(m, h, "var");
        unsigned saved = m->nb;
        if (m->nodes[a].tag == 2) {
            for (uint32_t p = child(m, a); p && !m->status;) {
                uint32_t value = next(m, p);
                if (!value)
                    return fail(m, p, JOLT_ERR_SYNTAX, "binding list requires name/value pairs");
                uint32_t vt = scoped(m, value, depth + 1), name = p;
                if (m->nodes[p].tag == 1) {
                    uint32_t declared = parameter(m, p, &name);
                    if (!assignable(declared, vt))
                        return fail(m, p, JOLT_ERR_TYPE, "binding type mismatch");
                    vt = declared;
                }
                bind(m, name, vt, mut, saved);
                m->ref[p] = name;
                m->type[p] = vt;
                p = next(m, value);
            }
            t = sequence(m, b, depth + 1, saved);
            m->nb = saved;
        } else {
            if (argc != 3)
                return fail(m, n, JOLT_ERR_SYNTAX, "binding requires name and value");
            t = scoped(m, b, depth + 1);
            uint32_t name = a;
            if (m->nodes[a].tag == 1) {
                uint32_t declared = parameter(m, a, &name);
                if (!assignable(declared, t))
                    return fail(m, a, JOLT_ERR_TYPE, "binding type mismatch");
                t = declared;
            }
            bind(m, name, t, mut, base);
            m->ref[a] = name;
            m->type[a] = t;
        }
        m->op[n] = O_BIND;
    } else if (is(m, h, "set!")) {
        if (argc != 3)
            return fail(m, n, JOLT_ERR_SYNTAX, "set! requires name and value");
        int found = lookup(m, a);
        if (found < 0 || !m->bindings[found].mutable)
            return fail(m, a, JOLT_ERR_SYNTAX, "set! requires a mutable binding");
        t = m->bindings[found].type;
        if (!assignable(t, scoped(m, b, depth + 1)))
            return fail(m, b, JOLT_ERR_TYPE, "assignment type mismatch");
        m->op[n] = O_SET;
        m->ref[n] = m->bindings[found].id;
    } else if (is(m, h, "if") || is(m, h, "select")) {
        if (argc != 4)
            return fail(m, n, JOLT_ERR_SYNTAX, "if/select requires three operands");
        if (!truth_type(scoped(m, a, depth + 1)))
            return fail(m, a, JOLT_ERR_TYPE, "condition must be bool or numeric");
        uint32_t left = scoped(m, b, depth + 1), right = scoped(m, c, depth + 1);
        t = common(m, n, left, right);
        m->op[n] = O_IF;
        m->ref[n] = is(m, h, "select") ? 1u : 0u;
    } else if (is(m, h, "cond")) {
        if (argc < 2)
            return fail(m, n, JOLT_ERR_SYNTAX, "expected a cond clause");
        t = T_NEVER;
        bool otherwise = false;
        for (uint32_t clause = a; clause; clause = next(m, clause)) {
            if (m->nodes[clause].tag != 2 || length(m, clause) != 2)
                return fail(m, clause, JOLT_ERR_SYNTAX, "expected [test result]");
            uint32_t test = child(m, clause);
            if (is(m, test, "else")) {
                otherwise = true;
                if (next(m, clause))
                    return fail(m, test, JOLT_ERR_SYNTAX, "else must be last");
            } else if (!truth_type(scoped(m, test, depth + 1)))
                return fail(m, test, JOLT_ERR_TYPE, "condition must be bool or numeric");
            uint32_t result = scoped(m, next(m, test), depth + 1);
            t = common(m, clause, t, result);
        }
        if ((t >= T_FN || t == T_FUNCTION) && !otherwise)
            return fail(m, n, JOLT_ERR_TYPE, "function-valued cond requires else");
        m->op[n] = O_COND;
    } else if (is(m, h, "when") || is(m, h, "unless") || is(m, h, "while") || is(m, h, "loop")) {
        bool loop = is(m, h, "loop"), wh = is(m, h, "while");
        if (argc < (loop ? 2u : 3u))
            return fail(m, n, JOLT_ERR_SYNTAX, "expected nonempty body");
        if (!loop && !truth_type(scoped(m, a, depth + 1)))
            return fail(m, a, JOLT_ERR_TYPE, "condition must be bool or numeric");
        unsigned saved = m->nb;
        if (loop || wh)
            ++m->loop_depth;
        sequence(m, loop ? a : b, depth + 1, saved);
        if (loop || wh)
            --m->loop_depth;
        m->nb = saved;
        m->op[n] = loop ? O_LOOP : wh ? O_WHILE : is(m, h, "when") ? O_WHEN : O_UNLESS;
    } else if (is(m, h, "for")) {
        if (argc < 3 || m->nodes[a].tag != 2 || (length(m, a) != 3 && length(m, a) != 4))
            return fail(m, n, JOLT_ERR_SYNTAX, "expected (for [name start end step?] body...)");
        uint32_t name = child(m, a), start = next(m, name), end = next(m, start),
                 step = next(m, end);
        uint32_t st = scoped(m, start, depth + 1), et = scoped(m, end, depth + 1),
                 it = common(m, n, st, et);
        if (step)
            it = common(m, n, it, scoped(m, step, depth + 1));
        if (!integer(it))
            return fail(m, a, JOLT_ERR_TYPE, "for bounds and step must be integers");
        unsigned saved = m->nb;
        bind(m, name, it, false, saved);
        ++m->loop_depth;
        sequence(m, b, depth + 1, saved);
        --m->loop_depth;
        m->nb = saved;
        m->op[n] = O_FOR;
    } else if (is(m, h, "break") || is(m, h, "continue")) {
        if (argc != 1 || !m->loop_depth)
            return fail(m, n, JOLT_ERR_SYNTAX,
                        "break/continue requires an enclosing loop and no operands");
        m->op[n] = is(m, h, "break") ? O_BREAK : O_CONTINUE;
        t = T_NEVER;
    } else if (is(m, h, "rgba")) {
        if (argc != 5)
            return fail(m, n, JOLT_ERR_SYNTAX, "rgba requires four values");
        for (uint32_t p = a; p; p = next(m, p)) {
            uint32_t pt = scoped(m, p, depth + 1);
            if (pt >= 16 && pt != T_ANY)
                return fail(m, p, JOLT_ERR_TYPE, "invalid output type");
        }
        m->op[n] = O_RGBA;
        t = T_RGBA;
    } else {
        int native = lookup(m, h) < 0 ? std_lookup(m, h) : -1;
        if (native >= 0) {
            const std_entry *entry = &stdlib[native];
            if (argc - 1 < entry->min || argc - 1 > entry->max)
                return fail(m, n, JOLT_ERR_SYNTAX, "standard-library arity mismatch");
            unsigned i = 0;
            for (uint32_t p = a; p; p = next(m, p), ++i) {
                uint32_t pt = scoped(m, p, depth + 1),
                         expected = entry->types[i < entry->params ? i : entry->params - 1];
                if (!assignable(expected, pt))
                    return fail(m, p, JOLT_ERR_TYPE, "standard-library argument type mismatch");
            }
            m->op[n] = O_NATIVE;
            m->ref[n] = (uint32_t) native;
            return m->type[n] = entry->result;
        }
        int cast = type_name(m, h);
        if (cast >= 0) {
            if (argc != 2)
                return fail(m, n, JOLT_ERR_SYNTAX, "cast requires one operand");
            uint32_t src = scoped(m, a, depth + 1);
            t = (uint32_t) cast;
            if (t != src && src != T_ANY &&
                !((numeric(t) || t == 9 || t == 10) && (numeric(src) || src == 9 || src == 10)))
                return fail(m, n, JOLT_ERR_TYPE, "invalid cast");
            m->op[n] = O_CAST;
        } else {
            for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); ++i)
                if (is(m, h, ops[i].name)) {
                    if (argc != ops[i].arity + 1)
                        return fail(m, n, JOLT_ERR_SYNTAX, "operation arity mismatch");
                    uint32_t at = scoped(m, a, depth + 1), bt = b ? scoped(m, b, depth + 1) : at,
                             op = ops[i].op;
                    if (op == O_AND || op == O_OR || op == O_NOT) {
                        if (!truth_type(at) || !truth_type(bt))
                            return fail(m, n, JOLT_ERR_TYPE,
                                        "logical operands must be bool or numeric");
                        t = 9;
                    } else if (op == O_EQ || op == O_NE) {
                        if ((at >= 16 && at != T_ANY) || (bt >= 16 && bt != T_ANY))
                            return fail(m, n, JOLT_ERR_TYPE, "equality requires data operands");
                        common(m, n, at, bt);
                        t = 9;
                    } else {
                        if ((!numeric(at) && at != T_ANY) || (!numeric(bt) && bt != T_ANY))
                            return fail(m, n, JOLT_ERR_TYPE, "numeric operands required");
                        uint32_t ct = (op == O_SHL || op == O_SHR) ? at : common(m, n, at, bt);
                        if (op >= O_BAND && (!integer(at) || !integer(bt)))
                            return fail(m, n, JOLT_ERR_TYPE, "bitwise operands must be integers");
                        t = op >= O_LT && op <= O_GE ? 9u : op == O_SHL || op == O_SHR ? at : ct;
                        if ((op == O_POW || op == O_SQRT) && integer(t))
                            t = 8;
                    }
                    m->op[n] = op;
                    return m->type[n] = t;
                }
            uint32_t ft = check(m, h, depth + 1, base);
            if (ft == T_FUNCTION || ft == T_ANY || ft >= T_NATIVE) {
                if (argc - 1 > JOLT_MAX_INPUTS)
                    return fail(m, n, JOLT_ERR_BUDGET, "at most 32 call arguments");
                for (uint32_t p = a; p; p = next(m, p))
                    scoped(m, p, depth + 1);
                m->op[n] = O_CALL;
                return m->type[n] = T_ANY;
            }
            if (ft < T_FN)
                return fail(m, h, JOLT_ERR_TYPE, "call target is not a function");
            uint32_t fn = ft - T_FN, params = fn_params(m, fn), p = child(m, params), arg = a;
            if (is(m, child(m, fn), "defn"))
                function(m, fn, depth + 1);
            for (; p && arg; p = next(m, p), arg = next(m, arg)) {
                uint32_t name;
                uint32_t pt = parameter(m, p, &name);
                if (!assignable(pt, scoped(m, arg, depth + 1)))
                    return fail(m, arg, JOLT_ERR_TYPE, "function argument type mismatch");
            }
            if (p || arg)
                return fail(m, n, JOLT_ERR_SYNTAX, "function arity mismatch");
            t = m->ret[fn];
            m->ref[n] = fn;
            m->op[n] = O_CALL;
        }
    }
    return m->type[n] = t;
}
static bool tree(module *m, uint32_t n, unsigned depth, uint32_t *expected)
{
    while (n) {
        if (n != *expected || depth >= JT_DEPTH)
            return false;
        ++*expected;
        jt_node x = m->nodes[n];
        if (x.tag > 3 || !x.line || !x.column)
            return false;
        if (x.tag == 1 || x.tag == 2) {
            if (x.length || !tree(m, x.child, depth + 1, expected))
                return false;
        } else if (x.child || !x.length)
            return false;
        n = x.next;
    }
    return true;
}
static void destroy(module *m)
{
    if (m) {
        for (uint32_t i = 1; i < m->count; ++i)
            tilly_mem_free(m->strings[i]);
        tilly_mem_free(m);
    }
}
static module *load(const uint8_t *code, size_t size, jt_error *e, bool preparing)
{
    if (e)
        memset(e, 0, sizeof(*e));
    if (!code || size < JT_HEADER || size > JOLT_MAX_PROGRAM_BYTES ||
        jolt_read_u32(code) != JOLT_BYTECODE_MAGIC || jolt_read_u32(code + 4) != 2)
        return NULL;
    uint32_t count = jolt_read_u32(code + 16), pool = jolt_read_u32(code + 20),
             root = jolt_read_u32(code + 24);
    if (count < 2 || count > JT_NODES || root != 1 || jolt_read_u32(code + 28) ||
        size != JT_HEADER + (size_t) count * JT_NODE + pool)
        return NULL;
    module *m = tilly_mem_calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->count = count;
    m->root = root;
    m->code = code;
    m->pool = (const char *) code + JT_HEADER + (size_t) count * JT_NODE;
    m->error = e;
    m->library = -1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *p = code + JT_HEADER + (size_t) i * JT_NODE;
        m->nodes[i] = (jt_node){jolt_read_u32(p),      jolt_read_u32(p + 4),  jolt_read_u32(p + 8),
                                jolt_read_u32(p + 12), jolt_read_u32(p + 16), jolt_read_u32(p + 20),
                                jolt_read_u32(p + 24)};
        jt_node n = m->nodes[i];
        if (n.child >= count || n.next >= count || n.text > pool || n.length > pool - n.text) {
            destroy(m);
            return NULL;
        }
    }
    uint32_t expected = 1;
    if (!tree(m, root, 0, &expected) || expected != count ||
        memcmp(&m->nodes[0], &(jt_node){0}, sizeof(jt_node)) || !utf8(m->pool, pool)) {
        destroy(m);
        return NULL;
    }
    for (uint32_t n = root; n && !m->status; n = next(m, n)) {
        uint32_t h = child(m, n);
        if (m->nodes[n].tag != 1) {
            fail(m, n, JOLT_ERR_SYNTAX, "expected defkernel or defn");
            break;
        }
        if (is(m, h, "import")) {
            std_import(m, n);
        } else if (is(m, h, "module")) {
            if (m->library >= 0 || n != root) {
                fail(m, n, JOLT_ERR_SYNTAX, "module must be the first and only module declaration");
                break;
            }
            for (unsigned i = 0; i < STD_COUNT; ++i)
                if (is(m, nth(m, n, 1), stdlib[i].module))
                    m->library = (int) stdlib[i].group;
            if (m->library < 0 || length(m, n) != 2)
                fail(m, n, JOLT_ERR_SYNTAX, "unknown library module");
        } else if (is(m, h, "intrinsic") || is(m, h, "export")) {
            if (m->library < 0)
                fail(m, n, JOLT_ERR_SYNTAX, "library declaration requires a module");
            else if (is(m, h, "intrinsic")) {
                int id = -1;
                for (unsigned i = 0; i < STD_COUNT; ++i)
                    if (stdlib[i].group == (unsigned) m->library &&
                        is(m, nth(m, n, 1), stdlib[i].name))
                        id = (int) i;
                uint32_t params = nth(m, n, 2);
                if (id < 0 || length(m, n) != 5 || !is(m, nth(m, n, 3), "->") ||
                    m->nodes[params].tag != 2)
                    fail(m, n, JOLT_ERR_SYNTAX, "unknown intrinsic declaration");
                else {
                    const std_entry *entry = &stdlib[id];
                    unsigned arity = 0, minimum = 0;
                    bool optional = false, variadic = false;
                    for (uint32_t p = child(m, params); p && !m->status; p = next(m, p)) {
                        if (is(m, p, "?") || is(m, p, "&")) {
                            if (optional) {
                                fail(m, p, JOLT_ERR_SYNTAX, "duplicate optional marker");
                                break;
                            }
                            optional = true;
                            variadic = is(m, p, "&");
                            continue;
                        }
                        if (arity >= entry->params ||
                            type_name(m, p) != (int) entry->types[arity]) {
                            fail(m, p, JOLT_ERR_TYPE, "intrinsic signature mismatch");
                            break;
                        }
                        ++arity;
                        if (!optional)
                            ++minimum;
                    }
                    if (arity != entry->params || minimum != entry->min ||
                        variadic != (entry->variadic != 0) ||
                        type_name(m, nth(m, n, 4)) != (int) entry->result)
                        fail(m, n, JOLT_ERR_TYPE, "intrinsic signature mismatch");
                }
            } else if (length(m, n) != 2 || !is(m, nth(m, n, 1), "*"))
                fail(m, n, JOLT_ERR_SYNTAX, "expected (export *)");
        } else if (is(m, h, "defn")) {
            if (m->library >= 0) {
                fail(m, n, JOLT_ERR_SYNTAX,
                     "standard-library interfaces contain declarations only");
                break;
            }
            uint32_t name = nth(m, n, 1);
            signature(m, n);
            bind(m, name, T_FN + n, false, 0);
            m->op[name] = O_FN;
            m->ref[name] = n;
        } else if (is(m, h, "defkernel") && !m->kernel && m->library < 0)
            m->kernel = n;
        else
            fail(m, n, JOLT_ERR_SYNTAX, "expected one defkernel and optional defn forms");
    }
    if (!m->kernel) {
        if (m->library < 0)
            fail(m, root, JOLT_ERR_SYNTAX, "expected defkernel");
        else if (!preparing && (jolt_read_u32(code + 8) || jolt_read_u32(code + 12)))
            fail(m, root, JOLT_ERR_BYTECODE, "library signature mismatch");
        return m;
    }
    m->globals_count = m->nb;
    for (uint32_t n = root; n && !m->status; n = next(m, n))
        if (is(m, child(m, n), "defn"))
            function(m, n, 0);
    if (!m->status) {
        uint32_t params = nth(m, m->kernel, 2);
        if (!name_ok(m, nth(m, m->kernel, 1)) || m->nodes[params].tag != 2 ||
            length(m, params) > JOLT_MAX_INPUTS)
            fail(m, m->kernel, JOLT_ERR_SYNTAX, "invalid kernel signature");
        unsigned saved = m->nb;
        for (uint32_t p = child(m, params); p && !m->status; p = next(m, p)) {
            uint32_t name;
            uint32_t t = parameter(m, p, &name);
            if (t == T_FUNCTION)
                fail(m, p, JOLT_ERR_TYPE, "kernel inputs must be data values");
            bind(m, name, t, false, saved);
            ++m->ni;
        }
        uint32_t body = next(m, params), annotation = UINT32_MAX;
        if (is(m, body, "->")) {
            int type = type_name(m, next(m, body));
            if (type < 0 || type == T_FUNCTION || type >= 100)
                fail(m, body, JOLT_ERR_TYPE, "unknown kernel return type");
            else
                annotation = (uint32_t) type;
            body = next(m, next(m, body));
        }
        m->ref[m->kernel] = body;
        uint32_t result = sequence(m, body, 0, saved);
        m->no = result == T_RGBA ? 4u : 1u;
        uint32_t final = body;
        while (next(m, final))
            final = next(m, final);
        for (uint32_t i = 1; i < m->count; ++i)
            if (m->type[i] == T_RGBA && (i != final || m->op[i] != O_RGBA))
                fail(m, i, JOLT_ERR_TYPE, "rgba is only allowed as the final kernel expression");
        if (result >= T_FN || result == T_FUNCTION || result == T_NEVER)
            fail(m, m->kernel, JOLT_ERR_TYPE, "kernel cannot return a function or loop control");
        if (annotation != UINT32_MAX) {
            if (!assignable(annotation, result))
                fail(m, m->kernel, JOLT_ERR_TYPE, "kernel return type mismatch");
            result = annotation;
        }
        m->ret[m->kernel] = result;
        if (!preparing && (m->ni != jolt_read_u32(code + 8) || m->no != jolt_read_u32(code + 12)))
            fail(m, m->kernel, JOLT_ERR_BYTECODE, "signature/header mismatch");
    }
    return m;
}
jolt_status_t jt_prepare(uint8_t *code, size_t size, jt_error *e)
{
    module *m = load(code, size, e, true);
    if (!m)
        return JOLT_ERR_BYTECODE;
    jolt_status_t s = m->status;
    if (!s) {
        jolt_write_u32(code + 8, m->ni);
        jolt_write_u32(code + 12, m->no);
    }
    destroy(m);
    return s;
}
jolt_status_t jt_check(const uint8_t *code, size_t size, jt_error *e)
{
    module *m = load(code, size, e, false);
    if (!m)
        return JOLT_ERR_BYTECODE;
    jolt_status_t s = m->status;
    destroy(m);
    return s;
}

/* Environments are persistent chains of mutable cells. Closures capture a chain
 * head, not a dynamic caller frame; all storage belongs to one bounded run. */
typedef struct object object;
typedef struct {
    jolt_value_t value;
    uint32_t fn, env;
    object *object;
} value;
struct object {
    size_t count;
    value items[];
};
typedef struct arena_chunk {
    struct arena_chunk *next;
    max_align_t align;
} arena_chunk;
typedef struct {
    uint32_t name, parent;
    value v;
} cell;
#define JT_CELLS 65536u
typedef struct {
    module *m;
    cell *cells;
    uint32_t used, globals;
    uint64_t steps;
    unsigned control;
    jolt_status_t status;
    jolt_value_t outputs[4];
    value aggregate_outputs[4];
    arena_chunk *arena;
    size_t heap_used;
} run;
static value zero(uint32_t t)
{
    value v = {0};
    v.value.type = (jolt_type_t) (t == T_ANY ? JOLT_TYPE_VOID : t);
    if (t == JOLT_TYPE_STRING)
        v.value.as.string.data = "";
    return v;
}
static bool truth(value v)
{
    return real(v.value) != 0;
}
static bool condition(run *r, value v)
{
    if (r->status || r->control)
        return false;
    if (v.fn || !truth_type((uint32_t) v.value.type)) {
        r->status = JOLT_ERR_TYPE;
        return false;
    }
    return truth(v);
}
static void *run_alloc(run *r, size_t size)
{
    if (r->status)
        return NULL;
    if (size > SIZE_MAX - sizeof(arena_chunk) ||
        size + sizeof(arena_chunk) > 16u * 1024u * 1024u - r->heap_used) {
        r->status = JOLT_ERR_BUDGET;
        return NULL;
    }
    arena_chunk *p = tilly_mem_alloc(sizeof(*p) + size);
    if (!p) {
        r->status = JOLT_ERR_MEMORY;
        return NULL;
    }
    p->next = r->arena;
    r->arena = p;
    r->heap_used += sizeof(*p) + size;
    return p + 1;
}
static bool charge(run *r, size_t count)
{
    if (count > r->steps) {
        r->status = JOLT_ERR_BUDGET;
        return false;
    }
    r->steps -= count;
    return !r->status;
}
static value collection(run *r, unsigned type, size_t count)
{
    value v = zero(type);
    if (count > 4096 || !charge(r, count)) {
        r->status = JOLT_ERR_BUDGET;
        return v;
    }
    v.object = run_alloc(r, sizeof(object) + count * sizeof(value));
    if (v.object) {
        v.object->count = count;
        memset(v.object->items, 0, count * sizeof(value));
    }
    return v;
}
static value number(run *r, double x)
{
    value v = zero(8);
    v.value.as.number = x;
    if (!isfinite(x))
        r->status = JOLT_ERR_NUMERIC;
    return v;
}
static value sint(int64_t x)
{
    value v = zero(2);
    v.value.as.integer = x;
    return v;
}
static value boolean(bool x)
{
    value v = zero(9);
    v.value.as.unsigned_integer = x;
    return v;
}
static value string_value(run *r, const char *p, size_t count)
{
    value v = zero(11);
    char *copy = run_alloc(r, count + 1);
    if (!copy)
        return v;
    if (count)
        memcpy(copy, p, count);
    copy[count] = 0;
    v.value.as.string.data = copy;
    v.value.as.string.size = count;
    return v;
}
static bool equal(run *r, value a, value b, unsigned depth)
{
    if (depth >= 64 || !charge(r, 1)) {
        r->status = JOLT_ERR_BUDGET;
        return false;
    }
    if (a.fn || b.fn)
        return a.fn == b.fn && a.env == b.env && a.object == b.object;
    if (a.value.type != b.value.type)
        return false;
    if (a.value.type >= 12 && a.value.type <= 15) {
        size_t na = a.object ? a.object->count : 0, nb = b.object ? b.object->count : 0;
        if (na != nb)
            return false;
        if (a.value.type == 14 || a.value.type == 15) {
            size_t stride = a.value.type == 14 ? 2u : 1u;
            for (size_t i = 0; i < na && !r->status; i += stride) {
                bool found = false;
                for (size_t j = 0; j < nb && !r->status; j += stride)
                    if (equal(r, a.object->items[i], b.object->items[j], depth + 1)) {
                        found = stride == 1 ||
                                equal(r, a.object->items[i + 1], b.object->items[j + 1], depth + 1);
                        break;
                    }
                if (!found)
                    return false;
            }
            return !r->status;
        }
        for (size_t i = 0; i < na; ++i)
            if (!equal(r, a.object->items[i], b.object->items[i], depth + 1))
                return false;
        return true;
    }
    if (a.value.type == 11)
        return a.value.as.string.size == b.value.as.string.size &&
               (!a.value.as.string.size ||
                !memcmp(a.value.as.string.data, b.value.as.string.data, a.value.as.string.size));
    if (a.value.type == 7 || a.value.type == 8)
        return a.value.as.number == b.value.as.number;
    return a.value.as.unsigned_integer == b.value.as.unsigned_integer;
}
static value convert(run *r, value v, uint32_t t)
{
    if (r->status || r->control || t == T_ANY || (t == T_FUNCTION && v.fn) ||
        (uint32_t) v.value.type == t)
        return v;
    if (v.fn || v.value.type >= 11 || t >= 11) {
        r->status = JOLT_ERR_TYPE;
        return v;
    }
    jolt_value_t a = v.value;
    v = zero(t);
    if (t == 0)
        return v;
    if (t == 9) {
        v.value.as.unsigned_integer = real(a) != 0;
        return v;
    }
    if (t == 7 || t == 8) {
        double x = real(a);
        if (t == 7)
            x = (double) (float) x;
        if (!isfinite(x))
            r->status = JOLT_ERR_NUMERIC;
        v.value.as.number = x;
        return v;
    }
    uint64_t bits;
    if (a.type == 7 || a.type == 8) {
        double x = trunc(a.as.number);
        double lower = signed_type(t) ? -ldexp(1.0, (int) width(t) - 1) : 0;
        double upper = ldexp(1.0, (int) width(t) - (signed_type(t) ? 1 : 0));
        if (!isfinite(x) || x < lower || x >= upper) {
            r->status = JOLT_ERR_NUMERIC;
            return v;
        }
        bits = signed_type(t) ? (uint64_t) (int64_t) x : (uint64_t) x;
    } else
        bits = signed_type(a.type) ? (uint64_t) a.as.integer : a.as.unsigned_integer;
    if (t == 10) {
        if (bits > 0x10ffffu || (bits >= 0xd800u && bits <= 0xdfffu))
            r->status = JOLT_ERR_NUMERIC;
        v.value.as.unsigned_integer = bits;
        return v;
    }
    if (signed_type(t))
        v.value.as.integer = signed_bits(bits, width(t));
    else
        v.value.as.unsigned_integer = bits & mask(t);
    return v;
}
static uint32_t push(run *r, uint32_t env, uint32_t name, value v)
{
    if (r->used + 1 >= JT_CELLS) {
        r->status = JOLT_ERR_BUDGET;
        return env;
    }
    uint32_t index = ++r->used;
    r->cells[index] = (cell){name, env, v};
    return index;
}
static cell *find(run *r, uint32_t env, uint32_t name)
{
    for (; env; env = r->cells[env].parent)
        if (r->cells[env].name == name)
            return &r->cells[env];
    r->status = JOLT_ERR_BYTECODE;
    return &r->cells[0];
}
static value eval(run *r, uint32_t n, uint32_t *env, unsigned depth);
static value invoke(run *r, value fn, const value *args, unsigned count, unsigned depth);
static value native(run *r, unsigned id, const value *args, unsigned count, unsigned depth);
static value sequence_next(run *r, value fn, unsigned depth);
static value eval_scope(run *r, uint32_t n, uint32_t env, unsigned depth)
{
    return eval(r, n, &env, depth);
}
static value eval_seq(run *r, uint32_t n, uint32_t *env, unsigned depth)
{
    value v = zero(0);
    for (; n && !r->status && !r->control; n = next(r->m, n))
        v = eval(r, n, env, depth);
    return v;
}
static value arithmetic(run *r, uint32_t n, value a, value b)
{
    module *m = r->m;
    uint32_t op = m->op[n], t = m->type[n];
    value out = zero(t);
    if (op == O_EQ || op == O_NE) {
        if (a.value.type >= 11 || b.value.type >= 11) {
            bool same = equal(r, a, b, 0);
            return boolean(op == O_EQ ? same : !same);
        }
    }
    if (t == T_ANY) {
        t = common(m, n, (uint32_t) a.value.type, (uint32_t) b.value.type);
        out = zero(t);
    }
    if (m->status) {
        r->status = m->status;
        return zero(0);
    }
    if ((op == O_POW || op == O_SQRT) && integer(t)) {
        t = 8;
        out = zero(t);
    }
    if (a.fn || b.fn) {
        r->status = JOLT_ERR_TYPE;
        return zero(0);
    }
    if (op >= O_BAND && op <= O_SHR && (!integer(a.value.type) || !integer(b.value.type))) {
        r->status = JOLT_ERR_TYPE;
        return zero(0);
    }
    if ((op != O_EQ && op != O_NE) &&
        (!truth_type((uint32_t) a.value.type) || !truth_type((uint32_t) b.value.type))) {
        r->status = JOLT_ERR_TYPE;
        return zero(0);
    }
    if (op == O_AND || op == O_OR || op == O_NOT) {
        out.value.as.unsigned_integer = op == O_AND  ? (truth(a) && truth(b))
                                        : op == O_OR ? (truth(a) || truth(b))
                                                     : !truth(a);
        return out;
    }
    if (a.value.type == 11) {
        bool eq = a.value.as.string.size == b.value.as.string.size &&
                  (!a.value.as.string.size ||
                   !memcmp(a.value.as.string.data, b.value.as.string.data, a.value.as.string.size));
        out.value.as.unsigned_integer = op == O_EQ ? eq : !eq;
        return out;
    }
    uint32_t ct = (op >= O_LT && op <= O_NE)
                      ? common(m, n, (uint32_t) a.value.type, (uint32_t) b.value.type)
                      : t;
    if (m->status) {
        r->status = m->status;
        return zero(0);
    }
    if (op == O_SHL || op == O_SHR)
        ct = (uint32_t) a.value.type;
    a = convert(r, a, ct);
    if (op != O_SHL && op != O_SHR)
        b = convert(r, b, ct);
    if (ct == 7 || ct == 8) {
        double x = a.value.as.number, y = b.value.as.number, z = 0;
        switch (op) {
        case O_ADD:
            z = x + y;
            break;
        case O_SUB:
            z = x - y;
            break;
        case O_MUL:
            z = x * y;
            break;
        case O_DIV:
            if (y == 0)
                r->status = JOLT_ERR_NUMERIC;
            else
                z = x / y;
            break;
        case O_MIN:
            z = fmin(x, y);
            break;
        case O_MAX:
            z = fmax(x, y);
            break;
        case O_ABS:
            z = fabs(x);
            break;
        case O_FLOOR:
            z = floor(x);
            break;
        case O_POW:
            z = pow(x, y);
            break;
        case O_SQRT:
            z = sqrt(x);
            break;
        case O_LT:
            z = x < y;
            break;
        case O_GT:
            z = x > y;
            break;
        case O_LE:
            z = x <= y;
            break;
        case O_GE:
            z = x >= y;
            break;
        case O_EQ:
            z = x == y;
            break;
        case O_NE:
            z = x != y;
            break;
        default:
            r->status = JOLT_ERR_BYTECODE;
            break;
        }
        if (t == 9)
            out.value.as.unsigned_integer = z != 0;
        else {
            if (t == 7)
                z = (double) (float) z;
            if (!isfinite(z))
                r->status = JOLT_ERR_NUMERIC;
            out.value.as.number = z;
        }
        return out;
    }
    uint64_t x = signed_type(ct) ? (uint64_t) a.value.as.integer : a.value.as.unsigned_integer;
    uint64_t y = signed_type(b.value.type) ? (uint64_t) b.value.as.integer
                                           : b.value.as.unsigned_integer,
             z = 0;
    bool less = signed_type(ct) ? a.value.as.integer < b.value.as.integer : x < y;
    bool equal = x == y;
    switch (op) {
    case O_ADD:
        z = x + y;
        break;
    case O_SUB:
        z = x - y;
        break;
    case O_MUL:
        z = x * y;
        break;
    case O_DIV:
        if (!y)
            r->status = JOLT_ERR_NUMERIC;
        else if (signed_type(ct)) {
            int64_t sx = a.value.as.integer, sy = b.value.as.integer;
            z = sx == INT64_MIN && sy == -1 ? (uint64_t) INT64_MIN : (uint64_t) (sx / sy);
        } else
            z = x / y;
        break;
    case O_MIN:
        z = less ? x : y;
        break;
    case O_MAX:
        z = less ? y : x;
        break;
    case O_ABS:
        z = signed_type(ct) && a.value.as.integer < 0 ? 0 - x : x;
        break;
    case O_FLOOR:
        z = x;
        break;
    case O_LT:
        z = less;
        break;
    case O_GT:
        z = !less && !equal;
        break;
    case O_LE:
        z = less || equal;
        break;
    case O_GE:
        z = !less;
        break;
    case O_EQ:
        z = equal;
        break;
    case O_NE:
        z = !equal;
        break;
    case O_BAND:
        z = x & y;
        break;
    case O_BOR:
        z = x | y;
        break;
    case O_BXOR:
        z = x ^ y;
        break;
    case O_SHL:
    case O_SHR:
        if (y >= width(ct))
            r->status = JOLT_ERR_NUMERIC;
        else if (op == O_SHL)
            z = x << y;
        else {
            z = (x & mask(ct)) >> y;
            if (signed_type(ct) && a.value.as.integer < 0 && y)
                z |= mask(ct) ^ (mask(ct) >> y);
        }
        break;
    default:
        r->status = JOLT_ERR_BYTECODE;
        break;
    }
    if (signed_type(t))
        out.value.as.integer = signed_bits(z, width(t));
    else
        out.value.as.unsigned_integer = t == 9 ? z : z & mask(t);
    return out;
}
static value eval(run *r, uint32_t n, uint32_t *env, unsigned depth)
{
    module *m = r->m;
    if (r->status || r->control)
        return zero(0);
    if (!r->steps || depth >= JT_DEPTH) {
        r->status = JOLT_ERR_BUDGET;
        return zero(0);
    }
    --r->steps;
    uint32_t op = m->op[n], a = nth(m, n, 1), b = nth(m, n, 2), c = nth(m, n, 3);
    value v = zero(m->type[n]);
    switch (op) {
    case O_VECTOR: {
        v = collection(r, 13, length(m, n));
        size_t i = 0;
        for (uint32_t p = child(m, n); p && !r->status; p = next(m, p))
            v.object->items[i++] = eval_scope(r, p, *env, depth + 1);
        return v;
    }
    case O_NATIVE_REF:
        v.fn = T_NATIVE + m->ref[n];
        return v;
    case O_NATIVE: {
        value args[32];
        unsigned count = 0;
        for (uint32_t p = a; p && !r->status && !r->control; p = next(m, p))
            args[count++] = eval_scope(r, p, *env, depth + 1);
        if (r->status || r->control)
            return zero(0);
        return native(r, m->ref[n], args, count, depth + 1);
    }
    case O_LITERAL:
        v.value = m->literals[n];
        return v;
    case O_LOAD:
        return find(r, *env, m->ref[n])->v;
    case O_FN:
        v.fn = n;
        v.env = *env;
        return v;
    case O_SEQ: {
        uint32_t local = *env;
        return eval_seq(r, a, &local, depth + 1);
    }
    case O_BIND:
        if (m->nodes[a].tag == 2) {
            uint32_t local = *env;
            for (uint32_t p = child(m, a); p && !r->status && !r->control;
                 p = next(m, next(m, p))) {
                value x = eval_scope(r, next(m, p), local, depth + 1);
                x = convert(r, x, m->type[p]);
                if (!r->status && !r->control)
                    local = push(r, local, m->ref[p], x);
            }
            return eval_seq(r, b, &local, depth + 1);
        }
        v = convert(r, eval_scope(r, b, *env, depth + 1), m->type[n]);
        if (!r->status && !r->control)
            *env = push(r, *env, m->ref[a], v);
        return v;
    case O_SET:
        v = convert(r, eval_scope(r, b, *env, depth + 1), m->type[n]);
        if (!r->status && !r->control)
            find(r, *env, m->ref[n])->v = v;
        return v;
    case O_IF: {
        bool selected = condition(r, eval_scope(r, a, *env, depth + 1));
        if (m->ref[n]) {
            value left = eval_scope(r, b, *env, depth + 1),
                  right = eval_scope(r, c, *env, depth + 1);
            v = selected ? left : right;
        } else
            v = eval_scope(r, selected ? b : c, *env, depth + 1);
        return convert(r, v, m->type[n]);
    }
    case O_COND:
        for (uint32_t clause = a; clause && !r->status && !r->control; clause = next(m, clause)) {
            uint32_t test = child(m, clause);
            if (is(m, test, "else") || condition(r, eval_scope(r, test, *env, depth + 1)))
                return convert(r, eval_scope(r, next(m, test), *env, depth + 1), m->type[n]);
        }
        return v;
    case O_WHEN:
    case O_UNLESS:
        if (condition(r, eval_scope(r, a, *env, depth + 1)) == (op == O_WHEN)) {
            uint32_t local = *env;
            eval_seq(r, b, &local, depth + 1);
        }
        return zero(0);
    case O_WHILE:
    case O_LOOP:
        while (!r->status) {
            if (!r->steps) {
                r->status = JOLT_ERR_BUDGET;
                break;
            }
            --r->steps;
            if (op == O_WHILE && !condition(r, eval_scope(r, a, *env, depth + 1)))
                break;
            uint32_t local = *env;
            eval_seq(r, op == O_LOOP ? a : b, &local, depth + 1);
            unsigned control = r->control;
            r->control = 0;
            if (control == 1)
                break;
        }
        return zero(0);
    case O_FOR: {
        uint32_t name = child(m, a), start = next(m, name), end = next(m, start),
                 step = next(m, end), t = m->type[name];
        value cur = convert(r, eval_scope(r, start, *env, depth + 1), t);
        value stop = convert(r, eval_scope(r, end, *env, depth + 1), t);
        value stride = zero(t);
        stride.value.as.unsigned_integer = 1;
        if (step)
            stride = convert(r, eval_scope(r, step, *env, depth + 1), t);
        if (r->status || r->control)
            return zero(0);
        bool negative = signed_type(t) && stride.value.as.integer < 0;
        if (!truth(stride))
            r->status = JOLT_ERR_NUMERIC;
        while (!r->status) {
            bool active = signed_type(t)
                              ? (negative ? cur.value.as.integer > stop.value.as.integer
                                          : cur.value.as.integer < stop.value.as.integer)
                              : cur.value.as.unsigned_integer < stop.value.as.unsigned_integer;
            if (!active)
                break;
            if (!r->steps) {
                r->status = JOLT_ERR_BUDGET;
                break;
            }
            --r->steps;
            uint32_t local = push(r, *env, name, cur);
            eval_seq(r, b, &local, depth + 1);
            unsigned control = r->control;
            r->control = 0;
            if (control == 1)
                break;
            /* Range induction never wraps; reaching the type boundary ends it. */
            if (signed_type(t)) {
                int64_t x = cur.value.as.integer, s = stride.value.as.integer;
                int64_t hi = width(t) == 64 ? INT64_MAX : INT32_MAX,
                        lo = width(t) == 64 ? INT64_MIN : INT32_MIN;
                if ((s > 0 && x > hi - s) || (s < 0 && x < lo - s))
                    break;
                cur.value.as.integer = x + s;
            } else {
                uint64_t x = cur.value.as.unsigned_integer, s = stride.value.as.unsigned_integer;
                if (x > mask(t) - s)
                    break;
                cur.value.as.unsigned_integer = x + s;
            }
        }
        return zero(0);
    }
    case O_BREAK:
        r->control = 1;
        return zero(0);
    case O_CONTINUE:
        r->control = 2;
        return zero(0);
    case O_CALL: {
        value fn = eval_scope(r, child(m, n), *env, depth + 1);
        value args[JOLT_MAX_INPUTS];
        unsigned count = 0;
        for (uint32_t arg = a; arg && !r->status; arg = next(m, arg))
            args[count++] = eval_scope(r, arg, *env, depth + 1);
        return invoke(r, fn, args, count, depth + 1);
    }
    case O_RGBA: {
        unsigned i = 0;
        for (uint32_t p = a; p && !r->status; p = next(m, p))
            r->aggregate_outputs[i++] = eval_scope(r, p, *env, depth + 1);
        return v;
    }
    case O_CAST:
        return convert(r, eval_scope(r, a, *env, depth + 1), m->type[n]);
    default: {
        value left = eval_scope(r, a, *env, depth + 1),
              right = b ? eval_scope(r, b, *env, depth + 1) : left;
        if (r->status || r->control)
            return zero(0);
        return arithmetic(r, n, left, right);
    }
    }
}
static value invoke(run *r, value fn, const value *args, unsigned count, unsigned depth)
{
    if (r->status || r->control)
        return zero(0);
    if (depth >= JT_DEPTH || !charge(r, 1)) {
        r->status = JOLT_ERR_BUDGET;
        return zero(0);
    }
    if (fn.fn >= T_NATIVE + STD_COUNT && fn.fn < T_NATIVE + STD_COUNT + 100) {
        if (count) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        return sequence_next(r, fn, depth + 1);
    }
    if (fn.fn >= T_NATIVE + STD_COUNT + 100) {
        uint32_t op = fn.fn - T_NATIVE - (uint32_t) STD_COUNT - 100;
        unsigned arity = (op == O_ABS || op == O_FLOOR || op == O_SQRT || op == O_NOT) ? 1u : 2u;
        if (count != arity) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        uint32_t saved_op = r->m->op[0], saved_type = r->m->type[0];
        r->m->op[0] = op;
        r->m->type[0] = op >= O_LT && op <= O_NOT ? 9 : T_ANY;
        value result = arithmetic(r, 0, args[0], args[arity - 1]);
        r->m->op[0] = saved_op;
        r->m->type[0] = saved_type;
        return result;
    }
    if (fn.fn >= T_NATIVE)
        return native(r, fn.fn - T_NATIVE, args, count, depth + 1);
    if (!fn.fn || count != length(r->m, fn_params(r->m, fn.fn))) {
        r->status = JOLT_ERR_TYPE;
        return zero(0);
    }
    module *m = r->m;
    uint32_t local = fn.env;
    unsigned i = 0;
    for (uint32_t p = child(m, fn_params(m, fn.fn)); p && !r->status; p = next(m, p)) {
        uint32_t name, t = parameter(m, p, &name);
        if (!assignable(t, args[i].fn ? T_FUNCTION : (uint32_t) args[i].value.type)) {
            r->status = JOLT_ERR_TYPE;
            break;
        }
        local = push(r, local, name, convert(r, args[i++], t));
    }
    return convert(r, eval_seq(r, fn_body(m, fn.fn), &local, depth + 1), m->ret[fn.fn]);
}

#include "stdlib_runtime.inc"
static bool valid_value(jolt_value_t v)
{
    uint32_t t = (uint32_t) v.type;
    if (t > 11)
        return false;
    if (t == 7 || t == 8)
        return isfinite(v.as.number) && (t != 7 || (double) (float) v.as.number == v.as.number);
    if (t == 11)
        return (!v.as.string.size || v.as.string.data) && v.as.string.size <= 1024u * 1024u &&
               utf8(v.as.string.data, v.as.string.size);
    if (t == 9)
        return v.as.unsigned_integer < 2;
    if (t == 10)
        return v.as.unsigned_integer <= 0x10ffffu &&
               !(v.as.unsigned_integer >= 0xd800u && v.as.unsigned_integer <= 0xdfffu);
    if (t == 1)
        return v.as.integer >= INT32_MIN && v.as.integer <= INT32_MAX;
    return !integer(t) || signed_type(t) || v.as.unsigned_integer <= mask(t);
}
static value unpack(run *r, jolt_value_t in, unsigned depth)
{
    value v = zero((uint32_t) in.type);
    v.value = in;
    if (depth >= 64) {
        r->status = JOLT_ERR_BUDGET;
        return v;
    }
    if (in.type >= 12 && in.type <= 15) {
        size_t count = in.as.collection.size;
        if ((count && !in.as.collection.items) || (in.type == 14 && count % 2)) {
            r->status = JOLT_ERR_TYPE;
            return v;
        }
        v = collection(r, (unsigned) in.type, count);
        for (size_t i = 0; i < count && !r->status; ++i)
            v.object->items[i] = unpack(r, in.as.collection.items[i], depth + 1);
    } else if (!valid_value(in))
        r->status = JOLT_ERR_TYPE;
    else if (in.type == 11 && !in.as.string.size)
        v.value.as.string.data = "";
    return v;
}
static size_t pack_size(run *r, value v, unsigned depth)
{
    if (depth >= 64 || !charge(r, 1)) {
        r->status = JOLT_ERR_BUDGET;
        return 0;
    }
    if (v.fn || v.value.type > JOLT_TYPE_SET) {
        r->status = JOLT_ERR_TYPE;
        return 0;
    }
    if (v.value.type == 11)
        return (v.value.as.string.size + 1 + 7u) & ~(size_t) 7u;
    if (v.value.type >= 12 && v.value.type <= 15) {
        size_t count = v.object ? v.object->count : 0, total = count * sizeof(jolt_value_t);
        for (size_t i = 0; i < count && !r->status; ++i)
            total += pack_size(r, v.object->items[i], depth + 1);
        if (total > 16u * 1024u * 1024u)
            r->status = JOLT_ERR_BUDGET;
        return total ? total : 8;
    }
    return 0;
}
static jolt_value_t pack(value v, char **cursor)
{
    jolt_value_t out = v.value;
    if (out.type == 11) {
        size_t count = out.as.string.size;
        if (count)
            memcpy(*cursor, out.as.string.data, count);
        (*cursor)[count] = 0;
        out.as.string.data = *cursor;
        *cursor += (count + 1 + 7u) & ~(size_t) 7u;
    } else if (out.type >= 12 && out.type <= 15) {
        size_t count = v.object ? v.object->count : 0;
        jolt_value_t *items = (jolt_value_t *) *cursor;
        *cursor += count ? count * sizeof(*items) : 8;
        out.as.collection.items = items;
        out.as.collection.size = count;
        for (size_t i = 0; i < count; ++i)
            items[i] = pack(v.object->items[i], cursor);
    }
    return out;
}
/* Returned strings are copied for the caller to own, never views into load(). */
jolt_status_t jt_eval(const uint8_t *code, size_t size, const jolt_value_t *inputs, size_t ni,
                      jolt_value_t *outputs, size_t no, uint64_t steps)
{
    module *m = load(code, size, NULL, false);
    if (!m)
        return JOLT_ERR_BYTECODE;
    if (m->status) {
        jolt_status_t s = m->status;
        destroy(m);
        return s;
    }
    if (!m->kernel || ni != m->ni || no != m->no || !outputs || (ni && !inputs) ||
        steps > 10000000) {
        destroy(m);
        return JOLT_ERR_ARGUMENT;
    }
    run r = {0};
    r.m = m;
    r.steps = steps ? steps : 100000;
    r.cells = tilly_mem_calloc(JT_CELLS, sizeof(cell));
    if (!r.cells) {
        destroy(m);
        return JOLT_ERR_MEMORY;
    }
    for (uint32_t n = m->root; n; n = next(m, n))
        if (is(m, child(m, n), "defn")) {
            value v = zero(T_FN + n);
            v.fn = n;
            r.globals = push(&r, r.globals, nth(m, n, 1), v);
        }
    for (uint32_t i = 1; i <= r.used; ++i)
        r.cells[i].v.env = r.globals;
    uint32_t env = r.globals;
    size_t index = 0;
    for (uint32_t p = child(m, nth(m, m->kernel, 2)); p; p = next(m, p)) {
        uint32_t name, t = parameter(m, p, &name);
        value v = unpack(&r, inputs[index++], 0);
        if (r.status || !assignable(t, (uint32_t) v.value.type)) {
            r.status = JOLT_ERR_TYPE;
            break;
        }
        env = push(&r, env, name, convert(&r, v, t));
    }
    value result = eval_seq(&r, m->ref[m->kernel], &env, 0);
    if (no == 1)
        r.aggregate_outputs[0] = convert(&r, result, m->ret[m->kernel]);
    char *copies[4] = {0};
    for (size_t i = 0; i < no && !r.status; ++i) {
        size_t size_needed = pack_size(&r, r.aggregate_outputs[i], 0);
        if (r.status)
            break;
        if (size_needed) {
            copies[i] = tilly_mem_alloc(size_needed);
            if (!copies[i]) {
                r.status = JOLT_ERR_MEMORY;
                break;
            }
        }
        char *cursor = copies[i];
        r.outputs[i] = pack(r.aggregate_outputs[i], &cursor);
    }
    if (!r.status)
        memcpy(outputs, r.outputs, no * sizeof(*outputs));
    else
        for (size_t i = 0; i < no; ++i)
            tilly_mem_free(copies[i]);
    while (r.arena) {
        arena_chunk *p = r.arena;
        r.arena = p->next;
        tilly_mem_free(p);
    }
    tilly_mem_free(r.cells);
    destroy(m);
    return r.status;
}
jolt_status_t jt_run_float(const uint8_t *code, size_t size, const float *inputs, size_t ni,
                           float *outputs, size_t no)
{
    if (ni > 32 || no > 4 || !outputs || (ni && !inputs))
        return JOLT_ERR_ARGUMENT;
    jolt_value_t in[32], out[4];
    float results[4];
    jolt_type_t types[32], out_types[4];
    jolt_status_t signature_status = jolt_bytecode_signature(code, size, types, ni, out_types, no);
    if (signature_status)
        return signature_status;
    for (size_t i = 0; i < ni; ++i) {
        if (types[i] != JOLT_TYPE_F32 && types[i] != JOLT_TYPE_F64)
            return JOLT_ERR_TYPE;
        in[i] = (jolt_value_t){.type = types[i]};
        in[i].as.number = inputs[i];
    }
    jolt_status_t s = jt_eval(code, size, in, ni, out, no, 0);
    if (s)
        return s;
    for (size_t i = 0; i < no; ++i) {
        if (out[i].type >= JOLT_TYPE_STRING) {
            tilly_mem_free((void *) out[i].as.string.data);
            s = JOLT_ERR_TYPE;
        } else {
            results[i] = (float) real(out[i]);
            if (!isfinite(results[i]))
                s = JOLT_ERR_NUMERIC;
        }
    }
    if (!s)
        memcpy(outputs, results, no * sizeof(float));
    return s;
}
jolt_status_t jolt_bytecode_signature(const uint8_t *code, size_t size, jolt_type_t *inputs,
                                      size_t ni, jolt_type_t *outputs, size_t no)
{
    if (!code || size < 16 || !outputs)
        return JOLT_ERR_ARGUMENT;
    if (jolt_read_u32(code + 4) == 1) {
#ifndef JOLT_TYPED_HOST
        jolt_status_t valid = jolt_bytecode_validate(code, size);
        if (valid)
            return valid;
#else
        return JOLT_ERR_BYTECODE; /* private Rust bridge handles typed programs only */
#endif
        if (ni < jolt_read_u32(code + 8) || no < jolt_read_u32(code + 12) ||
            (jolt_read_u32(code + 8) && !inputs))
            return JOLT_ERR_ARGUMENT;
        /* Version-1 validation is performed by the public VM entry point. */
        for (size_t i = 0; i < jolt_read_u32(code + 8); ++i)
            inputs[i] = JOLT_TYPE_F32;
        for (size_t i = 0; i < jolt_read_u32(code + 12); ++i)
            outputs[i] = JOLT_TYPE_F32;
        return JOLT_OK;
    }
    module *m = load(code, size, NULL, false);
    if (!m)
        return JOLT_ERR_BYTECODE;
    jolt_status_t s = m->status;
    if (!s && (ni < m->ni || no < m->no || (m->ni && !inputs)))
        s = JOLT_ERR_ARGUMENT;
    if (!s) {
        size_t i = 0;
        for (uint32_t p = child(m, nth(m, m->kernel, 2)); p; p = next(m, p)) {
            uint32_t name;
            inputs[i++] = (jolt_type_t) parameter(m, p, &name);
        }
        if (m->no == 1)
            outputs[0] = (jolt_type_t) m->ret[m->kernel];
        else {
            uint32_t body = m->ref[m->kernel];
            while (next(m, body))
                body = next(m, body);
            i = 0;
            for (uint32_t p = nth(m, body, 1); p; p = next(m, p))
                outputs[i++] = (jolt_type_t) m->type[p];
        }
    }
    destroy(m);
    return s;
}
