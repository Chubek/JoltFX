#include "runtime_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tilly/logger.h"

typedef union { max_align_t align; struct { size_t size; } block; } script_block_t;
/* Creation and lifecycle share the serialized extension-owner thread. */
static uint32_t next_reference_namespace = 1;

size_t jfx_script_alloc_size(const void *ptr) {
    return ptr ? ((const script_block_t *)ptr - 1)->block.size : 0;
}

void *jfx_script_alloc(jfx_script_runtime_t *rt, void *ptr, size_t size) {
    const size_t old = jfx_script_alloc_size(ptr);
    if (!size) {
        if (ptr) { rt->memory_used -= old; tilly_free(rt->allocator, (script_block_t *)ptr - 1); }
        return NULL;
    }
    if (size > SIZE_MAX - sizeof(script_block_t) ||
        (size > old && (size - old > rt->memory_limit ||
            rt->memory_used > rt->memory_limit - (size - old)))) {
        rt->fault = JFX_SCRIPT_OUT_OF_MEMORY;
        return NULL;
    }
    /* Check the budget before moving the original block; failed realloc must
     * leave the caller's pointer and accounting unchanged. */
    script_block_t *next = tilly_alloc(rt->allocator, sizeof(*next) + size, _Alignof(max_align_t));
    if (!next) { rt->fault = JFX_SCRIPT_OUT_OF_MEMORY; return NULL; }
    next->block.size = size;
    if (ptr) {
        memcpy(next + 1, ptr, old < size ? old : size);
        tilly_free(rt->allocator, (script_block_t *)ptr - 1);
    }
    rt->memory_used = rt->memory_used - old + size;
    return next + 1;
}

jfx_script_status_t jfx_script_base_create(size_t bytes, const jfx_script_desc_t *desc,
    jfx_script_language_t language, const jfx_script_ops_t *ops, jfx_script_runtime_t **out) {
    if (!out) return JFX_SCRIPT_INVALID_ARGUMENT;
    *out = NULL;
    if ((desc && (desc->size < sizeof(*desc) ||
        (desc->capabilities & ~(uint32_t)(JFX_SCRIPT_CAP_EDITOR | JFX_SCRIPT_CAP_EVENTS)))) || !ops)
        return JFX_SCRIPT_INVALID_ARGUMENT;
    size_t limit = desc && desc->config.memory_limit ? desc->config.memory_limit : JFX_SCRIPT_DEFAULT_MEMORY;
    if (bytes > limit) return JFX_SCRIPT_OUT_OF_MEMORY;
    tilly_allocator_t *alloc = desc && desc->allocator ? desc->allocator :
        (tilly_allocator_t *)tilly_default_allocator();
    jfx_script_runtime_t *rt = tilly_alloc(alloc, bytes, _Alignof(max_align_t));
    if (!rt) return JFX_SCRIPT_OUT_OF_MEMORY;
    if (!next_reference_namespace) { tilly_free(alloc, rt); return JFX_SCRIPT_BUSY; }
    memset(rt, 0, bytes);
    rt->ops = ops; rt->allocator = alloc; rt->language = language;
    rt->memory_limit = limit; rt->memory_used = bytes;
    rt->instruction_limit = desc && desc->config.instruction_limit ?
        desc->config.instruction_limit : JFX_SCRIPT_DEFAULT_INSTRUCTIONS;
    rt->editor = desc ? desc->editor : NULL;
    rt->capabilities = desc ? desc->capabilities : 0;
    rt->error_context = desc ? desc->error_context : NULL;
    rt->epoch = 1; rt->next_subscription = 1;
    rt->reference_namespace = next_reference_namespace++;
    *out = rt;
    return JFX_SCRIPT_OK;
}

void jfx_script_base_free(jfx_script_runtime_t *rt) {
    (void)jfx_script_alloc(rt, rt->result_string, 0);
    tilly_free(rt->allocator, rt);
}

jfx_script_status_t jfx_script_fail(jfx_script_runtime_t *rt, jfx_script_status_t status, const char *message) {
    if (rt) {
        if (rt->fault == JFX_SCRIPT_OK) rt->fault = status;
        if (message) (void)snprintf(rt->error, sizeof(rt->error), "%s", message);
    }
    return status;
}

