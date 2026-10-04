#include "runtime_internal.h"

static const char *const names[JFX_SCRIPT_LANGUAGE_COUNT] = { "lua", "mruby", "quickjs", "python", "wasm" };
const char *jfx_script_language_name(jfx_script_language_t language) {
    return language >= 0 && language < JFX_SCRIPT_LANGUAGE_COUNT ? names[language] : "unknown";
}
bool jfx_script_language_available(jfx_script_language_t language) {
    switch (language) {
#if JFX_EXT_LUA
        case JFX_SCRIPT_LUA: return true;
#endif
#if JFX_EXT_MRUBY
        case JFX_SCRIPT_MRUBY: return true;
#endif
#if JFX_EXT_QUICKJS
        case JFX_SCRIPT_QUICKJS: return true;
#endif
#if JFX_EXT_PYTHON
        case JFX_SCRIPT_PYTHON: return true;
#endif
#if JFX_EXT_WASM
        case JFX_SCRIPT_WASM: return true;
#endif
        default: return false;
    }
}
jfx_script_status_t jfx_script_runtime_create(jfx_script_language_t language,
    const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    if (!out) return JFX_SCRIPT_INVALID_ARGUMENT;
    *out = NULL;
    switch (language) {
#if JFX_EXT_LUA
        case JFX_SCRIPT_LUA: return jfx_lua_script_create(desc, out);
#endif
#if JFX_EXT_MRUBY
        case JFX_SCRIPT_MRUBY: return jfx_mruby_script_create(desc, out);
#endif
#if JFX_EXT_QUICKJS
        case JFX_SCRIPT_QUICKJS: return jfx_quickjs_script_create(desc, out);
#endif
#if JFX_EXT_PYTHON
        case JFX_SCRIPT_PYTHON: return jfx_python_script_create(desc, out);
#endif
#if JFX_EXT_WASM
        case JFX_SCRIPT_WASM: return jfx_wasm_script_create(desc, out);
#endif
        default: (void)desc; return JFX_SCRIPT_NOT_SUPPORTED;
    }
}
const jfx_ffi_bridge_t *jfx_script_ffi_bridge(void) {
    static const jfx_ffi_bridge_t bridge = {
        sizeof(jfx_ffi_bridge_t), jfx_script_runtime_create, jfx_script_runtime_destroy,
        jfx_script_runtime_load, jfx_script_runtime_call, jfx_script_runtime_register_kernel,
        jfx_script_runtime_invoke_kernel, jfx_script_runtime_to_native,
        jfx_script_runtime_from_native, jfx_script_runtime_on, jfx_script_runtime_gc_collect,
        jfx_script_runtime_gc_pause, jfx_script_runtime_gc_resume
    };
    return &bridge;
}
