#include "jfx/lua.h"
#include "runtime_internal.h"
#include <math.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

typedef struct { int key; uint32_t generation; } lua_ref_t;
struct jfx_lua_runtime {
    jfx_script_runtime_t base;
    lua_State *state;
    lua_ref_t refs[JFX_SCRIPT_REFS];
};
typedef struct { jfx_value_type_t type; uint64_t token; } lua_wrapped_t;
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
} lua_job_t;

static struct jfx_lua_runtime *as_lua(jfx_script_runtime_t *rt) { return (struct jfx_lua_runtime *)rt; }
static jfx_script_runtime_t *lua_owner(lua_State *L) {
    void *owner = NULL; (void)lua_getallocf(L, &owner); return owner;
}
static void *lua_allocator(void *owner, void *ptr, size_t old, size_t size) {
    (void)old; return jfx_script_alloc(owner, ptr, size);
}
static int ref_slot(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    size_t slot = (size_t)(ref & 255u);
    if (!slot || slot > JFX_SCRIPT_REFS) return -1;
    lua_ref_t *entry = &as_lua(rt)->refs[slot - 1];
    return entry->key && jfx_script_ref_matches(rt, ref, entry->generation) ? (int)(slot - 1) : -1;
}
static jfx_script_status_t pin(lua_State *L, int index, jfx_script_value_t *out) {
    jfx_script_runtime_t *rt = lua_owner(L);
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) {
        lua_ref_t *ref = &as_lua(rt)->refs[i];
        if (ref->key) continue;
        lua_pushvalue(L, index);
        ref->key = luaL_ref(L, LUA_REGISTRYINDEX);
        /* LUA_REFNIL is still an owned reference. */
        if (++ref->generation == 0) ++ref->generation;
        *out = jfx_script_ref_token(rt, ref->generation, i); return JFX_SCRIPT_OK;
    }
    return jfx_script_fail(rt, JFX_SCRIPT_OUT_OF_MEMORY, "script reference table is full");
}
static void release(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    int slot = ref_slot(rt, ref);
    if (slot < 0) return;
    lua_ref_t *entry = &as_lua(rt)->refs[slot];
    luaL_unref(as_lua(rt)->state, LUA_REGISTRYINDEX, entry->key); entry->key = 0;
}
static void push_ref(lua_State *L, jfx_script_value_t ref) {
    jfx_script_runtime_t *rt = lua_owner(L);
    int slot = ref_slot(rt, ref);
    if (slot < 0) { (void)jfx_script_fail(rt, JFX_SCRIPT_TYPE_ERROR, "invalid script reference"); luaL_error(L, "invalid script reference"); }
    int key = as_lua(rt)->refs[slot].key;
    if (key == LUA_REFNIL) lua_pushnil(L); else lua_rawgeti(L, LUA_REGISTRYINDEX, key);
}