bool jfx_script_tick(jfx_script_runtime_t *rt, uint64_t count) {
    if (rt->fault == JFX_SCRIPT_BUDGET) return false;
    if (count > rt->instruction_limit || rt->instructions > rt->instruction_limit - count) {
        (void)jfx_script_fail(rt, JFX_SCRIPT_BUDGET, "instruction budget exceeded");
        return false;
    }
    rt->instructions += count;
    return true;
}

jfx_script_status_t jfx_script_copy_string(jfx_script_runtime_t *rt, const char *text,
    size_t length, jfx_value_t *out) {
    if (!text || length == SIZE_MAX || memchr(text, 0, length))
        return jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "embedded NUL in script string");
    char *copy = jfx_script_alloc(rt, NULL, length + 1);
    if (!copy) return JFX_SCRIPT_OUT_OF_MEMORY;
    memcpy(copy, text, length); copy[length] = 0;
    (void)jfx_script_alloc(rt, rt->result_string, 0);
    rt->result_string = copy;
    *out = (jfx_value_t){ .type = JFX_TYPE_STRING, .str = copy };
    return JFX_SCRIPT_OK;
}

jfx_script_status_t jfx_script_wrap(jfx_script_runtime_t *rt, const jfx_value_t *value, uint64_t *out) {
    if (rt->resource_count == JFX_SCRIPT_RESOURCES)
        return jfx_script_fail(rt, JFX_SCRIPT_OUT_OF_MEMORY, "too many scoped resources");
    rt->resources[rt->resource_count] = *value;
    *out = (rt->epoch << 8) | (++rt->resource_count);
    return JFX_SCRIPT_OK;
}
jfx_script_status_t jfx_script_unwrap(jfx_script_runtime_t *rt, uint64_t token, jfx_value_t *out) {
    size_t slot = (size_t)(token & 255u);
    if ((token >> 8) != rt->epoch || !slot || slot > rt->resource_count)
        return jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "expired resource handle");
    *out = rt->resources[slot - 1];
    return JFX_SCRIPT_OK;
}

static bool valid_value(const jfx_value_t *v) {
    if (!v || v->type < JFX_TYPE_NIL || v->type > JFX_TYPE_FUNCTION) return false;
    if (v->type == JFX_TYPE_FLOAT) return isfinite(v->f);
    if (v->type >= JFX_TYPE_VEC2 && v->type <= JFX_TYPE_VEC4) {
        for (size_t i = 0; i < (size_t)(v->type - JFX_TYPE_VEC2 + 2); ++i)
            if (!isfinite(v->vec4[i])) return false;
    }
    if (v->type == JFX_TYPE_STRING) return v->str != NULL;
    if (v->type == JFX_TYPE_BUFFER) return v->buffer != NULL;
    if (v->type == JFX_TYPE_TEXTURE) return v->texture != NULL;
    if (v->type == JFX_TYPE_USERDATA) return v->userdata != NULL;
    return true;
}
static jfx_script_status_t begin(jfx_script_runtime_t *rt) {
    if (!rt) return JFX_SCRIPT_INVALID_ARGUMENT;
    if (rt->busy) return JFX_SCRIPT_BUSY;
    rt->busy = true; rt->fault = JFX_SCRIPT_OK; rt->error[0] = 0;
    rt->instructions = 0; rt->resource_count = 0;
    /* Resource epochs cannot repeat during a practical runtime lifetime. */
    if (++rt->epoch > (UINT64_MAX >> 8)) rt->epoch = 1;
    return JFX_SCRIPT_OK;
}
static jfx_script_status_t finish(jfx_script_runtime_t *rt, jfx_script_status_t status) {
    if (rt->fault != JFX_SCRIPT_OK) status = rt->fault;
    rt->busy = false; rt->resource_count = 0;
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i)
        if (rt->callbacks[i].id && rt->callbacks[i].remove_pending)
            (void)jfx_script_runtime_off(rt, rt->callbacks[i].id);
    rt->last_status = status;
    if (status != JFX_SCRIPT_OK) {
        if (!rt->error[0]) (void)snprintf(rt->error, sizeof(rt->error), "extension operation failed (%d)", status);
        tilly_log_simple(TILLY_LOG_ERROR, "%s", rt->error);
        if (rt->error_context) {
            char *copy = tillyz_strdup(&rt->error_context->arena, rt->error);
            tillyz_error_push(rt->error_context, TILLYZ_ERR_INIT_FAILED,
                copy ? copy : "extension error", __FILE__, __LINE__);
        }
    }
    return status;
}

