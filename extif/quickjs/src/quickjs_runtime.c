#include "jfx/quickjs.h"
#include "runtime_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "quickjs.h"

typedef struct { JSValue value; uint32_t generation; bool used; } js_ref_t;
typedef struct {
    jfx_script_runtime_t base;
    JSRuntime *runtime;
    JSContext *context;
    js_ref_t refs[JFX_SCRIPT_REFS];
} quickjs_runtime_t;
typedef struct { jfx_value_type_t type; uint64_t token; } js_wrapped_t;
static JSClassID value_class;
static quickjs_runtime_t *as_js(jfx_script_runtime_t *rt) { return (quickjs_runtime_t *)rt; }
static void *allocator(JSMallocState *state, size_t size) { return jfx_script_alloc(state->opaque, NULL, size); }
static void deallocator(JSMallocState *state, void *ptr) { (void)jfx_script_alloc(state->opaque, ptr, 0); }
static void *reallocator(JSMallocState *state, void *ptr, size_t size) { return jfx_script_alloc(state->opaque, ptr, size); }
static int interrupt(JSRuntime *runtime, void *user) { (void)runtime; return !jfx_script_tick(user, 1000); }
static void wrapped_finalizer(JSRuntime *runtime, JSValue value) {
    js_wrapped_t *ptr = JS_GetOpaque(value, value_class);
    if (ptr) (void)jfx_script_alloc(JS_GetRuntimeOpaque(runtime), ptr, 0);
}
static int ref_slot(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    size_t slot = (size_t)(ref & 255u);
    if (!slot || slot > JFX_SCRIPT_REFS) return -1;
    js_ref_t *entry = &as_js(rt)->refs[slot - 1];
    return entry->used && jfx_script_ref_matches(rt, ref, entry->generation) ? (int)(slot - 1) : -1;
}
static jfx_script_status_t pin(jfx_script_runtime_t *rt, JSValueConst value, jfx_script_value_t *out) {
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) {
        js_ref_t *ref = &as_js(rt)->refs[i];
        if (ref->used) continue;
        ref->value = JS_DupValue(as_js(rt)->context, value); ref->used = true;
        if (++ref->generation == 0) ++ref->generation;
        *out = jfx_script_ref_token(rt, ref->generation, i); return JFX_SCRIPT_OK;
    }
    return jfx_script_fail(rt, JFX_SCRIPT_OUT_OF_MEMORY, "script reference table is full");
}
static void release(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    int slot = ref_slot(rt, ref);
    if (slot < 0) return;
    js_ref_t *entry = &as_js(rt)->refs[slot];
    JS_FreeValue(as_js(rt)->context, entry->value); entry->used = false;
}
static jfx_script_status_t exception(jfx_script_runtime_t *rt) {
    JSContext *ctx = as_js(rt)->context;
    JSValue error = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, error);
    if (text) { (void)snprintf(rt->error, sizeof(rt->error), "%s", text); JS_FreeCString(ctx, text); }
    JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
    if (!JS_IsException(stack) && JS_IsString(stack)) {
        text = JS_ToCString(ctx, stack);
        if (text) {
            size_t n = strlen(rt->error);
            (void)snprintf(rt->error + n, sizeof(rt->error) - n, "\n%s", text); JS_FreeCString(ctx, text);
        }
    }
    JS_FreeValue(ctx, stack); JS_FreeValue(ctx, error);
    return rt->fault ? rt->fault : JFX_SCRIPT_ERROR;
}
static JSValue from_value(jfx_script_runtime_t *rt, const jfx_value_t *v) {
    JSContext *ctx = as_js(rt)->context;
    switch (v->type) {
        case JFX_TYPE_NIL: return JS_NULL;
        case JFX_TYPE_BOOL: return JS_NewBool(ctx, v->b);
        case JFX_TYPE_INT: return JS_NewBigInt64(ctx, v->i);
        case JFX_TYPE_FLOAT: return JS_NewFloat64(ctx, v->f);
        case JFX_TYPE_STRING: return JS_NewString(ctx, v->str);
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4: {
            uint32_t n = (uint32_t)(v->type - JFX_TYPE_VEC2 + 2);
            JSValue array = JS_NewArray(ctx);
            if (JS_IsException(array)) return array;
            for (uint32_t i = 0; i < n; ++i) {
                if (JS_SetPropertyUint32(ctx, array, i, JS_NewFloat64(ctx, v->vec4[i])) < 0) {
                    JS_FreeValue(ctx, array); return JS_EXCEPTION;
                }
            }
            return array;
        }
        case JFX_TYPE_COLOR: case JFX_TYPE_BUFFER: case JFX_TYPE_TEXTURE: case JFX_TYPE_USERDATA: {
            uint64_t token = 0;
            if (v->type == JFX_TYPE_COLOR) token = v->color;
            else if (jfx_script_wrap(rt, v, &token)) return JS_ThrowInternalError(ctx, "too many resources");
            JSValue value = JS_NewObjectClass(ctx, (int)value_class);
            if (JS_IsException(value)) return value;
            js_wrapped_t *wrapped = jfx_script_alloc(rt, NULL, sizeof(*wrapped));
            if (!wrapped) { JS_FreeValue(ctx, value); return JS_ThrowOutOfMemory(ctx); }
            *wrapped = (js_wrapped_t){ v->type, token }; JS_SetOpaque(value, wrapped);
            return value;
        }
        case JFX_TYPE_FUNCTION: {
            int slot = ref_slot(rt, v->function);
            if (slot < 0) return JS_ThrowTypeError(ctx, "invalid script reference");
            return JS_DupValue(ctx, as_js(rt)->refs[slot].value);
        }
        default: return JS_ThrowTypeError(ctx, "unsupported native value");
    }
}
static jfx_script_status_t to_value(jfx_script_runtime_t *rt, JSValueConst value,
    bool copy, const char **borrowed, jfx_value_t *out) {
    JSContext *ctx = as_js(rt)->context;
    *out = (jfx_value_t){0};
    if (JS_IsNull(value) || JS_IsUndefined(value)) return JFX_SCRIPT_OK;
    if (JS_IsBool(value)) { out->type = JFX_TYPE_BOOL; out->b = JS_ToBool(ctx, value) != 0; return JFX_SCRIPT_OK; }
    if (JS_IsBigInt(ctx, value)) {
        out->type = JFX_TYPE_INT;
        /* ToBigInt64 wraps modulo 2^64; require an exact round-trip. */
        if (JS_ToBigInt64(ctx, &out->i, value)) return JFX_SCRIPT_TYPE_ERROR;
        JSValue integer = JS_NewBigInt64(ctx, out->i);
        int equal = JS_StrictEq(ctx, value, integer); JS_FreeValue(ctx, integer);
        return equal == 1 ? JFX_SCRIPT_OK : JFX_SCRIPT_TYPE_ERROR;
    }
    if (JS_IsNumber(value)) {
        out->type = JFX_TYPE_FLOAT;
        if (JS_ToFloat64(ctx, &out->f, value) || !isfinite(out->f)) return JFX_SCRIPT_TYPE_ERROR;
        return JFX_SCRIPT_OK;
    }
    if (JS_IsString(value)) {
        size_t length = 0; const char *text = JS_ToCStringLen(ctx, &length, value);
        if (!text) return JFX_SCRIPT_OUT_OF_MEMORY;
        jfx_script_status_t status;
        if (copy) { status = jfx_script_copy_string(rt, text, length, out); JS_FreeCString(ctx, text); }
        else {
            status = memchr(text, 0, length) ? JFX_SCRIPT_TYPE_ERROR : JFX_SCRIPT_OK;
            *borrowed = text; out->type = JFX_TYPE_STRING; out->str = text;
        }
        return status;
    }
    if (JS_IsFunction(ctx, value)) { out->type = JFX_TYPE_FUNCTION; return pin(rt, value, &out->function); }
    js_wrapped_t *wrapped = JS_GetOpaque(value, value_class);
    if (wrapped) {
        if (wrapped->type == JFX_TYPE_COLOR) { out->type = JFX_TYPE_COLOR; out->color = (uint32_t)wrapped->token; return JFX_SCRIPT_OK; }
        return jfx_script_unwrap(rt, wrapped->token, out);
    }
    if (JS_IsArray(ctx, value)) {
        JSValue length = JS_GetPropertyStr(ctx, value, "length"); uint32_t n = 0;
        int error = JS_ToUint32(ctx, &n, length); JS_FreeValue(ctx, length);
        if (error || n < 2 || n > 4) return JFX_SCRIPT_TYPE_ERROR;
        out->type = (jfx_value_type_t)(JFX_TYPE_VEC2 + n - 2);
        for (uint32_t i = 0; i < n; ++i) {
            JSValue element = JS_GetPropertyUint32(ctx, value, i); double f = 0;
            error = !JS_IsNumber(element) || JS_ToFloat64(ctx, &f, element);
            JS_FreeValue(ctx, element);
            if (error || !isfinite(f) || fabs(f) > 3.402823466e38) return JFX_SCRIPT_TYPE_ERROR;
            out->vec4[i] = (float)f;
        }
        return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_TYPE_ERROR;
}
static JSValue host_function(JSContext *ctx, JSValueConst self, int argc,
    JSValueConst *argv, int magic) {
    (void)self;
    jfx_script_runtime_t *rt = JS_GetContextOpaque(ctx);
    if (argc > JFX_SCRIPT_MAX_ARGS) return JS_ThrowTypeError(ctx, "too many host arguments");
    jfx_value_t args[JFX_SCRIPT_MAX_ARGS] = {{0}}, result = {0};
    const char *strings[JFX_SCRIPT_MAX_ARGS] = {0};
    jfx_script_status_t status = JFX_SCRIPT_OK;
    for (int i = 0; i < argc; ++i) { status = to_value(rt, argv[i], false, &strings[i], &args[i]); if (status) break; }
    if (!status) status = jfx_script_host_call(rt, jfx_script_host_names[magic], args, (size_t)argc, &result);
    /* Build the result before releasing borrowed argument strings (identity-like
     * services may return a view of one of them). */
    JSValue value = status ? JS_UNDEFINED : from_value(rt, &result);
    for (int i = 0; i < argc; ++i) {
        if (strings[i]) JS_FreeCString(ctx, strings[i]);
        if (args[i].type == JFX_TYPE_FUNCTION) release(rt, args[i].function);
    }
    if (status) {
        if (!rt->error[0]) (void)jfx_script_fail(rt, status, "invalid or unavailable jfx host operation");
        else if (!rt->fault) rt->fault = status;
        return JS_ThrowTypeError(ctx, "%s", rt->error);
    }
    return value;
}
static jfx_script_status_t load(jfx_script_runtime_t *rt, const void *source, size_t length, const char *name) {
    /* QuickJS requires the source buffer to have a trailing NUL. */
    char *text = jfx_script_alloc(rt, NULL, length + 1);
    if (!text) return JFX_SCRIPT_OUT_OF_MEMORY;
    memcpy(text, source, length); text[length] = 0;
    JSValue result = JS_Eval(as_js(rt)->context, text, length, name, JS_EVAL_TYPE_GLOBAL);
    (void)jfx_script_alloc(rt, text, 0);
    if (JS_IsException(result)) return exception(rt);
    JS_FreeValue(as_js(rt)->context, result); return JFX_SCRIPT_OK;
}
static jfx_script_status_t capture(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t *out) {
    JSContext *ctx = as_js(rt)->context;
    JSValue global = JS_GetGlobalObject(ctx), value = JS_GetPropertyStr(ctx, global, name);
    JS_FreeValue(ctx, global);
    if (JS_IsException(value)) return exception(rt);
    jfx_script_status_t status = JS_IsFunction(ctx, value) ? pin(rt, value, out) : JFX_SCRIPT_NOT_FOUND;
    JS_FreeValue(ctx, value); return status;
}
static jfx_script_status_t call(jfx_script_runtime_t *rt, jfx_script_value_t ref,
    const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    JSContext *ctx = as_js(rt)->context;
    int slot = ref_slot(rt, ref);
    if (slot < 0) return JFX_SCRIPT_TYPE_ERROR;
    JSValue argv[JFX_SCRIPT_MAX_ARGS]; size_t initialized = 0;
    for (; initialized < argc; ++initialized) {
        argv[initialized] = from_value(rt, &args[initialized]);
        if (JS_IsException(argv[initialized])) break;
    }
    JSValue result = initialized == argc ? JS_Call(ctx, as_js(rt)->refs[slot].value, JS_UNDEFINED, (int)argc, argv) : JS_EXCEPTION;
    for (size_t i = 0; i < initialized; ++i) JS_FreeValue(ctx, argv[i]);
    if (JS_IsException(result)) return exception(rt);
    jfx_script_status_t status = to_value(rt, result, true, NULL, out);
    JS_FreeValue(ctx, result); return status;
}
static jfx_script_status_t from(jfx_script_runtime_t *rt, const jfx_value_t *v, jfx_script_value_t *out) {
    JSValue value = from_value(rt, v);
    if (JS_IsException(value)) return exception(rt);
    jfx_script_status_t status = pin(rt, value, out); JS_FreeValue(as_js(rt)->context, value); return status;
}
static jfx_script_status_t to(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_value_t *out) {
    int slot = ref_slot(rt, ref);
    return slot < 0 ? JFX_SCRIPT_TYPE_ERROR : to_value(rt, as_js(rt)->refs[slot].value, true, NULL, out);
}
static jfx_script_status_t clone(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_script_value_t *out) {
    int slot = ref_slot(rt, ref);
    return slot < 0 ? JFX_SCRIPT_TYPE_ERROR : pin(rt, as_js(rt)->refs[slot].value, out);
}
static void gc(jfx_script_runtime_t *rt, int mode) {
    if (!mode) JS_RunGC(as_js(rt)->runtime);
    else JS_SetGCThreshold(as_js(rt)->runtime, mode == 1 ? SIZE_MAX : 256u * 1024u);
}
static void destroy(jfx_script_runtime_t *rt) {
    quickjs_runtime_t *js = as_js(rt);
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) if (js->refs[i].used) JS_FreeValue(js->context, js->refs[i].value);
    if (js->context) JS_FreeContext(js->context);
    if (js->runtime) JS_FreeRuntime(js->runtime);
    jfx_script_base_free(rt);
}
jfx_script_status_t jfx_quickjs_script_create(const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    static const jfx_script_ops_t ops = { destroy, load, capture, call, from, to, clone, release, gc };
    static const JSMallocFunctions alloc = { allocator, deallocator, reallocator, jfx_script_alloc_size };
    jfx_script_status_t status = jfx_script_base_create(sizeof(quickjs_runtime_t), desc, JFX_SCRIPT_QUICKJS, &ops, out);
    if (status) return status;
    quickjs_runtime_t *js = as_js(*out);
    js->runtime = JS_NewRuntime2(&alloc, *out);
    if (!js->runtime) { jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    JS_SetRuntimeOpaque(js->runtime, *out);
    JS_SetMaxStackSize(js->runtime, 256u * 1024u);
    js->context = JS_NewContext(js->runtime);
    if (!js->context) { destroy(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    JS_SetContextOpaque(js->context, *out);
    JSContext *ctx = js->context;
    if (!value_class) JS_NewClassID(&value_class);
    JSClassDef cls = { .class_name = "jfx.Value", .finalizer = wrapped_finalizer };
    if (JS_NewClass(js->runtime, value_class, &cls)) { destroy(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    JSValue global = JS_GetGlobalObject(ctx), jfx = JS_NewObject(ctx);
    for (size_t i = 0; i < jfx_script_host_count; ++i)
        JS_SetPropertyStr(ctx, jfx, jfx_script_host_names[i],
            JS_NewCFunctionMagic(ctx, host_function, jfx_script_host_names[i], 0, JS_CFUNC_generic_magic, (int)i));
    JS_SetPropertyStr(ctx, global, "joltfx", JS_DupValue(ctx, jfx));
    JS_SetPropertyStr(ctx, global, "jfx", jfx); JS_FreeValue(ctx, global);
    static const char sandbox[] =
        "Object.getPrototypeOf(function(){}).constructor=undefined;"
        "Object.getPrototypeOf(function*(){}).constructor=undefined;"
        "Object.getPrototypeOf(async function(){}).constructor=undefined;"
        "Object.getPrototypeOf(async function*(){}).constructor=undefined;"
        "globalThis.eval=undefined;globalThis.Function=undefined;";
    JSValue result = JS_Eval(ctx, sandbox, sizeof(sandbox) - 1, "<sandbox>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result) || js->base.fault) {
        status = JS_IsException(result) ? exception(*out) : js->base.fault;
        if (!JS_IsException(result)) JS_FreeValue(ctx, result);
        destroy(*out); *out = NULL; return status;
    }
    JS_FreeValue(ctx, result);
    JS_SetInterruptHandler(js->runtime, interrupt, *out);
    return JFX_SCRIPT_OK;
}
