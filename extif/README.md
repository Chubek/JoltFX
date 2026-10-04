# Extension language layer

Lua, mruby, QuickJS, MicroPython and Wasmtime share **Script API 1.0** in
`common/include/jfx/script_runtime.h` and `ffi_bridge.h`. Engine clients use
opaque runtimes and typed values; interpreter APIs stay inside each adapter.

| Adapter | CMake option | Default | Script binding |
|---|---|---|---|
| [Lua](lua/README.md) | `JFX_EXT_LUA` | ON | `jfx`, alias `ljoltfx` |
| [mruby](mruby/README.md) | `JFX_EXT_MRUBY` | ON | `JFX` module |
| [QuickJS](quickjs/README.md) | `JFX_EXT_QUICKJS` | OFF | `jfx`, alias `joltfx` |
| [MicroPython](python/README.md) | `JFX_EXT_PYTHON` | OFF | `import jfx` / `pyjoltfx` |
| [Wasmtime](wasm/README.md) | `JFX_EXT_WASM` | OFF | `joltwasm` ABI; imports from `joltfx` |

```sh
cmake -S . -B build -DJFX_EXT_QUICKJS=ON -DJFX_EXT_PYTHON=ON
cmake --build build --target joltfx_cli
build/frontends/cli/joltfx scripts list
build/frontends/cli/joltfx scripts run lua extif/examples/grade.lua gain 0.75
build/frontends/cli/joltfx scripts edit python extif/examples/grade.py \
  extif/examples/sequence.jfx edited.jfx edit
```

The [extension guide](../docs/extensions.md) describes embedding, installation,
types, host functions, capabilities, ownership and budgets. `examples/` supplies
the same grading workflow in all five languages.

```sh
ctest --test-dir build -R ext_conformance --output-on-failure
cmake --build build --target ext_perf   # initialization and cached-call profile
```

Creation, execution, GC, callbacks and destruction use one serialized owner
thread. Scripts can edit the supplied Core editor and access invocation-scoped
buffers/textures. `register_kernel` captures a **runtime-local batch function**;
engine-catalog registration, backend/interface APIs and direct GPU management
belong to the native [plugin SDK](../docs/plugins.md).