void jfx_script_runtime_destroy(jfx_script_runtime_t *rt) {
    if (!rt || rt->busy) return;
    /* off() uses the exact common relay, including its owning userdata. */
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i)
        if (rt->callbacks[i].id) (void)jfx_script_runtime_off(rt, rt->callbacks[i].id);
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i)
        if (rt->kernels[i].function) rt->ops->release(rt, rt->kernels[i].function);
    rt->ops->destroy(rt);
}

jfx_script_status_t jfx_script_runtime_load(jfx_script_runtime_t *rt,
    const void *source, size_t length, const char *name) {
    if (!rt || !source || !name || !*name) return JFX_SCRIPT_INVALID_ARGUMENT;
    if (length > rt->memory_limit) return JFX_SCRIPT_OUT_OF_MEMORY;
    if (rt->language != JFX_SCRIPT_WASM && memchr(source, 0, length)) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    uint32_t baseline = rt->next_subscription;
    bool old_kernels[JFX_SCRIPT_REGISTRATIONS];
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) old_kernels[i] = rt->kernels[i].function != 0;
    status = rt->ops->load(rt, source, length, name);
    if (status || rt->fault) {
        rt->busy = false;
        for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) {
            if (rt->callbacks[i].id >= baseline && rt->callbacks[i].id)
                (void)jfx_script_runtime_off(rt, rt->callbacks[i].id);
            if (!old_kernels[i] && rt->kernels[i].function) {
                rt->ops->release(rt, rt->kernels[i].function);
                memset(&rt->kernels[i], 0, sizeof(rt->kernels[i]));
            }
        }
    }
    return finish(rt, status);
}

static jfx_script_status_t call_ref(jfx_script_runtime_t *rt, jfx_script_value_t ref,
    const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    if (argc > JFX_SCRIPT_MAX_ARGS || (argc && !args) || !out) return JFX_SCRIPT_INVALID_ARGUMENT;
    for (size_t i = 0; i < argc; ++i) if (!valid_value(&args[i])) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_value_t result = {0};
    jfx_script_status_t status = rt->ops->call(rt, ref, args, argc, &result);
    if (!status && !rt->fault) *out = result;
    else if (result.type == JFX_TYPE_FUNCTION && result.function) rt->ops->release(rt, result.function);
    return status;
}
jfx_script_status_t jfx_script_runtime_call(jfx_script_runtime_t *rt, const char *name,
    const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    if (!rt || !name || !*name || !out || argc > JFX_SCRIPT_MAX_ARGS || (argc && !args))
        return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    jfx_script_value_t ref = 0;
    status = rt->ops->capture(rt, name, &ref);
    if (!status) { status = call_ref(rt, ref, args, argc, out); rt->ops->release(rt, ref); }
    return finish(rt, status);
}
jfx_script_status_t jfx_script_runtime_call_number(jfx_script_runtime_t *rt,
    const char *name, double input, double *out) {
    if (!out || !isfinite(input)) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_value_t arg = { .type = JFX_TYPE_FLOAT, .f = input }, ret = {0};
    jfx_script_status_t status = jfx_script_runtime_call(rt, name, &arg, 1, &ret);
    if (status) return status;
    if (ret.type != JFX_TYPE_FLOAT && ret.type != JFX_TYPE_INT)
        return finish(rt, jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "function did not return a number"));
    *out = ret.type == JFX_TYPE_INT ? (double)ret.i : ret.f;
    return JFX_SCRIPT_OK;
}

