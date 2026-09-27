#include <assert.h>
#include <math.h>

#include "jfx/lua.h"
#include "jfx/mruby.h"

static void near(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-9);
}

int main(void) {
    jfx_script_config_t config = { .memory_limit = 1024 * 1024, .instruction_limit = 10000 };
    jfx_lua_runtime_t *lua = NULL;
    assert(jfx_lua_runtime_create(&config, &lua) == JFX_SCRIPT_OK && lua);
    assert(jfx_lua_runtime_load(lua,
        "function gain(value) return jfx.clamp(value * 2.0, 0.0, 1.0) end", "gain.lua") ==
        JFX_SCRIPT_OK);
    double result = 0.0;
    assert(jfx_lua_runtime_call_number(lua, "gain", 0.75, &result) == JFX_SCRIPT_OK);
    near(result, 1.0);
    assert(jfx_lua_runtime_load(lua, "return os.execute('echo unsafe')", "unsafe.lua") ==
        JFX_SCRIPT_ERROR);
    jfx_lua_runtime_gc_pause(lua);
    jfx_lua_runtime_gc_resume(lua);
    jfx_lua_runtime_gc_collect(lua);
    jfx_lua_runtime_destroy(lua);

    jfx_mruby_runtime_t *mruby = NULL;
    assert(jfx_mruby_runtime_create(&config, &mruby) == JFX_SCRIPT_OK && mruby);
    assert(jfx_mruby_runtime_load(mruby,
        "def gain(value)\n  JFX.clamp(value * 2.0, 0.0, 1.0)\nend", "gain.rb") ==
        JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_call_number(mruby, "gain", 0.75, &result) == JFX_SCRIPT_OK);
    near(result, 1.0);
    assert(jfx_mruby_runtime_load(mruby, "File.open('unsafe')", "unsafe.rb") ==
        JFX_SCRIPT_ERROR);
    jfx_mruby_runtime_gc_pause(mruby);
    jfx_mruby_runtime_gc_resume(mruby);
    jfx_mruby_runtime_gc_collect(mruby);
    jfx_mruby_runtime_destroy(mruby);
    return 0;
}
