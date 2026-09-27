#include "jfx/lua.h"

#include <math.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "tilly/allocator.h"

#define JFX_LUA_HOOK_INTERVAL 1000

struct jfx_lua_runtime {
    lua_State *state;
    size_t memory_limit;
    size_t memory_used;
    uint64_t instruction_limit;
    uint64_t instructions;
};

static void *lua_alloc(void *userdata, void *ptr, size_t old_size, size_t new_size) {
    jfx_lua_runtime_t *runtime = userdata;
    if (new_size > old_size && new_size - old_size > runtime->memory_limit - runtime->memory_used) {
        return NULL;
    }
    if (new_size == 0) {
        if (old_size <= runtime->memory_used) runtime->memory_used -= old_size;
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), ptr);
        return NULL;
    }
    void *next = ptr ? tilly_realloc((tilly_allocator_t *)tilly_default_allocator(), ptr, new_size) :
        tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), new_size, _Alignof(max_align_t));
    if (next) {
        if (new_size >= old_size) runtime->memory_used += new_size - old_size;
        else runtime->memory_used -= old_size - new_size;
    }
    return next;
}

static int lua_clamp(lua_State *state) {
    double value = luaL_checknumber(state, 1);
    double low = luaL_checknumber(state, 2);
    double high = luaL_checknumber(state, 3);
    if (!isfinite(value) || !isfinite(low) || !isfinite(high) || low > high) {
        return luaL_error(state, "jfx.clamp expects finite bounds with low <= high");
    }
    if (value < low) value = low;
    if (value > high) value = high;
    lua_pushnumber(state, value);
    return 1;
}

static void lua_budget_hook(lua_State *state, lua_Debug *debug) {
    (void)debug;
    void *userdata = NULL;
    (void)lua_getallocf(state, &userdata);
    jfx_lua_runtime_t *runtime = userdata;
    runtime->instructions += JFX_LUA_HOOK_INTERVAL;
    if (runtime->instructions > runtime->instruction_limit) {
        (void)luaL_error(state, "instruction budget exceeded");
    }
}

static void open_safe_libraries(lua_State *state) {
    luaL_requiref(state, "_G", luaopen_base, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(state, 1);
    lua_pushnil(state); lua_setglobal(state, "dofile");
    lua_pushnil(state); lua_setglobal(state, "load");
    lua_pushnil(state); lua_setglobal(state, "loadfile");
    lua_pushnil(state); lua_setglobal(state, "collectgarbage");
    lua_newtable(state);
    lua_pushcfunction(state, lua_clamp); lua_setfield(state, -2, "clamp");
    lua_setglobal(state, "jfx");
}

jfx_script_status_t jfx_lua_runtime_create(const jfx_script_config_t *config,
    jfx_lua_runtime_t **out_runtime) {
    if (!out_runtime) return JFX_SCRIPT_INVALID_ARGUMENT;
    *out_runtime = NULL;
    jfx_lua_runtime_t *runtime = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*runtime), _Alignof(jfx_lua_runtime_t));
    if (!runtime) return JFX_SCRIPT_OUT_OF_MEMORY;
    memset(runtime, 0, sizeof(*runtime));
    runtime->memory_limit = config && config->memory_limit ? config->memory_limit : 4u * 1024u * 1024u;
    runtime->instruction_limit = config && config->instruction_limit ? config->instruction_limit : 100000u;
    runtime->state = lua_newstate(lua_alloc, runtime, 0);
    if (!runtime->state) { tilly_free((tilly_allocator_t *)tilly_default_allocator(), runtime); return JFX_SCRIPT_OUT_OF_MEMORY; }
    open_safe_libraries(runtime->state);
    lua_sethook(runtime->state, lua_budget_hook, LUA_MASKCOUNT, JFX_LUA_HOOK_INTERVAL);
    *out_runtime = runtime;
    return JFX_SCRIPT_OK;
}

void jfx_lua_runtime_destroy(jfx_lua_runtime_t *runtime) {
    if (!runtime) return;
    lua_close(runtime->state);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), runtime);
}

jfx_script_status_t jfx_lua_runtime_load(jfx_lua_runtime_t *runtime,
    const char *source, const char *name) {
    if (!runtime || !source || !name) return JFX_SCRIPT_INVALID_ARGUMENT;
    runtime->instructions = 0;
    if (luaL_loadbufferx(runtime->state, source, strlen(source), name, "t") != LUA_OK ||
        lua_pcall(runtime->state, 0, 0, 0) != LUA_OK) {
        lua_pop(runtime->state, 1);
        return JFX_SCRIPT_ERROR;
    }
    return JFX_SCRIPT_OK;
}

jfx_script_status_t jfx_lua_runtime_call_number(jfx_lua_runtime_t *runtime,
    const char *function_name, double input, double *out_result) {
    if (!runtime || !function_name || !out_result || !isfinite(input)) return JFX_SCRIPT_INVALID_ARGUMENT;
    runtime->instructions = 0;
    lua_getglobal(runtime->state, function_name);
    if (lua_type(runtime->state, -1) != LUA_TFUNCTION) { lua_pop(runtime->state, 1); return JFX_SCRIPT_NOT_FOUND; }
    lua_pushnumber(runtime->state, input);
    if (lua_pcall(runtime->state, 1, 1, 0) != LUA_OK) { lua_pop(runtime->state, 1); return JFX_SCRIPT_ERROR; }
    int is_number = 0;
    double result = lua_tonumberx(runtime->state, -1, &is_number);
    lua_pop(runtime->state, 1);
    if (!is_number || !isfinite(result)) return JFX_SCRIPT_ERROR;
    *out_result = result;
    return JFX_SCRIPT_OK;
}

void jfx_lua_runtime_gc_collect(jfx_lua_runtime_t *runtime) { if (runtime) (void)lua_gc(runtime->state, LUA_GCCOLLECT, 0); }
void jfx_lua_runtime_gc_pause(jfx_lua_runtime_t *runtime) { if (runtime) (void)lua_gc(runtime->state, LUA_GCSTOP, 0); }
void jfx_lua_runtime_gc_resume(jfx_lua_runtime_t *runtime) { if (runtime) (void)lua_gc(runtime->state, LUA_GCRESTART, 0); }
