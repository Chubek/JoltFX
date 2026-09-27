/* Extension runtime conformance.
 *
 * Both hosts must expose the same numeric scripting surface, enforce the same
 * two budgets, and refuse the same capabilities. The sandbox is capability
 * removal (the libraries that touch the filesystem and the process are not
 * opened, and process control is undefined), so a script fails because the
 * thing it asked for does not exist - not because its text was matched against
 * a blocklist. */

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "jfx/lua.h"
#include "jfx/mruby.h"

static void near(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-9);
}

int main(void) {
    const jfx_script_config_t config = { .memory_limit = 1024 * 1024,
        .instruction_limit = 10000 };

    /* ---- Lua ---- */
    jfx_lua_runtime_t *lua = NULL;
    assert(jfx_lua_runtime_create(&config, &lua) == JFX_SCRIPT_OK && lua);
    assert(jfx_lua_runtime_memory_limit(lua) == config.memory_limit);
    /* The interpreter state itself is charged, so compare against a baseline
     * rather than zero. */
    const size_t lua_baseline = jfx_lua_runtime_memory_used(lua);
    assert(lua_baseline <= config.memory_limit);

    assert(jfx_lua_runtime_load(lua,
        "function gain(value) return jfx.clamp(value * 2.0, 0.0, 1.0) end", "gain.lua") ==
        JFX_SCRIPT_OK);
    double result = 0.0;
    assert(jfx_lua_runtime_call_number(lua, "gain", 0.75, &result) == JFX_SCRIPT_OK);
    near(result, 1.0);
    assert(jfx_lua_runtime_call_number(lua, "gain", -0.25, &result) == JFX_SCRIPT_OK);
    near(result, 0.0);

    /* Capabilities the script must not have. `os` is never opened and
     * dofile/load/loadfile are removed, so these fail as undefined names. */
    assert(jfx_lua_runtime_load(lua, "return os.execute('echo unsafe')", "unsafe.lua") ==
        JFX_SCRIPT_ERROR);
    assert(jfx_lua_runtime_load(lua, "return dofile('/etc/passwd')", "unsafe2.lua") ==
        JFX_SCRIPT_ERROR);
    assert(jfx_lua_runtime_load(lua, "return io.open('/etc/passwd')", "unsafe3.lua") ==
        JFX_SCRIPT_ERROR);
    /* An identifier that merely contains a blocked word must still work: the
     * sandbox is not a source-text filter. */
    assert(jfx_lua_runtime_load(lua,
        "function evaluate(value) return jfx.clamp(value, 0.0, 2.0) end", "names.lua") ==
        JFX_SCRIPT_OK);
    assert(jfx_lua_runtime_call_number(lua, "evaluate", 1.5, &result) == JFX_SCRIPT_OK);
    near(result, 1.5);

    /* Instruction budget. */
    assert(jfx_lua_runtime_load(lua,
        "function exhausted(value) while true do end end", "budget.lua") == JFX_SCRIPT_OK);
    assert(jfx_lua_runtime_call_number(lua, "exhausted", 0.0, &result) == JFX_SCRIPT_ERROR);

    /* Memory budget: allocating far past the limit must fail rather than grow
     * the process. */
    assert(jfx_lua_runtime_load(lua,
        "function hungry(value) local t = {} for i = 1, 2000000 do t[i] = i end return #t end",
        "hungry.lua") == JFX_SCRIPT_OK);
    assert(jfx_lua_runtime_call_number(lua, "hungry", 0.0, &result) != JFX_SCRIPT_OK);
    assert(jfx_lua_runtime_memory_used(lua) <= jfx_lua_runtime_memory_limit(lua));

    /* The accounting must return to its starting figure after the garbage
     * collector runs, so a long-lived host does not drift towards the limit. */
    jfx_lua_runtime_gc_pause(lua);
    jfx_lua_runtime_gc_resume(lua);
    jfx_lua_runtime_gc_collect(lua);
    /* After a full collection the host is back near its baseline: a budget
     * that only ever grew would have been exhausted by now. */
    assert(jfx_lua_runtime_memory_used(lua) <= lua_baseline + config.memory_limit / 4u);

    assert(jfx_lua_runtime_load(NULL, "x", "y") == JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_lua_runtime_load(lua, NULL, "y") == JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_lua_runtime_call_number(lua, "no_such_function", 0.0, &result) ==
        JFX_SCRIPT_NOT_FOUND);
    assert(jfx_lua_runtime_call_number(lua, "gain", NAN, &result) ==
        JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_lua_runtime_memory_used(NULL) == 0u);
    assert(jfx_lua_runtime_memory_limit(NULL) == 0u);
    jfx_lua_runtime_gc_collect(NULL);
    jfx_lua_runtime_destroy(lua);
    jfx_lua_runtime_destroy(NULL);

    /* ---- mruby ---- */
    jfx_mruby_runtime_t *mruby = NULL;
    assert(jfx_mruby_runtime_create(&config, &mruby) == JFX_SCRIPT_OK && mruby);
    assert(jfx_mruby_runtime_memory_limit(mruby) == config.memory_limit);
    const size_t mruby_baseline = jfx_mruby_runtime_memory_used(mruby);
    assert(mruby_baseline <= config.memory_limit);

    assert(jfx_mruby_runtime_load(mruby,
        "def gain(value)\n  JFX.clamp(value * 2.0, 0.0, 1.0)\nend", "gain.rb") ==
        JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_call_number(mruby, "gain", 0.75, &result) == JFX_SCRIPT_OK);
    near(result, 1.0);
    assert(jfx_mruby_runtime_call_number(mruby, "gain", -0.25, &result) == JFX_SCRIPT_OK);
    near(result, 0.0);

    /* Same capabilities, enforced by absence rather than by source matching. */
    assert(jfx_mruby_runtime_load(mruby, "File.open('unsafe')", "unsafe.rb") ==
        JFX_SCRIPT_ERROR);
    assert(jfx_mruby_runtime_load(mruby, "Dir.entries('/')", "unsafe2.rb") ==
        JFX_SCRIPT_ERROR);
    assert(jfx_mruby_runtime_load(mruby, "system('echo unsafe')", "unsafe3.rb") ==
        JFX_SCRIPT_ERROR);
    /* An identifier containing a formerly blocked word must work. */
    assert(jfx_mruby_runtime_load(mruby,
        "def evaluate(value)\n  JFX.clamp(value, 0.0, 2.0)\nend", "names.rb") ==
        JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_call_number(mruby, "evaluate", 1.5, &result) == JFX_SCRIPT_OK);
    near(result, 1.5);

    /* Instruction budget. */
    assert(jfx_mruby_runtime_load(mruby,
        "def exhausted(value)\n  while true\n  end\nend", "budget.rb") == JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_call_number(mruby, "exhausted", 0.0, &result) == JFX_SCRIPT_ERROR);

    /* Memory budget. */
    assert(jfx_mruby_runtime_load(mruby,
        "def hungry(value)\n  a = Array.new(4000000) { |i| i }\n  a.size\nend", "hungry.rb") ==
        JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_call_number(mruby, "hungry", 0.0, &result) != JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_memory_used(mruby) <= jfx_mruby_runtime_memory_limit(mruby));

    jfx_mruby_runtime_gc_pause(mruby);
    jfx_mruby_runtime_gc_resume(mruby);
    jfx_mruby_runtime_gc_collect(mruby);
    assert(jfx_mruby_runtime_memory_used(mruby) <= mruby_baseline + config.memory_limit / 4u);

    assert(jfx_mruby_runtime_load(NULL, "x", "y") == JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_mruby_runtime_load(mruby, NULL, "y") == JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_mruby_runtime_call_number(mruby, "no_such_method", 0.0, &result) !=
        JFX_SCRIPT_OK);
    assert(jfx_mruby_runtime_call_number(mruby, "gain", NAN, &result) ==
        JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_mruby_runtime_memory_used(NULL) == 0u);
    assert(jfx_mruby_runtime_memory_limit(NULL) == 0u);
    jfx_mruby_runtime_gc_collect(NULL);
    jfx_mruby_runtime_destroy(mruby);
    jfx_mruby_runtime_destroy(NULL);
    return 0;
}