static jfx_script_status_t kernel_add(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t ref) {
    if (!name || !*name || strlen(name) >= sizeof(rt->kernels[0].name)) return JFX_SCRIPT_INVALID_ARGUMENT;
    size_t empty = JFX_SCRIPT_REGISTRATIONS;
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) {
        if (!rt->kernels[i].function) { if (empty == JFX_SCRIPT_REGISTRATIONS) empty = i; }
        else if (!strcmp(rt->kernels[i].name, name)) return JFX_SCRIPT_BUSY;
    }
    if (empty == JFX_SCRIPT_REGISTRATIONS) return JFX_SCRIPT_OUT_OF_MEMORY;
    jfx_script_status_t status = rt->ops->clone(rt, ref, &rt->kernels[empty].function);
    if (!status) (void)snprintf(rt->kernels[empty].name, sizeof(rt->kernels[empty].name), "%s", name);
    return status;
}
jfx_script_status_t jfx_script_runtime_register_kernel(jfx_script_runtime_t *rt,
    const char *name, const char *function) {
    if (!rt || !name || !function) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    jfx_script_value_t ref = 0;
    status = rt->ops->capture(rt, function, &ref);
    if (!status) { status = kernel_add(rt, name, ref); rt->ops->release(rt, ref); }
    return finish(rt, status);
}
jfx_script_status_t jfx_script_runtime_invoke_kernel(jfx_script_runtime_t *rt,
    const char *name, const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    if (!rt || !name || !out) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    status = JFX_SCRIPT_NOT_FOUND;
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) {
        if (rt->kernels[i].function && !strcmp(rt->kernels[i].name, name)) {
            status = call_ref(rt, rt->kernels[i].function, args, argc, out); break;
        }
    }
    return finish(rt, status);
}
jfx_script_status_t jfx_script_runtime_from_native(jfx_script_runtime_t *rt,
    const jfx_value_t *value, jfx_script_value_t *out) {
    if (!rt || !out || !valid_value(value)) return JFX_SCRIPT_INVALID_ARGUMENT;
    /* Standalone references to borrowed resources are intentionally forbidden. */
    if (value->type == JFX_TYPE_BUFFER || value->type == JFX_TYPE_TEXTURE || value->type == JFX_TYPE_USERDATA)
        return JFX_SCRIPT_TYPE_ERROR;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    jfx_script_value_t reference = 0;
    status = rt->ops->from(rt, value, &reference);
    if (!status && !rt->fault) *out = reference;
    else if (reference) rt->ops->release(rt, reference);
    return finish(rt, status);
}
jfx_script_status_t jfx_script_runtime_to_native(jfx_script_runtime_t *rt,
    jfx_script_value_t value, jfx_value_t *out) {
    if (!rt || !out || !value) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    jfx_value_t result = {0};
    status = rt->ops->to(rt, value, &result);
    if (!status && !rt->fault) *out = result;
    return finish(rt, status);
}
void jfx_script_runtime_release_value(jfx_script_runtime_t *rt, jfx_script_value_t value) {
    if (rt && !rt->busy && value) rt->ops->release(rt, value);
}

static const char *const event_names[JFX_EVENT_COUNT] = {
    "frame_begin", "frame_end", "kernel_submit", "kernel_complete", "kernel_error",
    "resource_alloc", "resource_free", "timeline_play", "timeline_pause", "asset_load",
    "asset_unload", "ui_input", "plugin_load", "plugin_unload"
};
static void event_relay(jfx_event_type_t event, void *data, void *user) {
    (void)data; /* Event payloads are untyped native pointers, never script data. */
    jfx_script_callback_t *cb = user;
    jfx_script_runtime_t *rt = cb->runtime;
    if (!rt || rt->busy || cb->remove_pending) return;
    if (begin(rt)) return;
    jfx_value_t arg = { .type = JFX_TYPE_STRING, .str = event_names[event] }, result = {0};
    jfx_script_status_t status = call_ref(rt, cb->function, &arg, 1, &result);
    (void)finish(rt, status);
}
static jfx_script_status_t callback_add(jfx_script_runtime_t *rt, const char *event,
    jfx_script_value_t ref, uint32_t *out) {
    if (!(rt->capabilities & JFX_SCRIPT_CAP_EVENTS)) return JFX_SCRIPT_CAPABILITY;
    jfx_event_type_t type = JFX_EVENT_COUNT;
    for (size_t i = 0; i < JFX_EVENT_COUNT; ++i) if (!strcmp(event, event_names[i])) type = (jfx_event_type_t)i;
    if (type == JFX_EVENT_COUNT || rt->next_subscription == UINT32_MAX) return JFX_SCRIPT_INVALID_ARGUMENT;
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) {
        jfx_script_callback_t *cb = &rt->callbacks[i];
        if (cb->id) continue;
        jfx_script_status_t status = rt->ops->clone(rt, ref, &cb->function);
        if (status) return status;
        cb->runtime = rt; cb->event = type;
        if (!event_subscribe(type, event_relay, cb)) {
            rt->ops->release(rt, cb->function); memset(cb, 0, sizeof(*cb)); return JFX_SCRIPT_NOT_SUPPORTED;
        }
        cb->id = rt->next_subscription++; *out = cb->id; return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_OUT_OF_MEMORY;
}
jfx_script_status_t jfx_script_runtime_on(jfx_script_runtime_t *rt, const char *event,
    const char *function, uint32_t *out) {
    if (!rt || !event || !function || !out) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_status_t status = begin(rt);
    if (status) return status;
    jfx_script_value_t ref = 0;
    status = rt->ops->capture(rt, function, &ref);
    if (!status) { status = callback_add(rt, event, ref, out); rt->ops->release(rt, ref); }
    return finish(rt, status);
}
jfx_script_status_t jfx_script_runtime_off(jfx_script_runtime_t *rt, uint32_t id) {
    if (!rt || !id) return JFX_SCRIPT_INVALID_ARGUMENT;
    if (rt->busy) return JFX_SCRIPT_BUSY;
    for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) {
        jfx_script_callback_t *cb = &rt->callbacks[i];
        if (cb->id != id) continue;
        (void)event_unsubscribe_user(cb->event, event_relay, cb);
        rt->ops->release(rt, cb->function); memset(cb, 0, sizeof(*cb)); return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_NOT_FOUND;
}

