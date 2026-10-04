# Extension scripting

JoltFX embeds Lua, mruby, QuickJS, MicroPython and Wasmtime over the same Core
editor, resources, events and diagnostics. Script API **1.0** exposes
`jfx_script_runtime_t`, `jfx_value_t` and a language-neutral `jfx_ffi_bridge_t`.
Its headers and enabled adapters are installed with the engine.

## Build and run

Lua and mruby are enabled by default. QuickJS and MicroPython use vendored
sources and are opt-in; mruby needs Ruby and MicroPython header generation needs
Python 3 and Make. Wasmtime needs a separate version **38+ C API** installation.

```sh
cmake -S . -B build -DJFX_EXT_QUICKJS=ON -DJFX_EXT_PYTHON=ON \
  -DJFX_EXT_WASM=ON -DJFX_WASMTIME_ROOT=/path/to/wasmtime-c-api
cmake --build build --parallel
ctest --test-dir build -R ext_conformance --output-on-failure
```

All options are independent, including configurations with every runtime off.
`jfx_script_language_available` and `joltfx scripts list` report the actual build.
Unavailable languages return `JFX_SCRIPT_NOT_SUPPORTED` from the factory.

```sh
CLI=build/frontends/cli/joltfx
$CLI scripts list
$CLI scripts run lua extif/examples/grade.lua gain 0.75   # prints 1
$CLI scripts edit lua extif/examples/grade.lua \
  extif/examples/sequence.jfx edited.jfx edit
$CLI nle render edited.jfx -o frame --start 0 --end 1
```

Replace `lua` / `.lua` with `mruby` / `.rb`, `quickjs` / `.js`, `python` / `.py`
or `wasm` / `.wat` for the matching example. `scripts run LANG FILE` loads and
executes the file. An optional function takes zero arguments, or one finite
double when `NUMBER` is supplied. `scripts edit` loads the input document first,
executes the script and optional zero-argument function, then writes the project
on success. Script, input and function failures preserve existing output.
The CLI reads at most 1 MiB of script source and uses the default runtime budgets.

## Embedding

Link `jfx_extif` in the source tree, or the installed `JoltFX::jfx_extif` target.
This aggregate includes the factory and every enabled adapter. Linking an
individual adapter also allows its `jfx_*_script_create` entry point.

```cmake
cmake_minimum_required(VERSION 3.20)
project(script_host LANGUAGES C CXX)
find_package(JoltFX CONFIG REQUIRED)
add_executable(script_host main.c)
target_link_libraries(script_host PRIVATE JoltFX::jfx_extif)
```

Use `cmake --install build --prefix /path/to/joltfx`, then set
`CMAKE_PREFIX_PATH=/path/to/joltfx` in the consumer. The package resolves its
system dependencies; if Wasmtime is enabled, supply its prefix in the consumer
too. The mruby runtime archive is installed beside its adapter. A C++ linker is
required by the engine's color adapter; C hosts can enable both C and CXX in
CMake, as above. The native plugin SDK has its own header-only package.

```c
#include <stdio.h>
#include <string.h>
#include "jfx/ffi_bridge.h"

int main(void) {
    jfx_editor_t *editor = jfx_editor_create(320, 180);
    if (!editor) return 1;
    jfx_script_desc_t desc = { .size = sizeof(desc), .editor = editor,
        .capabilities = JFX_SCRIPT_CAP_EDITOR };
    jfx_script_runtime_t *runtime = NULL;
    const char *source = "function gain(x) return jfx.clamp(x*2,0,1) end";
    jfx_script_status_t status = jfx_script_runtime_create(JFX_SCRIPT_LUA, &desc, &runtime);
    if (!status) status = jfx_script_runtime_load(runtime, source, strlen(source), "gain.lua");
    double result = 0;
    if (!status) status = jfx_script_runtime_call_number(runtime, "gain", 0.75, &result);
    if (status) fprintf(stderr, "%s\n", jfx_script_runtime_last_error(runtime));
    else printf("%.17g\n", result);
    jfx_script_runtime_destroy(runtime);
    jfx_editor_destroy(editor);
    return status ? 1 : 0;
}
```

Initialize an engine when using its buffers/textures or events. The editor,
allocator and optional TillyZ error context are borrowed and must outlive the
runtime. A NULL descriptor selects no editor/event capabilities. All extension
operations and event publication use **one serialized owner thread**; each
runtime permits one active invocation. Reentrant execution returns `BUSY` and
callbacks to an already executing runtime are skipped. Lifecycle mutation and
cross-runtime execution from callbacks must be deferred to the owner loop.

The bridge table provides matching lifecycle, execution, local-batch,
translation, callback and GC methods. Lua/mruby's existing numeric APIs remain
usable; `jfx_lua_runtime_interface` / `jfx_mruby_runtime_interface` exposes the
common handle. Their legacy calls retain the old generic script-error mapping
for budget/type failures.

## Typed values and ownership

| Native type | Script representation |
|---|---|
| NIL, BOOL | nil/boolean, null/boolean or None/bool |
| INT | signed 64-bit integer; **BigInt** in QuickJS |
| FLOAT | finite double; **Number** in QuickJS |
| VEC2/3/4 | table, array or list/tuple of 2–4 finite float components |
| COLOR | opaque value constructed by `jfx.color(packed_rgba)` |
| STRING | NUL-free UTF-8 text |
| BUFFER, TEXTURE, USERDATA | opaque invocation-scoped wrapper |
| FUNCTION | owned runtime-local callable reference |

The boundary does not silently coerce strings or booleans into numbers. Vectors
use float components; scalar INT and FLOAT round-trip their native precision.
QuickJS Number returns FLOAT even when integral. BigInts and MicroPython integers
outside int64 fail translation. Wasmtime encodes these types as value records
or uses strict scalar export signatures; see [its ABI guide](../extif/wasm/README.md).