static jfx_script_status_t from_value(lua_State *L, const jfx_value_t *v) {
    jfx_script_runtime_t *rt = lua_owner(L);
    switch (v->type) {
        case JFX_TYPE_NIL: lua_pushnil(L); break;
        case JFX_TYPE_BOOL: lua_pushboolean(L, v->b); break;
        case JFX_TYPE_INT: lua_pushinteger(L, (lua_Integer)v->i); break;
        case JFX_TYPE_FLOAT: lua_pushnumber(L, v->f); break;
        case JFX_TYPE_STRING: lua_pushstring(L, v->str); break;
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4: {
            int n = (int)v->type - (int)JFX_TYPE_VEC2 + 2;
            lua_createtable(L, n, 0);
            for (int i = 0; i < n; ++i) { lua_pushnumber(L, v->vec4[i]); lua_rawseti(L, -2, i + 1); }
            break;
        }
        case JFX_TYPE_COLOR: case JFX_TYPE_BUFFER: case JFX_TYPE_TEXTURE: case JFX_TYPE_USERDATA: {
            uint64_t token = v->color;
            if (v->type != JFX_TYPE_COLOR) {
                jfx_script_status_t status = jfx_script_wrap(rt, v, &token); if (status) return status;
            }
            lua_wrapped_t *wrapped = lua_newuserdatauv(L, sizeof(*wrapped), 0);
            wrapped->type = v->type; wrapped->token = token;
            luaL_setmetatable(L, "jfx.Value"); break;
        }
        case JFX_TYPE_FUNCTION: push_ref(L, v->function); break;
        default: return JFX_SCRIPT_TYPE_ERROR;
    }
    return JFX_SCRIPT_OK;
}
static jfx_script_status_t to_value(lua_State *L, int index, bool copy, jfx_value_t *out) {
    jfx_script_runtime_t *rt = lua_owner(L);
    index = lua_absindex(L, index);
    *out = (jfx_value_t){0};
    switch (lua_type(L, index)) {
        case LUA_TNIL: return JFX_SCRIPT_OK;
        case LUA_TBOOLEAN: out->type = JFX_TYPE_BOOL; out->b = lua_toboolean(L, index) != 0; return JFX_SCRIPT_OK;
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) { out->type = JFX_TYPE_INT; out->i = (int64_t)lua_tointeger(L, index); }
            else { out->type = JFX_TYPE_FLOAT; out->f = lua_tonumber(L, index); if (!isfinite(out->f)) return JFX_SCRIPT_TYPE_ERROR; }
            return JFX_SCRIPT_OK;
        case LUA_TSTRING: {
            size_t length = 0; const char *text = lua_tolstring(L, index, &length);
            if (copy) return jfx_script_copy_string(rt, text, length, out);
            if (memchr(text, 0, length)) return JFX_SCRIPT_TYPE_ERROR;
            out->type = JFX_TYPE_STRING; out->str = text; return JFX_SCRIPT_OK;
        }
        case LUA_TFUNCTION: out->type = JFX_TYPE_FUNCTION; return pin(L, index, &out->function);
        case LUA_TUSERDATA: {
            lua_wrapped_t *v = luaL_testudata(L, index, "jfx.Value");
            if (!v) return JFX_SCRIPT_TYPE_ERROR;
            if (v->type == JFX_TYPE_COLOR) { out->type = JFX_TYPE_COLOR; out->color = (uint32_t)v->token; return JFX_SCRIPT_OK; }
            return jfx_script_unwrap(rt, v->token, out);
        }
        case LUA_TTABLE: {
            size_t n = lua_rawlen(L, index);
            if (n < 2 || n > 4) return JFX_SCRIPT_TYPE_ERROR;
            out->type = (jfx_value_type_t)(JFX_TYPE_VEC2 + n - 2);
            for (size_t i = 0; i < n; ++i) {
                lua_rawgeti(L, index, (lua_Integer)i + 1);
                if (lua_type(L, -1) != LUA_TNUMBER) { lua_pop(L, 1); return JFX_SCRIPT_TYPE_ERROR; }
                double number = lua_tonumber(L, -1); lua_pop(L, 1);
                if (!isfinite(number) || fabs(number) > 3.402823466e38) return JFX_SCRIPT_TYPE_ERROR;
                out->vec4[i] = (float)number;
            }
            return JFX_SCRIPT_OK;
        }
        default: return JFX_SCRIPT_TYPE_ERROR;
    }
}

