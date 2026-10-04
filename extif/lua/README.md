# Lua binding

`jfx_lua` embeds vendored Lua **5.5.1**, using Tilly allocation and Script API 1.0.
`jfx` / `ljoltfx` expose the [shared host functions](../../docs/extensions.md).
Base, table, string, math, coroutine and UTF-8 libraries are available. OS, I/O,
package/debug libraries, dynamic loading, bytecode dumping and script-controlled
GC are omitted. Coroutine instruction hooks share the invocation budget.

INT uses Lua's 64-bit integer; FLOAT uses double. Vector tables use Lua's
one-based component indexing; buffer and texture coordinates are zero-based.
Opaque resource userdata expire at invocation completion. Errors include Lua
tracebacks; loads accept text only. The existing numeric entry points in
`jfx/lua.h` remain supported, with `jfx_lua_runtime_interface` for typed access.

```sh
cmake -S . -B build -DJFX_EXT_LUA=ON
cmake --build build --target jfx_lua joltfx_cli
build/frontends/cli/joltfx scripts run lua extif/examples/grade.lua gain 0.75
```

The runtime is compiled into the adapter archive, with no external Lua package.
Its sources are portable C; platform frontend/toolchain support is inherited from
the engine. `ext_perf` reports actual initial memory and initialization/call cost
for the chosen build rather than assuming a fixed runtime footprint.