At most 16 native arguments cross a call. Successful result strings are borrowed
until a subsequent execution/translation or runtime destruction. Copy a string
before retaining it or using it as an argument in another operation.
Function results and `from_native` references are owned: release them through
`jfx_script_runtime_release_value`. References reject use by another runtime and
use after release. Standalone references to borrowed buffers, textures or
userdata are rejected. Invocation arguments may contain these resources, and
their wrappers expire at the end of that invocation, even when stored in globals.

Call/translation outputs remain unchanged on failure. Host resource writes and
editor commands are immediate; an exception does not undo earlier writes.
Additive loads may leave script globals and editor edits made before an error.
New runtime-owned event subscriptions and local batch registrations from a
failed load are removed. The host can group editor work using editor transactions.
Wasmtime permits one successfully loaded module per runtime.

## Shared host functions

Lua/QuickJS use `jfx`, mruby uses `JFX`, and MicroPython uses `import jfx`.
Lua also exposes `ljoltfx`, QuickJS exposes `joltfx`, and MicroPython accepts
`import pyjoltfx`. Wasmtime calls the same services through `joltfx.call`.

| Function | Contract |
|---|---|
| `clamp(value, low, high)` | finite numbers with `low <= high`; returns FLOAT |
| `log(text)` | send text to Tilly diagnostics |
| `color(packed_rgba)` | unsigned 32-bit packed color, returned as COLOR |
| `command(op, a, b, c, value, text)` | shared `jfx_editor_command`; EDITOR capability |
| `state("sequence" or "graph")` | Core state JSON; EDITOR capability |
| `on(event, function_or_name)` | own an event subscription; returns INT ID; EVENTS capability |
| `off(id)` | remove an owned subscription; script-side removal completes after invocation |
| `register_kernel(name, function_or_name)` | capture a runtime-local batch function |
| `size(buffer)` | float-element count |
| `read(buffer, index)` / `write(buffer, index, value)` | zero-based float access with bounds checks |
| `dimensions(texture)` | VEC2 of width and height |
| `sample(texture, x, y)` / `write_pixel(texture, x, y, rgba)` | zero-based pixel access; RGBA8 or RGBA32F |

Indices are nonnegative integral values within uint32. RGBA8 writes clamp to
0–1; RGBA32F retains finite HDR values. RGBA16F returns `NOT_SUPPORTED`.
Accessors address the supplied resources without copying whole images/buffers.
Only the host creates resources; scripts do not own their native lifetime.
Editor `a/b/c` and text meanings follow the [NLE](nle.md),
[composition](composition.md) and [color](editor.md) command references.

`register_kernel` stores a cached callable for `jfx_script_runtime_invoke_kernel`.
Replacing a same-named global later preserves the captured function; duplicate
local names return BUSY. It does not register an effect in the engine catalog.
Native [plugins](plugins.md) supply that functionality, backend/interface access
and direct GPU operations. Each runtime supports 32 local batches, 32 owned
subscriptions, 128 explicit references and 32 resource wrappers per invocation.

## Events, errors and budgets

Events are synchronous. Names are `frame_begin`, `frame_end`, `kernel_submit`,
`kernel_complete`, `kernel_error`, `resource_alloc`, `resource_free`,
`timeline_play`, `timeline_pause`, `asset_load`, `asset_unload`, `ui_input`,
`plugin_load` and `plugin_unload`. A script callback receives the event-name
string; native payload pointers never cross the boundary. Destruction removes
only that runtime's subscriptions. Callback errors populate `last_status` and
`last_error`, log to Tilly and reach the optional TillyZ error context.

The default budget is **4 MiB** of charged runtime memory and **100,000 execution
units per invocation**. Set `desc.config` to override either; zero selects its
default. Tilly supplies the runtime allocations, including MicroPython's fixed
heap and Wasmtime's linear memories. Wasmtime's compiler, executable code and
Rust runtime metadata use Wasmtime's own allocator and are outside this byte
counter. Native engine resources/editor history have their own engine budgets.

Lua hooks count in blocks of 100 VM instructions; mruby checks fetched VM
instructions; QuickJS's interrupt checks charge blocks of 1,000; MicroPython
checks loop/return boundaries; Wasmtime consumes fuel. Every shared service also
charges a unit. These are runtime-specific execution budgets, not interchangeable
instruction counts or hard wall-clock deadlines for compilation/native builtins.
Budget errors are recoverable; a subsequent invocation gets a fresh budget.

Sandboxed libraries omit direct file/process/network/native-library access and
dynamic source/bytecode loading. Wasmtime receives no WASI imports. Core editor
commands remain host-mediated and follow the granted EDITOR capability.
GC collect/pause/resume belongs to the host; MicroPython collects at host
boundaries, never while unrooted native stack temporaries are live. Hosts can
pause collectors around frame work and collect between frames. Paused heaps
remain bounded and can return OUT_OF_MEMORY.

## Verification and profiling

`ext_conformance*` covers typed precision, resource access/expiry/bounds, cached
function stability, independent runtimes, callback ownership/self-removal,
failed-load cleanup, memory/instruction/recursion limits, recovery and sandbox
restrictions. CLI conformance runs every enabled example, grades a real project
and checks its rendered pixels and failure-preserved output. The all-language CI
lane enables ASAN/UBSan and checks installed CMake consumption.

Run `cmake --build build --target ext_perf` for per-runtime initialization CPU
time, initial charged bytes, load/capture time and 10,000 cached invocations.
Library size and initialization cost depend on compiler/optimization and enabled
features; adapter READMEs describe their runtime and platform constraints.
