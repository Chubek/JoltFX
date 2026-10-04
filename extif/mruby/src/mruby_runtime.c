#include "jfx/mruby.h"
#include "runtime_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/compile.h"
#include "mruby/data.h"
#include "mruby/error.h"
#include "mruby/internal.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/variable.h"

typedef struct { mrb_value value; uint32_t generation; bool used; } ruby_ref_t;
struct jfx_mruby_runtime {
    jfx_script_runtime_t base;
    mrb_state *state;
    struct RClass *value_class;
    mrb_value reference_roots;
    ruby_ref_t refs[JFX_SCRIPT_REFS];
};
typedef struct { jfx_script_runtime_t *rt; jfx_value_type_t type; uint64_t token; } ruby_wrapped_t;
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
    mrbc_context *context;
} ruby_job_t;
static struct jfx_mruby_runtime *as_ruby(jfx_script_runtime_t *rt) { return (struct jfx_mruby_runtime *)rt; }
static void *allocator(mrb_state *mrb, void *ptr, size_t size, void *user) {
    (void)mrb; return jfx_script_alloc(user, ptr, size);
}
static int ref_slot(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    size_t slot = (size_t)(ref & 255u);
    if (!slot || slot > JFX_SCRIPT_REFS) return -1;
    ruby_ref_t *entry = &as_ruby(rt)->refs[slot - 1];
    return entry->used && jfx_script_ref_matches(rt, ref, entry->generation) ? (int)(slot - 1) : -1;
}
static jfx_script_status_t pin(mrb_state *mrb, mrb_value value, jfx_script_value_t *out) {
    jfx_script_runtime_t *rt = mrb->allocf_ud;
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) {
        ruby_ref_t *ref = &as_ruby(rt)->refs[i];
        if (ref->used) continue;
        mrb_ary_set(mrb, as_ruby(rt)->reference_roots, (mrb_int)i, value);
        ref->used = true; ref->value = value;
        if (++ref->generation == 0) ++ref->generation;
        *out = jfx_script_ref_token(rt, ref->generation, i); return JFX_SCRIPT_OK;
    }
    return jfx_script_fail(rt, JFX_SCRIPT_OUT_OF_MEMORY, "script reference table is full");
}
static void release(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    int slot = ref_slot(rt, ref);
    if (slot < 0) return;
    ruby_ref_t *entry = &as_ruby(rt)->refs[slot];
    mrb_ary_set(as_ruby(rt)->state, as_ruby(rt)->reference_roots, (mrb_int)slot, mrb_nil_value());
    entry->value = mrb_nil_value(); entry->used = false;
}
static mrb_value ref_value(mrb_state *mrb, jfx_script_value_t ref) {
    jfx_script_runtime_t *rt = mrb->allocf_ud;
    int slot = ref_slot(rt, ref);
    if (slot < 0) { (void)jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "invalid script reference"); mrb_raise(mrb, E_TYPE_ERROR, "invalid script reference"); }
    return as_ruby(rt)->refs[slot].value;
}
static void wrapped_free(mrb_state *mrb, void *ptr) {
    if (ptr) (void)jfx_script_alloc(mrb->allocf_ud, ptr, 0);
}
static const mrb_data_type wrapped_type = { "jfx.Value", wrapped_free };
static mrb_value from_value(mrb_state *mrb, const jfx_value_t *v) {
    jfx_script_runtime_t *rt = mrb->allocf_ud;
    switch (v->type) {
        case JFX_TYPE_NIL: return mrb_nil_value();
        case JFX_TYPE_BOOL: return mrb_bool_value(v->b);
        case JFX_TYPE_INT: return mrb_int_value(mrb, (mrb_int)v->i);
        case JFX_TYPE_FLOAT: return mrb_float_value(mrb, v->f);
        case JFX_TYPE_STRING: return mrb_str_new_cstr(mrb, v->str);
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4: {
            mrb_int n = v->type - JFX_TYPE_VEC2 + 2;
            mrb_value array = mrb_ary_new_capa(mrb, n);
            for (mrb_int i = 0; i < n; ++i) mrb_ary_push(mrb, array, mrb_float_value(mrb, v->vec4[i]));
            return array;
        }
        case JFX_TYPE_COLOR: case JFX_TYPE_BUFFER: case JFX_TYPE_TEXTURE: case JFX_TYPE_USERDATA: {
            uint64_t token = 0;
            if (v->type == JFX_TYPE_COLOR) token = v->color;
            else if (jfx_script_wrap(rt, v, &token)) mrb_raise(mrb, E_RUNTIME_ERROR, "too many resources");
            /* Allocate the Ruby object first so allocation failure cannot strand
             * an external wrapper block outside the GC's ownership. */
            struct RData *data = mrb_data_object_alloc(mrb, as_ruby(rt)->value_class, NULL, &wrapped_type);
            ruby_wrapped_t *wrapped = jfx_script_alloc(rt, NULL, sizeof(*wrapped));
            if (!wrapped) mrb_raise(mrb, E_RUNTIME_ERROR, "memory budget exceeded");
            *wrapped = (ruby_wrapped_t){ rt, v->type, token }; data->data = wrapped;
            return mrb_obj_value(data);
        }
        case JFX_TYPE_FUNCTION: return ref_value(mrb, v->function);
        default: mrb_raise(mrb, E_TYPE_ERROR, "unsupported native value");
    }
    return mrb_nil_value();
}
static jfx_script_status_t to_value(mrb_state *mrb, mrb_value v, bool copy, jfx_value_t *out) {
    jfx_script_runtime_t *rt = mrb->allocf_ud;
    *out = (jfx_value_t){0};
    if (mrb_nil_p(v)) return JFX_SCRIPT_OK;
    if (mrb_true_p(v) || mrb_false_p(v)) { out->type = JFX_TYPE_BOOL; out->b = mrb_true_p(v); return JFX_SCRIPT_OK; }
    if (mrb_integer_p(v)) { out->type = JFX_TYPE_INT; out->i = (int64_t)mrb_integer(v); return JFX_SCRIPT_OK; }
    if (mrb_float_p(v)) { out->type = JFX_TYPE_FLOAT; out->f = (double)mrb_float(v); return isfinite(out->f) ? JFX_SCRIPT_OK : JFX_SCRIPT_TYPE_ERROR; }
    if (mrb_string_p(v)) {
        const char *text = RSTRING_PTR(v); size_t length = (size_t)RSTRING_LEN(v);
        if (copy) return jfx_script_copy_string(rt, text, length, out);
        if (memchr(text, 0, length)) return JFX_SCRIPT_TYPE_ERROR;
        out->type = JFX_TYPE_STRING; out->str = mrb_str_to_cstr(mrb, v); return JFX_SCRIPT_OK;
    }
    if (mrb_proc_p(v)) { out->type = JFX_TYPE_FUNCTION; return pin(mrb, v, &out->function); }
    if (mrb_data_p(v) && DATA_TYPE(v) == &wrapped_type) {
        ruby_wrapped_t *wrapped = DATA_PTR(v);
        if (!wrapped || wrapped->rt != rt) return JFX_SCRIPT_TYPE_ERROR;
        if (wrapped->type == JFX_TYPE_COLOR) { out->type = JFX_TYPE_COLOR; out->color = (uint32_t)wrapped->token; return JFX_SCRIPT_OK; }
        return jfx_script_unwrap(rt, wrapped->token, out);
    }
    if (mrb_array_p(v)) {
        mrb_int n = RARRAY_LEN(v);
        if (n < 2 || n > 4) return JFX_SCRIPT_TYPE_ERROR;
        out->type = (jfx_value_type_t)(JFX_TYPE_VEC2 + n - 2);
        for (mrb_int i = 0; i < n; ++i) {
            mrb_value element = mrb_ary_ref(mrb, v, i);
            if (!mrb_integer_p(element) && !mrb_float_p(element)) return JFX_SCRIPT_TYPE_ERROR;
            double f = mrb_integer_p(element) ? (double)mrb_integer(element) : (double)mrb_float(element);
            if (!isfinite(f) || fabs(f) > 3.402823466e38) return JFX_SCRIPT_TYPE_ERROR;
            out->vec4[i] = (float)f;
        }
        return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_TYPE_ERROR;
}
static mrb_value host_function(mrb_state *mrb, mrb_value self) {
    (void)self;
    jfx_script_runtime_t *rt = mrb->allocf_ud;
    const char *name = mrb_sym_name(mrb, mrb_get_mid(mrb));
    const mrb_value *argv = NULL; mrb_int argc = 0;
    mrb_get_args(mrb, "*", &argv, &argc);
    if (argc > JFX_SCRIPT_MAX_ARGS) mrb_raise(mrb, E_ARGUMENT_ERROR, "too many host arguments");
    jfx_value_t args[JFX_SCRIPT_MAX_ARGS] = {{0}}, result = {0};
    jfx_script_status_t status = JFX_SCRIPT_OK;
    for (mrb_int i = 0; i < argc; ++i) { status = to_value(mrb, argv[i], false, &args[i]); if (status) break; }
    if (!status) status = jfx_script_host_call(rt, name, args, (size_t)argc, &result);
    for (mrb_int i = 0; i < argc; ++i) if (args[i].type == JFX_TYPE_FUNCTION) release(rt, args[i].function);
    if (status) {
        if (!rt->error[0]) (void)jfx_script_fail(rt, status, "invalid or unavailable JFX host operation");
        else if (!rt->fault) rt->fault = status;
        mrb_raise(mrb, E_RUNTIME_ERROR, rt->error);
    }
    return from_value(mrb, &result);
}
static void hook(mrb_state *mrb, const struct mrb_irep *irep, const mrb_code *pc, mrb_value *registers) {
    (void)irep; (void)pc; (void)registers;
    if (!jfx_script_tick(mrb->allocf_ud, 1)) mrb_raise(mrb, E_RUNTIME_ERROR, "instruction budget exceeded");
}
static mrb_value value_string(mrb_state *mrb, mrb_value self) { (void)self; return mrb_str_new_lit(mrb, "jfx.Value"); }
static mrb_value setup(mrb_state *mrb, void *user) {
    jfx_script_runtime_t *rt = user;
    /* Keep host references in a private fixed-size array, rooted permanently in
     * the native GC arena. mrb_gc_register's $_gc_root_ is script-mutable. The
     * protected setup returns this array, preserving it across arena restores. */
    as_ruby(rt)->reference_roots = mrb_ary_new_capa(mrb, JFX_SCRIPT_REFS);
    mrb_ary_set(mrb, as_ruby(rt)->reference_roots, JFX_SCRIPT_REFS - 1, mrb_nil_value());
    struct RClass *jfx = mrb_define_module(mrb, "JFX");
    for (size_t i = 0; i < jfx_script_host_count; ++i)
        mrb_define_module_function(mrb, jfx, jfx_script_host_names[i], host_function, MRB_ARGS_ANY());
    as_ruby(rt)->value_class = mrb_define_class_under(mrb, jfx, "Value", mrb->object_class);
    mrb_undef_class_method(mrb, as_ruby(rt)->value_class, "new");
    mrb_define_method(mrb, as_ruby(rt)->value_class, "to_s", value_string, MRB_ARGS_NONE());
    mrb_define_method(mrb, as_ruby(rt)->value_class, "inspect", value_string, MRB_ARGS_NONE());
    const char *const blocked[] = { "system", "exec", "spawn", "eval", "load", "require", "puts", "print", "p" };
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); ++i) {
        mrb_undef_method(mrb, mrb->kernel_module, blocked[i]);
        mrb_undef_class_method(mrb, mrb->kernel_module, blocked[i]);
    }
    mrb_const_remove(mrb, mrb_obj_value(mrb->object_class), mrb_intern_lit(mrb, "GC"));
    return as_ruby(rt)->reference_roots;
}
static mrb_value job_entry(mrb_state *mrb, void *user) {
    ruby_job_t *job = user;
    job->status = JFX_SCRIPT_OK;
    switch (job->operation) {
        case 0: {
            mrbc_context *context = mrbc_context_new(mrb); job->context = context;
            context->capture_errors = TRUE; mrbc_filename(mrb, context, job->name);
            mrb_value value = mrb_load_nstring_cxt(mrb, job->source, job->length, context);
            mrbc_context_free(mrb, context); job->context = NULL;
            if (mrb->exc) mrb_exc_raise(mrb, mrb_obj_value(mrb->exc));
            return value;
        }
        case 1: {
            struct RClass *klass = mrb_class(mrb, mrb_top_self(mrb));
            mrb_method_t method = mrb_method_search_vm(mrb, &klass, mrb_intern_cstr(mrb, job->name));
            struct RProc *proc = MRB_METHOD_PROC_P(method) ? MRB_METHOD_PROC(method) : NULL;
            if (!proc) { job->status = JFX_SCRIPT_NOT_FOUND; break; }
            job->status = pin(mrb, mrb_obj_value(proc), job->out_ref); break;
        }
        case 2: {
            mrb_value argv[JFX_SCRIPT_MAX_ARGS];
            for (size_t i = 0; i < job->argc; ++i) argv[i] = from_value(mrb, &job->args[i]);
            mrb_value result = mrb_yield_with_class(mrb, ref_value(mrb, job->ref), (mrb_int)job->argc,
                argv, mrb_top_self(mrb), mrb->object_class);
            job->status = to_value(mrb, result, true, job->out); break;
        }
        case 3: job->status = pin(mrb, from_value(mrb, job->args), job->out_ref); break;
        case 4: job->status = to_value(mrb, ref_value(mrb, job->ref), true, job->out); break;
        case 5: job->status = pin(mrb, ref_value(mrb, job->ref), job->out_ref); break;
        default: job->status = JFX_SCRIPT_INVALID_ARGUMENT; break;
    }
    return mrb_nil_value();
}
static mrb_value describe_error(mrb_state *mrb, void *user) {
    mrb_value exception = *(mrb_value *)user;
    mrb_value text = mrb_exc_inspect(mrb, exception), trace = mrb_exc_backtrace(mrb, exception);
    if (mrb_array_p(trace)) {
        for (mrb_int i = 0; i < RARRAY_LEN(trace) && i < 8; ++i) {
            mrb_str_cat_lit(mrb, text, "\n"); mrb_str_concat(mrb, text, mrb_ary_ref(mrb, trace, i));
        }
    }
    jfx_script_runtime_t *rt = mrb->allocf_ud;
    (void)snprintf(rt->error, sizeof(rt->error), "%.*s", (int)RSTRING_LEN(text), RSTRING_PTR(text));
    return mrb_nil_value();
}
static jfx_script_status_t run(ruby_job_t *job) {
    mrb_state *mrb = as_ruby(job->rt)->state;
    int arena = mrb_gc_arena_save(mrb); mrb_bool error = FALSE;
    mrb_value exception = mrb_protect_error(mrb, job_entry, job, &error);
    if (error || mrb->exc) {
        if (mrb->exc) exception = mrb_obj_value(mrb->exc);
        mrb->exc = NULL;
        jfx_script_status_t status = job->rt->fault ? job->rt->fault : JFX_SCRIPT_ERROR;
        mrb_bool ignored = FALSE;
        (void)mrb_protect_error(mrb, describe_error, &exception, &ignored);
        if (!job->rt->error[0]) (void)jfx_script_fail(job->rt, status, "mruby exception");
        job->status = status; mrb->exc = NULL;
    }
    mrb_gc_arena_restore(mrb, arena);
    if (job->context) { mrbc_context_free(mrb, job->context); job->context = NULL; }
    return job->status;
}
static jfx_script_status_t load(jfx_script_runtime_t *rt, const void *s, size_t n, const char *name) {
    ruby_job_t job = { .rt = rt, .source = s, .length = n, .name = name }; return run(&job);
}
static jfx_script_status_t capture(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t *out) {
    ruby_job_t job = { .rt = rt, .operation = 1, .name = name, .out_ref = out }; return run(&job);
}
static jfx_script_status_t call(jfx_script_runtime_t *rt, jfx_script_value_t ref, const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    ruby_job_t job = { .rt = rt, .operation = 2, .ref = ref, .args = args, .argc = argc, .out = out }; return run(&job);
}
static jfx_script_status_t from(jfx_script_runtime_t *rt, const jfx_value_t *v, jfx_script_value_t *out) {
    ruby_job_t job = { .rt = rt, .operation = 3, .args = v, .out_ref = out }; return run(&job);
}
static jfx_script_status_t to(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_value_t *out) {
    ruby_job_t job = { .rt = rt, .operation = 4, .ref = ref, .out = out }; return run(&job);
}
static jfx_script_status_t clone(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_script_value_t *out) {
    ruby_job_t job = { .rt = rt, .operation = 5, .ref = ref, .out_ref = out }; return run(&job);
}
static void destroy(jfx_script_runtime_t *rt) { mrb_close(as_ruby(rt)->state); jfx_script_base_free(rt); }
static void gc(jfx_script_runtime_t *rt, int mode) {
    mrb_state *mrb = as_ruby(rt)->state;
    if (!mode) mrb_full_gc(mrb); else mrb->gc.disabled = mode == 1;
}
jfx_script_status_t jfx_mruby_script_create(const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    static const jfx_script_ops_t ops = { destroy, load, capture, call, from, to, clone, release, gc };
    jfx_script_status_t status = jfx_script_base_create(sizeof(struct jfx_mruby_runtime), desc, JFX_SCRIPT_MRUBY, &ops, out);
    if (status) return status;
    struct jfx_mruby_runtime *rt = as_ruby(*out);
    rt->state = mrb_open_allocf(allocator, &rt->base);
    if (!rt->state) { jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    mrb_bool error = FALSE; (void)mrb_protect_error(rt->state, setup, &rt->base, &error);
    if (error || rt->state->exc) { destroy(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    rt->state->code_fetch_hook = hook;
    return JFX_SCRIPT_OK;
}

static jfx_script_status_t legacy(jfx_script_status_t s) { return s == JFX_SCRIPT_BUDGET || s == JFX_SCRIPT_TYPE_ERROR ? JFX_SCRIPT_ERROR : s; }
jfx_script_status_t jfx_mruby_runtime_create(const jfx_script_config_t *config, jfx_mruby_runtime_t **out) {
    if (!out) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_desc_t desc = { .size = sizeof(desc) }; if (config) desc.config = *config;
    jfx_script_runtime_t *rt = NULL;
    jfx_script_status_t status = jfx_mruby_script_create(&desc, &rt); *out = (jfx_mruby_runtime_t *)rt; return status;
}
void jfx_mruby_runtime_destroy(jfx_mruby_runtime_t *rt) { jfx_script_runtime_destroy((jfx_script_runtime_t *)rt); }
jfx_script_status_t jfx_mruby_runtime_load(jfx_mruby_runtime_t *rt, const char *source, const char *name) {
    return source ? legacy(jfx_script_runtime_load((jfx_script_runtime_t *)rt, source, strlen(source), name)) : JFX_SCRIPT_INVALID_ARGUMENT;
}
jfx_script_status_t jfx_mruby_runtime_call_number(jfx_mruby_runtime_t *rt, const char *name, double input, double *out) {
    return legacy(jfx_script_runtime_call_number((jfx_script_runtime_t *)rt, name, input, out));
}
size_t jfx_mruby_runtime_memory_used(const jfx_mruby_runtime_t *rt) { return jfx_script_runtime_memory_used((const jfx_script_runtime_t *)rt); }
size_t jfx_mruby_runtime_memory_limit(const jfx_mruby_runtime_t *rt) { return jfx_script_runtime_memory_limit((const jfx_script_runtime_t *)rt); }
void jfx_mruby_runtime_gc_collect(jfx_mruby_runtime_t *rt) { jfx_script_runtime_gc_collect((jfx_script_runtime_t *)rt); }
void jfx_mruby_runtime_gc_pause(jfx_mruby_runtime_t *rt) { jfx_script_runtime_gc_pause((jfx_script_runtime_t *)rt); }
void jfx_mruby_runtime_gc_resume(jfx_mruby_runtime_t *rt) { jfx_script_runtime_gc_resume((jfx_script_runtime_t *)rt); }
jfx_script_runtime_t *jfx_mruby_runtime_interface(jfx_mruby_runtime_t *rt) { return (jfx_script_runtime_t *)rt; }