static bool number(const jfx_value_t *v, double *out) {
    if (v->type == JFX_TYPE_INT) *out = (double)v->i;
    else if (v->type == JFX_TYPE_FLOAT) *out = v->f;
    else return false;
    return isfinite(*out);
}
static bool index_value(const jfx_value_t *v, uint32_t *out) {
    double d;
    if (!number(v, &d) || d < 0 || d > UINT32_MAX || floor(d) != d) return false;
    *out = (uint32_t)d; return true;
}
const char *const jfx_script_host_names[] = {
    "clamp", "log", "command", "state", "on", "off", "register_kernel",
    "size", "read", "write", "dimensions", "sample", "write_pixel", "color"
};
const size_t jfx_script_host_count = sizeof(jfx_script_host_names) / sizeof(jfx_script_host_names[0]);

jfx_script_status_t jfx_script_host_call(jfx_script_runtime_t *rt, const char *name,
    const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    *out = (jfx_value_t){ .type = JFX_TYPE_NIL };
    if (!jfx_script_tick(rt, 1)) return JFX_SCRIPT_BUDGET;
    double a, b, c;
    uint32_t x, y, z;
    if (!strcmp(name, "clamp")) {
        if (argc != 3 || !number(&args[0], &a) || !number(&args[1], &b) ||
            !number(&args[2], &c) || b > c)
            return jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "jfx.clamp expects three finite numbers and low <= high");
        *out = (jfx_value_t){ .type = JFX_TYPE_FLOAT, .f = fmin(c, fmax(b, a)) }; return JFX_SCRIPT_OK;
    }
    if (!strcmp(name, "log")) {
        if (argc != 1 || args[0].type != JFX_TYPE_STRING) return JFX_SCRIPT_TYPE_ERROR;
        tilly_log_simple(TILLY_LOG_INFO, "%s", args[0].str); return JFX_SCRIPT_OK;
    }
    if (!strcmp(name, "color")) {
        if (argc != 1 || !index_value(&args[0], &x)) return JFX_SCRIPT_TYPE_ERROR;
        *out = (jfx_value_t){ .type = JFX_TYPE_COLOR, .color = x }; return JFX_SCRIPT_OK;
    }
    if (!strcmp(name, "command") || !strcmp(name, "state")) {
        if (!(rt->capabilities & JFX_SCRIPT_CAP_EDITOR) || !rt->editor) return JFX_SCRIPT_CAPABILITY;
        if (!strcmp(name, "command")) {
            if (argc != 6 || args[0].type != JFX_TYPE_STRING || !index_value(&args[1], &x) ||
                !index_value(&args[2], &y) || !index_value(&args[3], &z) || !number(&args[4], &a) ||
                args[5].type != JFX_TYPE_STRING) return JFX_SCRIPT_TYPE_ERROR;
            jfx_result_t result = jfx_editor_command(rt->editor, args[0].str, x, y, z, a, args[5].str);
            if (result != JFX_SUCCESS) return jfx_script_fail(rt, JFX_SCRIPT_ERROR, jfx_result_to_string(result));
            return JFX_SCRIPT_OK;
        }
        if (argc != 1 || args[0].type != JFX_TYPE_STRING ||
            (strcmp(args[0].str, "sequence") && strcmp(args[0].str, "graph"))) return JFX_SCRIPT_TYPE_ERROR;
        for (size_t capacity = 4096; capacity <= rt->memory_limit; capacity *= 2) {
            char *json = jfx_script_alloc(rt, NULL, capacity);
            if (!json) return JFX_SCRIPT_OUT_OF_MEMORY;
            jfx_result_t result = !strcmp(args[0].str, "graph") ? jfx_editor_graph_state(rt->editor, json, capacity) :
                jfx_editor_sequence_state(rt->editor, json, capacity);
            if (result == JFX_SUCCESS) {
                (void)jfx_script_alloc(rt, rt->result_string, 0); rt->result_string = json;
                *out = (jfx_value_t){ .type = JFX_TYPE_STRING, .str = json }; return JFX_SCRIPT_OK;
            }
            (void)jfx_script_alloc(rt, json, 0);
            if (result != JFX_ERROR_OUT_OF_MEMORY || capacity > SIZE_MAX / 2) break;
        }
        return JFX_SCRIPT_OUT_OF_MEMORY;
    }
    if (!strcmp(name, "on") || !strcmp(name, "register_kernel")) {
        if (argc != 2 || args[0].type != JFX_TYPE_STRING ||
            (args[1].type != JFX_TYPE_FUNCTION && args[1].type != JFX_TYPE_STRING)) return JFX_SCRIPT_TYPE_ERROR;
        jfx_script_value_t ref = args[1].type == JFX_TYPE_FUNCTION ? args[1].function : 0;
        jfx_script_status_t status = JFX_SCRIPT_OK;
        if (args[1].type == JFX_TYPE_STRING) status = rt->ops->capture(rt, args[1].str, &ref);
        if (!status) {
            uint32_t id = 0;
            status = !strcmp(name, "on") ? callback_add(rt, args[0].str, ref, &id) : kernel_add(rt, args[0].str, ref);
            if (!status && !strcmp(name, "on")) *out = (jfx_value_t){ .type = JFX_TYPE_INT, .i = id };
        }
        if (args[1].type == JFX_TYPE_STRING && ref) rt->ops->release(rt, ref);
        return status;
    }
    if (!strcmp(name, "off")) {
        if (argc != 1 || !index_value(&args[0], &x)) return JFX_SCRIPT_TYPE_ERROR;
        if (!x) return JFX_SCRIPT_INVALID_ARGUMENT;
        for (size_t i = 0; i < JFX_SCRIPT_REGISTRATIONS; ++i) {
            if (rt->callbacks[i].id == x) { rt->callbacks[i].remove_pending = true; return JFX_SCRIPT_OK; }
        }
        return JFX_SCRIPT_NOT_FOUND;
    }
    if (argc < 1) return JFX_SCRIPT_TYPE_ERROR;
    if (!strcmp(name, "size")) {
        if (argc != 1 || args[0].type != JFX_TYPE_BUFFER) return JFX_SCRIPT_TYPE_ERROR;
        *out = (jfx_value_t){ .type = JFX_TYPE_INT, .i = (int64_t)(jfx_buffer_size(args[0].buffer) / sizeof(float)) };
        return JFX_SCRIPT_OK;
    }
    if (!strcmp(name, "read") || !strcmp(name, "write")) {
        bool write = !strcmp(name, "write");
        if (argc != (write ? 3u : 2u) || args[0].type != JFX_TYPE_BUFFER || !index_value(&args[1], &x))
            return JFX_SCRIPT_TYPE_ERROR;
        size_t size = jfx_buffer_size(args[0].buffer);
        if ((size_t)x >= size / sizeof(float)) return jfx_script_fail(rt, JFX_SCRIPT_ERROR, "buffer index out of bounds");
        float f = 0;
        jfx_result_t result;
        if (write) {
            if (!number(&args[2], &a) || fabs(a) > 3.402823466e38) return JFX_SCRIPT_TYPE_ERROR;
            f = (float)a; result = jfx_buffer_write(args[0].buffer, (size_t)x * sizeof(float), &f, sizeof(f));
        } else result = jfx_buffer_read(args[0].buffer, (size_t)x * sizeof(float), &f, sizeof(f));
        if (result || !isfinite(f)) return JFX_SCRIPT_ERROR;
        if (!write) *out = (jfx_value_t){ .type = JFX_TYPE_FLOAT, .f = f };
        return JFX_SCRIPT_OK;
    }
    if (!strcmp(name, "dimensions")) {
        if (argc != 1 || args[0].type != JFX_TYPE_TEXTURE) return JFX_SCRIPT_TYPE_ERROR;
        *out = (jfx_value_t){ .type = JFX_TYPE_VEC2, .vec2 = {
            (float)jfx_texture_width(args[0].texture), (float)jfx_texture_height(args[0].texture) } };
        return JFX_SCRIPT_OK;
    }
    if (!strcmp(name, "sample") || !strcmp(name, "write_pixel")) {
        bool write = !strcmp(name, "write_pixel");
        if (argc != (write ? 4u : 3u) || args[0].type != JFX_TYPE_TEXTURE ||
            !index_value(&args[1], &x) || !index_value(&args[2], &y) ||
            (write && args[3].type != JFX_TYPE_VEC4)) return JFX_SCRIPT_TYPE_ERROR;
        jfx_texture_t *texture = args[0].texture;
        uint32_t width = jfx_texture_width(texture), height = jfx_texture_height(texture);
        if (x >= width || y >= height) return jfx_script_fail(rt, JFX_SCRIPT_ERROR, "texture coordinates out of bounds");
        jfx_format_t format = jfx_texture_format(texture);
        size_t stride = jfx_format_bytes_per_texel(format);
        uint8_t *pixel = (uint8_t *)jfx_texture_data(texture) + ((size_t)y * width + x) * stride;
        if (format != JFX_FORMAT_R8G8B8A8_UNORM && format != JFX_FORMAT_R32G32B32A32_SFLOAT)
            return JFX_SCRIPT_NOT_SUPPORTED;
        out->type = write ? JFX_TYPE_NIL : JFX_TYPE_VEC4;
        for (size_t i = 0; i < 4; ++i) {
            if (write) {
                float f = args[3].vec4[i];
                if (!isfinite(f)) return JFX_SCRIPT_TYPE_ERROR;
                if (format == JFX_FORMAT_R8G8B8A8_UNORM) pixel[i] = (uint8_t)lroundf(fminf(1, fmaxf(0, f)) * 255);
                else memcpy(pixel + i * sizeof(float), &f, sizeof(f));
            } else if (format == JFX_FORMAT_R8G8B8A8_UNORM) out->vec4[i] = (float)pixel[i] / 255;
            else { memcpy(&out->vec4[i], pixel + i * sizeof(float), sizeof(float)); if (!isfinite(out->vec4[i])) return JFX_SCRIPT_TYPE_ERROR; }
        }
        return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_NOT_FOUND;
}

const char *jfx_script_runtime_last_error(const jfx_script_runtime_t *rt) { return rt ? rt->error : "invalid runtime"; }
jfx_script_status_t jfx_script_runtime_last_status(const jfx_script_runtime_t *rt) { return rt ? rt->last_status : JFX_SCRIPT_INVALID_ARGUMENT; }
size_t jfx_script_runtime_memory_used(const jfx_script_runtime_t *rt) { return rt ? rt->memory_used : 0; }
size_t jfx_script_runtime_memory_limit(const jfx_script_runtime_t *rt) { return rt ? rt->memory_limit : 0; }
void jfx_script_runtime_gc_collect(jfx_script_runtime_t *rt) { if (rt && !rt->busy && !rt->gc_paused) rt->ops->gc(rt, 0); }
void jfx_script_runtime_gc_pause(jfx_script_runtime_t *rt) { if (rt && !rt->busy) { rt->gc_paused = true; rt->ops->gc(rt, 1); } }
void jfx_script_runtime_gc_resume(jfx_script_runtime_t *rt) { if (rt && !rt->busy) { rt->gc_paused = false; rt->ops->gc(rt, 2); } }