static int host_function(lua_State *L) {
    jfx_script_runtime_t *rt = lua_owner(L);
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    int argc = lua_gettop(L);
    if (argc > JFX_SCRIPT_MAX_ARGS) return luaL_error(L, "too many host arguments");
    jfx_value_t args[JFX_SCRIPT_MAX_ARGS] = {{0}}, result = {0};
    jfx_script_status_t status = JFX_SCRIPT_OK;
    for (int i = 0; i < argc; ++i) { status = to_value(L, i + 1, false, &args[i]); if (status) break; }
    if (!status) status = jfx_script_host_call(rt, name, args, (size_t)argc, &result);
    for (int i = 0; i < argc; ++i) if (args[i].type == JFX_TYPE_FUNCTION) release(rt, args[i].function);
    if (status) {
        if (!rt->error[0]) (void)jfx_script_fail(rt, status, "invalid or unavailable jfx host operation");
        else if (!rt->fault) rt->fault = status;
        return luaL_error(L, "%s", rt->error);
    }
    status = from_value(L, &result);
    if (status) return luaL_error(L, "cannot marshal host result");
    return 1;
}
static void hook(lua_State *L, lua_Debug *debug) {
    (void)debug;
    if (!jfx_script_tick(lua_owner(L), 100)) luaL_error(L, "instruction budget exceeded");
}
static int value_string(lua_State *L) { lua_pushliteral(L, "jfx.Value"); return 1; }
static int setup(lua_State *L) {
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    const char *const removed[] = { "load", "loadfile", "dofile", "collectgarbage", "print", "warn" };
    for (size_t i = 0; i < sizeof(removed) / sizeof(removed[0]); ++i) { lua_pushnil(L); lua_setglobal(L, removed[i]); }
    lua_getglobal(L, "string"); lua_pushnil(L); lua_setfield(L, -2, "dump"); lua_pop(L, 1);
    luaL_newmetatable(L, "jfx.Value");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "__metatable");
    lua_pushcfunction(L, value_string); lua_setfield(L, -2, "__tostring"); lua_pop(L, 1);
    lua_newtable(L);
    for (size_t i = 0; i < jfx_script_host_count; ++i) {
        lua_pushstring(L, jfx_script_host_names[i]); lua_pushcclosure(L, host_function, 1);
        lua_setfield(L, -2, jfx_script_host_names[i]);
    }
    lua_pushvalue(L, -1); lua_setglobal(L, "ljoltfx"); lua_setglobal(L, "jfx");
    return 0;
}
static int traceback(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "Lua error", 1); return 1;
}
static jfx_script_status_t error_status(jfx_script_runtime_t *rt, int status) {
    if (status == LUA_OK) return JFX_SCRIPT_OK;
    const char *error = lua_tostring(as_lua(rt)->state, -1);
    jfx_script_status_t code = rt->fault ? rt->fault : status == LUA_ERRMEM ? JFX_SCRIPT_OUT_OF_MEMORY : JFX_SCRIPT_ERROR;
    return jfx_script_fail(rt, code, error ? error : "Lua exception");
}
static int job_entry(lua_State *L) {
    lua_job_t *job = lua_touserdata(L, 1);
    lua_settop(L, 0);
    job->status = JFX_SCRIPT_OK;
    switch (job->operation) {
        case 0: {
            int status = luaL_loadbufferx(L, job->source, job->length, job->name, "t");
            if (status != LUA_OK) return lua_error(L);
            lua_call(L, 0, 0); break;
        }
        case 1:
            lua_getglobal(L, job->name);
            if (!lua_isfunction(L, -1)) job->status = JFX_SCRIPT_NOT_FOUND;
            else job->status = pin(L, -1, job->out_ref);
            break;
        case 2:
            push_ref(L, job->ref);
            for (size_t i = 0; i < job->argc; ++i) {
                job->status = from_value(L, &job->args[i]); if (job->status) return 0;
            }
            lua_call(L, (int)job->argc, 1);
            job->status = to_value(L, -1, true, job->out); break;
        case 3:
            job->status = from_value(L, job->args);
            if (!job->status) job->status = pin(L, -1, job->out_ref);
            break;
        case 4: push_ref(L, job->ref); job->status = to_value(L, -1, true, job->out); break;
        case 5: push_ref(L, job->ref); job->status = pin(L, -1, job->out_ref); break;
        default: job->status = JFX_SCRIPT_INVALID_ARGUMENT; break;
    }
    return 0;
}
static jfx_script_status_t run(lua_job_t *job) {
    lua_State *L = as_lua(job->rt)->state;
    int top = lua_gettop(L);
    lua_pushcfunction(L, traceback); int handler = lua_gettop(L);
    lua_pushcfunction(L, job_entry); lua_pushlightuserdata(L, job);
    int status = lua_pcall(L, 1, 0, handler);
    jfx_script_status_t result = status == LUA_OK ? job->status : error_status(job->rt, status);
    lua_settop(L, top);
    return result;
}
static jfx_script_status_t load(jfx_script_runtime_t *rt, const void *s, size_t n, const char *name) {
    lua_job_t job = { .rt = rt, .source = s, .length = n, .name = name }; return run(&job);
}
static jfx_script_status_t capture(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t *out) {
    lua_job_t job = { .rt = rt, .operation = 1, .name = name, .out_ref = out }; return run(&job);
}
static jfx_script_status_t call(jfx_script_runtime_t *rt, jfx_script_value_t ref, const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    lua_job_t job = { .rt = rt, .operation = 2, .ref = ref, .args = args, .argc = argc, .out = out }; return run(&job);
}
static jfx_script_status_t from(jfx_script_runtime_t *rt, const jfx_value_t *value, jfx_script_value_t *out) {
    lua_job_t job = { .rt = rt, .operation = 3, .args = value, .out_ref = out }; return run(&job);
}
static jfx_script_status_t to(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_value_t *out) {
    lua_job_t job = { .rt = rt, .operation = 4, .ref = ref, .out = out }; return run(&job);
}
static jfx_script_status_t clone(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_script_value_t *out) {
    lua_job_t job = { .rt = rt, .operation = 5, .ref = ref, .out_ref = out }; return run(&job);
}
static void destroy(jfx_script_runtime_t *rt) { lua_close(as_lua(rt)->state); jfx_script_base_free(rt); }
static void gc(jfx_script_runtime_t *rt, int mode) {
    (void)lua_gc(as_lua(rt)->state, mode == 0 ? LUA_GCCOLLECT : mode == 1 ? LUA_GCSTOP : LUA_GCRESTART);
}
jfx_script_status_t jfx_lua_script_create(const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    static const jfx_script_ops_t ops = { destroy, load, capture, call, from, to, clone, release, gc };
    jfx_script_status_t status = jfx_script_base_create(sizeof(struct jfx_lua_runtime), desc, JFX_SCRIPT_LUA, &ops, out);
    if (status) return status;
    struct jfx_lua_runtime *rt = as_lua(*out);
    rt->state = lua_newstate(lua_allocator, &rt->base, 0);
    if (!rt->state) { jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    lua_pushcfunction(rt->state, setup);
    int code = lua_pcall(rt->state, 0, 0, 0);
    if (code != LUA_OK) { destroy(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    lua_sethook(rt->state, hook, LUA_MASKCOUNT, 100);
    return JFX_SCRIPT_OK;
}

/* ABI-compatible numeric entry points retain their original error semantics. */
static jfx_script_status_t legacy(jfx_script_status_t s) {
    return s == JFX_SCRIPT_BUDGET || s == JFX_SCRIPT_TYPE_ERROR ? JFX_SCRIPT_ERROR : s;
}
jfx_script_status_t jfx_lua_runtime_create(const jfx_script_config_t *config, jfx_lua_runtime_t **out) {
    if (!out) return JFX_SCRIPT_INVALID_ARGUMENT;
    jfx_script_desc_t desc = { .size = sizeof(desc) }; if (config) desc.config = *config;
    jfx_script_runtime_t *rt = NULL;
    jfx_script_status_t status = jfx_lua_script_create(&desc, &rt); *out = (jfx_lua_runtime_t *)rt; return status;
}
void jfx_lua_runtime_destroy(jfx_lua_runtime_t *rt) { jfx_script_runtime_destroy((jfx_script_runtime_t *)rt); }
jfx_script_status_t jfx_lua_runtime_load(jfx_lua_runtime_t *rt, const char *source, const char *name) {
    return source ? legacy(jfx_script_runtime_load((jfx_script_runtime_t *)rt, source, strlen(source), name)) : JFX_SCRIPT_INVALID_ARGUMENT;
}
jfx_script_status_t jfx_lua_runtime_call_number(jfx_lua_runtime_t *rt, const char *name, double input, double *out) {
    return legacy(jfx_script_runtime_call_number((jfx_script_runtime_t *)rt, name, input, out));
}
size_t jfx_lua_runtime_memory_used(const jfx_lua_runtime_t *rt) { return jfx_script_runtime_memory_used((const jfx_script_runtime_t *)rt); }
size_t jfx_lua_runtime_memory_limit(const jfx_lua_runtime_t *rt) { return jfx_script_runtime_memory_limit((const jfx_script_runtime_t *)rt); }
void jfx_lua_runtime_gc_collect(jfx_lua_runtime_t *rt) { jfx_script_runtime_gc_collect((jfx_script_runtime_t *)rt); }
void jfx_lua_runtime_gc_pause(jfx_lua_runtime_t *rt) { jfx_script_runtime_gc_pause((jfx_script_runtime_t *)rt); }
void jfx_lua_runtime_gc_resume(jfx_lua_runtime_t *rt) { jfx_script_runtime_gc_resume((jfx_script_runtime_t *)rt); }
jfx_script_runtime_t *jfx_lua_runtime_interface(jfx_lua_runtime_t *rt) { return (jfx_script_runtime_t *)rt; }
