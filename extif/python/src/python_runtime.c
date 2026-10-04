#include "jfx/python.h"
#include "runtime_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "py/compile.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mpstate.h"
#include "py/objint.h"
#include "py/objlist.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "tilly/logger.h"

typedef struct {
    jfx_script_runtime_t base;
    mp_state_ctx_t saved;
    void *heap;
    uint32_t generations[JFX_SCRIPT_REFS];
    bool used[JFX_SCRIPT_REFS];
} python_runtime_t;
typedef struct { mp_obj_base_t base; jfx_value_type_t type; uint64_t token; } python_wrapped_t;
typedef struct {
    jfx_script_runtime_t *rt;
    const void *source;
    size_t length, argc;
    const char *name;
    const jfx_value_t *args;
    jfx_value_t *out;
    jfx_script_value_t ref, *out_ref;
    int operation;
    jfx_script_status_t status;
} python_job_t;
static python_runtime_t *current;
static python_runtime_t *as_python(jfx_script_runtime_t *rt) { return (python_runtime_t *)rt; }
static const mp_obj_type_t wrapped_type;
/* ASan may move address-taken locals to a fake stack. A native frame address
 * keeps MicroPython's descending-C-stack recursion check on the real stack. */
#if defined(__GNUC__) || defined(__clang__)
#define SET_STACK_TOP() mp_stack_set_top(__builtin_frame_address(0))
#else
#define SET_STACK_TOP() volatile int anchor = 0; mp_stack_set_top((void *)&anchor)
#endif

/* This port collects only between host invocations. All surviving objects are
 * rooted in mp_state_ctx (globals and explicit references); no borrowed C stack
 * needs conservative scanning, including sanitizer red zones. */
