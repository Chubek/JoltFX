#ifndef JFX_LUA_H
#define JFX_LUA_H

#include "jfx/script_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_lua_runtime jfx_lua_runtime_t;

jfx_script_status_t jfx_lua_runtime_create(const jfx_script_config_t *config,
    jfx_lua_runtime_t **out_runtime);
void jfx_lua_runtime_destroy(jfx_lua_runtime_t *runtime);
jfx_script_status_t jfx_lua_runtime_load(jfx_lua_runtime_t *runtime,
    const char *source, const char *name);
jfx_script_status_t jfx_lua_runtime_call_number(jfx_lua_runtime_t *runtime,
    const char *function_name, double input, double *out_result);
void jfx_lua_runtime_gc_collect(jfx_lua_runtime_t *runtime);
void jfx_lua_runtime_gc_pause(jfx_lua_runtime_t *runtime);
void jfx_lua_runtime_gc_resume(jfx_lua_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
