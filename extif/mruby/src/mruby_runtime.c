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
    size_t memory_used;
    uint64_t instruction_limit;
    uint64_t instructions;
};

/* Allocates through Tilly and refuses to exceed the script's memory budget.
 *
 * The budget is enforced here rather than by inspecting the script source: a
 * source filter cannot see what a program does at run time, while this sees
 * every allocation. A refused allocation returns NULL, which mruby turns into a
 * recoverable out-of-memory error inside the script.
 *
 * Every block carries a header holding its size. mruby's allocator contract
 * passes the new size and the old pointer but never the old size, so without a
 * header the running total could only ever grow and a script that had freed
 * everything it allocated would still exhaust its budget. The header keeps the
 * accounting exact. */
/* 16 bytes: a multiple of max_align_t on every platform JoltFX targets, so a
 * payload starting just past the header keeps the alignment mruby expects. */
typedef struct {
    size_t size;
    size_t reserved;
} block_header_t;

/* Blocks are allocated max_align_t-aligned, so a payload at offset
 * sizeof(block_header_t) is still max_align_t-aligned as long as the header
 * size is a whole number of alignment units. */
_Static_assert(sizeof(block_header_t) % _Alignof(max_align_t) == 0,
    "the block header must be a whole number of max_align_t units");

static void *mruby_alloc(mrb_state *state, void *ptr, size_t size, void *userdata) {
    (void)state;
    jfx_mruby_runtime_t *runtime = (jfx_mruby_runtime_t *)userdata;
    if (!runtime) {
        return NULL;
    }
    if (size == 0) {
        if (!ptr) {
            return NULL;
        }
        block_header_t *header = (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
        runtime->memory_used -= header->size;
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), header);
        return NULL;
    }
    if (size > SIZE_MAX - sizeof(block_header_t)) {
        return NULL;
    }
    const size_t total = sizeof(block_header_t) + size;
    uint8_t *raw = ptr ? (uint8_t *)ptr - sizeof(block_header_t) : NULL;
    block_header_t *header =
        ptr ? (block_header_t *)tilly_realloc(
                  (tilly_allocator_t *)tilly_default_allocator(), raw, total)
            : (block_header_t *)tilly_alloc(
                  (tilly_allocator_t *)tilly_default_allocator(), total,
                  _Alignof(block_header_t));
    if (!header) {
        return NULL;
    }
    const size_t previous = ptr ? header->size : 0u;
    if (size > previous) {
        const size_t growth = size - previous;
        /* Charge the growth, refusing rather than exceeding the budget. The
         * comparison is written so it cannot underflow. */
        if (growth > runtime->memory_limit || runtime->memory_used > runtime->memory_limit - growth) {
            if (ptr) {
                /* Put the original block back so the caller's view of memory
                 * stays consistent, then report the refusal. */
                block_header_t *restored =
                    (block_header_t *)tilly_realloc(
                        (tilly_allocator_t *)tilly_default_allocator(), header,
                        sizeof(block_header_t) + previous);
                if (restored) {
                    restored->size = previous;
                }
            }
            return NULL;
        }
        runtime->memory_used += growth;
    } else {
        runtime->memory_used -= previous - size;
    }
    header->size = size;
    return header + 1;
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
    /* Capability removal is the sandbox. The build disables mruby's stdio and
     * filesystem gems, and the process-control methods are removed here, so
     * `File`, `Dir` and `system` do not exist for a script to reach. Filtering
     * the source text for those words would only catch the obvious spelling
     * and would also reject innocent identifiers containing them. */
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

size_t jfx_mruby_runtime_memory_used(const jfx_mruby_runtime_t *runtime) {
    return runtime ? runtime->memory_used : 0u;
}

size_t jfx_mruby_runtime_memory_limit(const jfx_mruby_runtime_t *runtime) {
    return runtime ? runtime->memory_limit : 0u;
}

void jfx_mruby_runtime_gc_collect(jfx_mruby_runtime_t *runtime) { if (runtime) mrb_full_gc(runtime->state); }
void jfx_mruby_runtime_gc_pause(jfx_mruby_runtime_t *runtime) { if (runtime) runtime->state->gc.disabled = TRUE; }
void jfx_mruby_runtime_gc_resume(jfx_mruby_runtime_t *runtime) { if (runtime) runtime->state->gc.disabled = FALSE; }