void gc_collect(void) { gc_collect_start(); gc_collect_end(); }
void jfx_python_vm_hook(void) {
    if (current && !jfx_script_tick(&current->base, 1))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("instruction budget exceeded"));
}
void nlr_jump_fail(void *value) { (void)value; tillyz_default_panic("uncaught MicroPython exception", __FILE__, __LINE__); abort(); }
mp_uint_t mp_hal_stdout_tx_strn(const char *text, size_t length) {
    if (current) { char message[512]; size_t n = length < sizeof(message) - 1 ? length : sizeof(message) - 1;
        memcpy(message, text, n); message[n] = 0; tilly_log_simple(TILLY_LOG_INFO, "%s", message); }
    return (mp_uint_t)length;
}
void mp_hal_stdout_tx_strn_cooked(const char *text, size_t length) { (void)mp_hal_stdout_tx_strn(text, length); }
static int ref_slot(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    size_t slot = (size_t)(ref & 255u);
    if (!slot || slot > JFX_SCRIPT_REFS) return -1;
    python_runtime_t *py = as_python(rt);
    return py->used[slot - 1] && jfx_script_ref_matches(rt, ref, py->generations[slot - 1]) ? (int)(slot - 1) : -1;
}
static jfx_script_status_t pin(jfx_script_runtime_t *rt, mp_obj_t object, jfx_script_value_t *out) {
    python_runtime_t *py = as_python(rt);
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) {
        if (py->used[i]) continue;
        py->used[i] = true; MP_STATE_VM(jfx_refs)[i] = object;
        if (++py->generations[i] == 0) ++py->generations[i];
        *out = jfx_script_ref_token(rt, py->generations[i], i); return JFX_SCRIPT_OK;
    }
    return jfx_script_fail(rt, JFX_SCRIPT_OUT_OF_MEMORY, "script reference table is full");
}
static void release(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    int slot = ref_slot(rt, ref);
    if (slot < 0) return;
    python_runtime_t *py = as_python(rt); py->used[slot] = false;
    if (current == py) MP_STATE_VM(jfx_refs)[slot] = MP_OBJ_NULL;
    else py->saved.vm.jfx_refs[slot] = MP_OBJ_NULL;
}
static mp_obj_t ref_value(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    int slot = ref_slot(rt, ref);
    if (slot < 0) { (void)jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "invalid script reference"); mp_raise_TypeError(MP_ERROR_TEXT("invalid script reference")); }
    return MP_STATE_VM(jfx_refs)[slot];
}
static void wrapped_print(const mp_print_t *print, mp_obj_t self, mp_print_kind_t kind) {
    (void)self; (void)kind; mp_print_str(print, "jfx.Value");
}
/* MicroPython's upstream type-slot macro uses a flexible-array initializer and
 * function pointers in void slots. Keep the exception local to that macro. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
static MP_DEFINE_CONST_OBJ_TYPE(wrapped_type, MP_QSTR_Value, MP_TYPE_FLAG_NONE, print, wrapped_print);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
static mp_obj_t from_value(jfx_script_runtime_t *rt, const jfx_value_t *v) {
    switch (v->type) {
        case JFX_TYPE_NIL: return mp_const_none;
        case JFX_TYPE_BOOL: return mp_obj_new_bool(v->b);
        case JFX_TYPE_INT: return mp_obj_new_int_from_ll((long long)v->i);
        case JFX_TYPE_FLOAT: return mp_obj_new_float(v->f);
        case JFX_TYPE_STRING: return mp_obj_new_str(v->str, strlen(v->str));
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4: {
            size_t n = (size_t)(v->type - JFX_TYPE_VEC2 + 2); mp_obj_t array[4];
            for (size_t i = 0; i < n; ++i) array[i] = mp_obj_new_float(v->vec4[i]);
            return mp_obj_new_list(n, array);
        }
        case JFX_TYPE_COLOR: case JFX_TYPE_BUFFER: case JFX_TYPE_TEXTURE: case JFX_TYPE_USERDATA: {
            uint64_t token = 0;
            if (v->type == JFX_TYPE_COLOR) token = v->color;
            else if (jfx_script_wrap(rt, v, &token)) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("too many resources"));
            python_wrapped_t *wrapped = mp_obj_malloc(python_wrapped_t, &wrapped_type);
            wrapped->type = v->type; wrapped->token = token; return MP_OBJ_FROM_PTR(wrapped);
        }
        case JFX_TYPE_FUNCTION: return ref_value(rt, v->function);
        default: mp_raise_TypeError(MP_ERROR_TEXT("unsupported native value"));
    }
}
static jfx_script_status_t to_value(jfx_script_runtime_t *rt, mp_obj_t value, bool copy, jfx_value_t *out) {
    *out = (jfx_value_t){0};
    if (value == mp_const_none) return JFX_SCRIPT_OK;
    if (value == mp_const_true || value == mp_const_false) { out->type = JFX_TYPE_BOOL; out->b = value == mp_const_true; return JFX_SCRIPT_OK; }
    if (mp_obj_is_integer(value)) {
        out->type = JFX_TYPE_INT;
        if (mp_obj_is_small_int(value)) out->i = (int64_t)MP_OBJ_SMALL_INT_VALUE(value);
        else {
            byte bytes[8];
            if (!mp_obj_int_to_bytes_impl(value, false, sizeof(bytes), bytes)) return JFX_SCRIPT_TYPE_ERROR;
            uint64_t integer = 0;
            for (size_t i = 0; i < 8; ++i) integer |= (uint64_t)bytes[i] << (i * 8);
            memcpy(&out->i, &integer, sizeof(integer));
            if (!mp_obj_equal(value, mp_obj_new_int_from_ll((long long)out->i))) return JFX_SCRIPT_TYPE_ERROR;
        }
        return JFX_SCRIPT_OK;
    }
    if (mp_obj_is_float(value)) { out->type = JFX_TYPE_FLOAT; out->f = mp_obj_get_float(value); return isfinite(out->f) ? JFX_SCRIPT_OK : JFX_SCRIPT_TYPE_ERROR; }
    if (mp_obj_is_str(value)) {
        size_t length = 0; const char *text = mp_obj_str_get_data(value, &length);
        if (copy) return jfx_script_copy_string(rt, text, length, out);
        if (memchr(text, 0, length)) return JFX_SCRIPT_TYPE_ERROR;
        out->type = JFX_TYPE_STRING; out->str = text; return JFX_SCRIPT_OK;
    }
    if (mp_obj_is_type(value, &wrapped_type)) {
        python_wrapped_t *wrapped = MP_OBJ_TO_PTR(value);
        if (wrapped->type == JFX_TYPE_COLOR) { out->type = JFX_TYPE_COLOR; out->color = (uint32_t)wrapped->token; return JFX_SCRIPT_OK; }
        return jfx_script_unwrap(rt, wrapped->token, out);
    }
    if (mp_obj_is_callable(value)) { out->type = JFX_TYPE_FUNCTION; return pin(rt, value, &out->function); }
    if (mp_obj_is_type(value, &mp_type_list) || mp_obj_is_type(value, &mp_type_tuple)) {
        size_t n = 0; mp_obj_t *array = NULL; mp_obj_get_array(value, &n, &array);
        if (n < 2 || n > 4) return JFX_SCRIPT_TYPE_ERROR;
        out->type = (jfx_value_type_t)(JFX_TYPE_VEC2 + n - 2);
        for (size_t i = 0; i < n; ++i) {
            if (!mp_obj_is_integer(array[i]) && !mp_obj_is_float(array[i])) return JFX_SCRIPT_TYPE_ERROR;
            double f = mp_obj_get_float(array[i]);
            if (!isfinite(f) || fabs(f) > 3.402823466e38) return JFX_SCRIPT_TYPE_ERROR;
            out->vec4[i] = (float)f;
        }
        return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_TYPE_ERROR;
}
static mp_obj_t host_function(size_t argc, const mp_obj_t *argv, size_t magic) {
    jfx_script_runtime_t *rt = &current->base;
    if (argc > JFX_SCRIPT_MAX_ARGS) mp_raise_TypeError(MP_ERROR_TEXT("too many host arguments"));
    jfx_value_t args[JFX_SCRIPT_MAX_ARGS] = {{0}}, result = {0};
    jfx_script_status_t status = JFX_SCRIPT_OK;
    for (size_t i = 0; i < argc; ++i) { status = to_value(rt, argv[i], false, &args[i]); if (status) break; }
    if (!status) status = jfx_script_host_call(rt, jfx_script_host_names[magic], args, argc, &result);
    for (size_t i = 0; i < argc; ++i) if (args[i].type == JFX_TYPE_FUNCTION) release(rt, args[i].function);
    if (status) {
        if (!rt->error[0]) (void)jfx_script_fail(rt, status, "invalid or unavailable jfx host operation");
        else if (!rt->fault) rt->fault = status;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jfx host operation failed"));
    }
    return from_value(rt, &result);
}
#define HOST_WRAPPER(name, magic) \
    static mp_obj_t host_##name(size_t argc, const mp_obj_t *argv) { return host_function(argc, argv, magic); } \
    static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(host_##name##_obj, 0, JFX_SCRIPT_MAX_ARGS, host_##name)
HOST_WRAPPER(clamp, 0); HOST_WRAPPER(log, 1); HOST_WRAPPER(command, 2); HOST_WRAPPER(state, 3);
HOST_WRAPPER(on, 4); HOST_WRAPPER(off, 5); HOST_WRAPPER(register_kernel, 6); HOST_WRAPPER(size, 7);
HOST_WRAPPER(read, 8); HOST_WRAPPER(write, 9); HOST_WRAPPER(dimensions, 10); HOST_WRAPPER(sample, 11);
HOST_WRAPPER(write_pixel, 12); HOST_WRAPPER(color, 13);
#define HOST_ENTRY(name) { MP_ROM_QSTR(MP_QSTR_##name), MP_ROM_PTR(&host_##name##_obj) }
static const mp_rom_map_elem_t module_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_jfx) },
    HOST_ENTRY(clamp), HOST_ENTRY(log), HOST_ENTRY(command), HOST_ENTRY(state), HOST_ENTRY(on),
    HOST_ENTRY(off), HOST_ENTRY(register_kernel), HOST_ENTRY(size), HOST_ENTRY(read), HOST_ENTRY(write),
    HOST_ENTRY(dimensions), HOST_ENTRY(sample), HOST_ENTRY(write_pixel), HOST_ENTRY(color)
};
static MP_DEFINE_CONST_DICT(module_globals, module_table);
const mp_obj_module_t jfx_python_module = { .base = { &mp_type_module }, .globals = (mp_obj_dict_t *)&module_globals };

static void print_error(void *user, const char *text, size_t length) {
    jfx_script_runtime_t *rt = user; size_t offset = strlen(rt->error);
    size_t available = sizeof(rt->error) - offset - 1;
    if (length > available) length = available;
    memcpy(rt->error + offset, text, length); rt->error[offset + length] = 0;
}
static jfx_script_status_t run(python_job_t *job) {
    python_runtime_t *py = as_python(job->rt);
    bool nested = current == py;
    if (current && !nested) return JFX_SCRIPT_BUSY;
    if (!nested) { current = py; mp_state_ctx = py->saved; }
    char *previous_stack_top = MP_STATE_THREAD(stack_top);
    size_t previous_stack_limit = MP_STATE_THREAD(stack_limit);
    SET_STACK_TOP(); mp_stack_set_limit(128u * 1024u);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        job->status = JFX_SCRIPT_OK;
        switch (job->operation) {
            case 0: {
                mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(job->name), job->source, job->length, 0);
                qstr name = lex->source_name;
                mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
                mp_obj_t function = mp_compile(&tree, name, false); (void)mp_call_function_0(function); break;
            }
            case 1: {
                mp_obj_t name = MP_OBJ_NEW_QSTR(qstr_from_str(job->name));
                mp_map_elem_t *entry = mp_map_lookup(&mp_globals_get()->map, name, MP_MAP_LOOKUP);
                job->status = entry && mp_obj_is_callable(entry->value) ? pin(job->rt, entry->value, job->out_ref) : JFX_SCRIPT_NOT_FOUND;
                break;
            }
            case 2: {
                mp_obj_t argv[JFX_SCRIPT_MAX_ARGS];
                for (size_t i = 0; i < job->argc; ++i) argv[i] = from_value(job->rt, &job->args[i]);
                mp_obj_t result = mp_call_function_n_kw(ref_value(job->rt, job->ref), job->argc, 0, argv);
                job->status = to_value(job->rt, result, true, job->out); break;
            }
            case 3: job->status = pin(job->rt, from_value(job->rt, job->args), job->out_ref); break;
            case 4: job->status = to_value(job->rt, ref_value(job->rt, job->ref), true, job->out); break;
            case 5: job->status = pin(job->rt, ref_value(job->rt, job->ref), job->out_ref); break;
            default: job->status = JFX_SCRIPT_INVALID_ARGUMENT; break;
        }
        nlr_pop();
    } else {
        mp_obj_t exception = MP_OBJ_FROM_PTR(nlr.ret_val);
        job->status = job->rt->fault ? job->rt->fault :
            mp_obj_exception_match(exception, MP_OBJ_FROM_PTR(&mp_type_MemoryError)) ? JFX_SCRIPT_OUT_OF_MEMORY : JFX_SCRIPT_ERROR;
        mp_print_t print = { job->rt, print_error };
        if (job->rt->error[0]) print_error(job->rt, "\n", 1);
        mp_obj_print_exception(&print, exception);
    }
    /* Collection happens at the host boundary, not inside a running script. */
    if (!nested) {
        if (!job->rt->gc_paused) gc_collect();
        py->saved = mp_state_ctx; current = NULL;
    } else {
        MP_STATE_THREAD(stack_top) = previous_stack_top;
        MP_STATE_THREAD(stack_limit) = previous_stack_limit;
    }
    return job->status;
}
static jfx_script_status_t load(jfx_script_runtime_t *rt, const void *s, size_t n, const char *name) {
    python_job_t job = { .rt = rt, .source = s, .length = n, .name = name }; return run(&job);
}
static jfx_script_status_t capture(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t *out) {
    python_job_t job = { .rt = rt, .operation = 1, .name = name, .out_ref = out }; return run(&job);
}
static jfx_script_status_t call(jfx_script_runtime_t *rt, jfx_script_value_t ref, const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    python_job_t job = { .rt = rt, .operation = 2, .ref = ref, .args = args, .argc = argc, .out = out }; return run(&job);
}
static jfx_script_status_t from(jfx_script_runtime_t *rt, const jfx_value_t *v, jfx_script_value_t *out) {
    python_job_t job = { .rt = rt, .operation = 3, .args = v, .out_ref = out }; return run(&job);
}
static jfx_script_status_t to(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_value_t *out) {
    python_job_t job = { .rt = rt, .operation = 4, .ref = ref, .out = out }; return run(&job);
}
static jfx_script_status_t clone(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_script_value_t *out) {
    python_job_t job = { .rt = rt, .operation = 5, .ref = ref, .out_ref = out }; return run(&job);
}
static void gc(jfx_script_runtime_t *rt, int mode) {
    if (mode || current) return;
    python_runtime_t *py = as_python(rt); current = py; mp_state_ctx = py->saved;
    gc_collect(); py->saved = mp_state_ctx; current = NULL;
}
static void destroy(jfx_script_runtime_t *rt) {
    python_runtime_t *py = as_python(rt);
    if (py->heap) {
        current = py; mp_state_ctx = py->saved; mp_deinit(); current = NULL;
        (void)jfx_script_alloc(rt, py->heap, 0);
    }
    jfx_script_base_free(rt);
}
jfx_script_status_t jfx_python_script_create(const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    static const jfx_script_ops_t ops = { destroy, load, capture, call, from, to, clone, release, gc };
    if (current) return JFX_SCRIPT_BUSY;
    jfx_script_status_t status = jfx_script_base_create(sizeof(python_runtime_t), desc, JFX_SCRIPT_PYTHON, &ops, out);
    if (status) return status;
    python_runtime_t *py = as_python(*out);
    size_t available = py->base.memory_limit - py->base.memory_used;
    /* Leave space for result strings and shared-host marshaling. */
    size_t heap_size = available - available / 4;
    if (heap_size < 32768 || !(py->heap = jfx_script_alloc(*out, NULL, heap_size))) {
        jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY;
    }
    current = py; memset(&mp_state_ctx, 0, sizeof(mp_state_ctx));
    SET_STACK_TOP(); mp_stack_set_limit(128u * 1024u);
    gc_init(py->heap, (uint8_t *)py->heap + heap_size);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) { mp_init(); MP_STATE_MEM(gc_auto_collect_enabled) = false; nlr_pop(); }
    else {
        current = NULL; (void)jfx_script_alloc(*out, py->heap, 0);
        jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY;
    }
    py->saved = mp_state_ctx; current = NULL; return JFX_SCRIPT_OK;
}
