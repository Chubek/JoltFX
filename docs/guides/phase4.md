# Phase 4 components

Phase 4 adds selectable backend adapters, a Dear ImGui desktop frontend, and
sandboxed Lua and mruby hosts. All public targets build through the normal
CMake configuration.

## Backends

The engine accepts `vulkan`, `metal`, `d3d12`, `webgpu`, and `auto` through
`jfx_engine_config_t.backend_name`. `auto` resolves to Vulkan. Vulkan retains
its existing device path. Metal, D3D12, and WebGPU expose the same source and
JBC1 execution API and currently execute through the validated CPU pipeline.
Their capability query reports `cpu_fallback=true`, `gpu_available=false`, and
the device name `software-fallback` until their native platform dispatchers
are implemented.

The fallback makes the effect contract available on any host, including CI.
It does not claim GPU execution or native feature support. Use the backend
conformance CTest to verify the shared pixel behavior:

```bash
ctest --test-dir build -R backend_fallback_conformance --output-on-failure
```

## Desktop frontend

`jfx_desktop` owns an engine instance and composes a Dear ImGui frame with
menu, viewport, timeline, properties, and console panels. It can run one
headless frame for CI and development environments without a window server:

```bash
./build/frontends/desktop/jfx_desktop --headless-smoke --backend webgpu
```

The current target creates and renders ImGui draw data but does not create a
native window or submit the draw data to a platform renderer. Wiring an
SDL/GLFW or native platform shell and a backend renderer is the next desktop
step.

## Script runtimes

`jfx_lua` embeds the vendored Lua source. It opens only base, table, string,
math, coroutine, and UTF-8 libraries. File, process, package, debugger, and
dynamic loader APIs are absent. A `jfx.clamp(value, low, high)` function is
provided. Lua uses Tilly allocation, a memory budget, and an instruction hook.

`jfx_mruby` builds vendored mruby during the CMake build with no stdio,
the compiler gem, and `MRB_USE_DEBUG_HOOK`. It uses Tilly allocation, a
per-call instruction hook, and blocks filesystem, process, eval, and shell
source forms before evaluation. It provides `JFX.clamp(value, low, high)`.

Both bindings expose load and single-number function-call APIs and explicit
GC controls. They are sandboxed scripting hosts, so they do not register
kernels or expose raw buffers, textures, OS APIs, or callbacks.

```c
jfx_script_config_t config = { .memory_limit = 1024 * 1024,
                               .instruction_limit = 10000 };
jfx_lua_runtime_t *runtime = NULL;
jfx_lua_runtime_create(&config, &runtime);
jfx_lua_runtime_load(runtime,
    "function gain(x) return jfx.clamp(x * 2, 0, 1) end", "gain.lua");
double value = 0.0;
jfx_lua_runtime_call_number(runtime, "gain", 0.75, &value);
jfx_lua_runtime_destroy(runtime);
```

Run `ctest --test-dir build -R ext_conformance --output-on-failure` to test
numeric calls, rejected I/O, and instruction budgets for both runtimes.
