#include "jfx/mruby.h"

#include <math.h>
#include <string.h>

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/variable.h"
#include "tilly/allocator.h"

struct jfx_mruby_runtime {
    mrb_state *state;
    size_t memory_limit;
    uint64_t instruction_limit;
    uint64_t instructions;
};

static void *mruby_alloc(mrb_state *state, void *ptr, size_t size, void *userdata) {
    (void)state;
    (void)userdata;
    if (size == 0) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), ptr);
        return NULL;
    }
    if (ptr) return tilly_realloc((tilly_allocator_t *)tilly_default_allocator(), ptr, size);
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), size, _Alignof(max_align_t));
}

static mrb_value mruby_clamp(mrb_state *state, mrb_value self) {
    (void)self;
    mrb_state *mrb = state;
    mrb_float value = 0.0;
    mrb_float low = 0.0;
    mrb_float high = 0.0;
    mrb_get_args(state, "fff", &value, &low, &high);
    if (!isfinite(value) || !isfinite(low) || !isfinite(high) || low > high) {
        mrb_raise(state, E_ARGUMENT_ERROR, "JFX.clamp expects finite bounds with low <= high");
    }
    if (value < low) value = low;
    if (value > high) value = high;
    return mrb_float_value(state, value);
}

static int forbidden_source(const char *source) {
    static const char *const words[] = { "File", "Dir", "IO", "system", "exec", "spawn", "eval", "`" };
    for (size_t i = 0; i < sizeof(words) / sizeof(*words); ++i) {
        if (strstr(source, words[i])) return 1;
    }
    return 0;
}

static void clear_exception(mrb_state *state) {
    state->exc = NULL;
}

static void mruby_code_fetch_hook(mrb_state *mrb, const struct mrb_irep *irep,
    const mrb_code *program_counter, mrb_value *registers) {
    (void)irep;
    (void)program_counter;
    (void)registers;
    jfx_mruby_runtime_t *runtime = mrb->allocf_ud;
    if (++runtime->instructions > runtime->instruction_limit) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "instruction budget exceeded");
    }
}

jfx_script_status_t jfx_mruby_runtime_create(const jfx_script_config_t *config,
    jfx_mruby_runtime_t **out_runtime) {
    if (!out_runtime) return JFX_SCRIPT_INVALID_ARGUMENT;
    *out_runtime = NULL;
    jfx_mruby_runtime_t *runtime = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*runtime), _Alignof(jfx_mruby_runtime_t));
    if (!runtime) return JFX_SCRIPT_OUT_OF_MEMORY;
    memset(runtime, 0, sizeof(*runtime));
    runtime->memory_limit = config && config->memory_limit ? config->memory_limit : 4u * 1024u * 1024u;
    runtime->instruction_limit = config && config->instruction_limit ? config->instruction_limit : 100000u;
    runtime->state = mrb_open_allocf(mruby_alloc, runtime);
    if (!runtime->state) { tilly_free((tilly_allocator_t *)tilly_default_allocator(), runtime); return JFX_SCRIPT_OUT_OF_MEMORY; }
    struct RClass *jfx = mrb_define_module(runtime->state, "JFX");
    mrb_define_module_function(runtime->state, jfx, "clamp", mruby_clamp, MRB_ARGS_REQ(3));
    mrb_undef_method(runtime->state, runtime->state->kernel_module, "system");
    mrb_undef_method(runtime->state, runtime->state->kernel_module, "exec");
    mrb_undef_method(runtime->state, runtime->state->kernel_module, "spawn");
    runtime->state->code_fetch_hook = mruby_code_fetch_hook;
    *out_runtime = runtime;
    return JFX_SCRIPT_OK;
}

void jfx_mruby_runtime_destroy(jfx_mruby_runtime_t *runtime) {
    if (!runtime) return;
    mrb_close(runtime->state);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), runtime);
}

jfx_script_status_t jfx_mruby_runtime_load(jfx_mruby_runtime_t *runtime,
    const char *source, const char *name) {
    (void)name;
    if (!runtime || !source) return JFX_SCRIPT_INVALID_ARGUMENT;
    if (forbidden_source(source)) return JFX_SCRIPT_ERROR;
    runtime->instructions = 0;
    int arena = mrb_gc_arena_save(runtime->state);
    (void)mrb_load_string(runtime->state, source);
    mrb_gc_arena_restore(runtime->state, arena);
    if (runtime->state->exc) { clear_exception(runtime->state); return JFX_SCRIPT_ERROR; }
    return JFX_SCRIPT_OK;
}

jfx_script_status_t jfx_mruby_runtime_call_number(jfx_mruby_runtime_t *runtime,
    const char *function_name, double input, double *out_result) {
    if (!runtime || !function_name || !out_result || !isfinite(input)) return JFX_SCRIPT_INVALID_ARGUMENT;
    runtime->instructions = 0;
    mrb_value result = mrb_funcall(runtime->state, mrb_top_self(runtime->state), function_name, 1,
        mrb_float_value(runtime->state, (mrb_float)input));
    if (runtime->state->exc) { clear_exception(runtime->state); return JFX_SCRIPT_ERROR; }
    if (!mrb_float_p(result) && !mrb_fixnum_p(result)) return JFX_SCRIPT_ERROR;
    *out_result = (double)mrb_as_float(runtime->state, result);
    return isfinite(*out_result) ? JFX_SCRIPT_OK : JFX_SCRIPT_ERROR;
}

void jfx_mruby_runtime_gc_collect(jfx_mruby_runtime_t *runtime) { if (runtime) mrb_full_gc(runtime->state); }
void jfx_mruby_runtime_gc_pause(jfx_mruby_runtime_t *runtime) { if (runtime) runtime->state->gc.disabled = TRUE; }
void jfx_mruby_runtime_gc_resume(jfx_mruby_runtime_t *runtime) { if (runtime) runtime->state->gc.disabled = FALSE; }
