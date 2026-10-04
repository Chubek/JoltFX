# JoltFX plugin SDK

SDK **1.0.0** extends the shared editor and rendering engine through a C host-service
table. The public header is `jfx/jfx_plugin_sdk.h`; C11 and C++17 modules can use it
without linking the engine. The desktop **Plugins** tab and **Extensions** menu,
the terminal editor and the CLI all use the same host.

## Build and load the example

From the repository:

```sh
cmake --preset default
cmake --build build --target jfx_desktop joltfx_cli jfx_example_plugin
build/frontends/desktop/jfx_desktop --plugin "$PWD/build/sdk/jfx_example_plugin.so"
build/frontends/cli/joltfx plugins inspect "$PWD/build/sdk/jfx_example_plugin.so"
```

Use the module filename produced by your platform (`.so` on Linux, usually `.so`
for CMake MODULE libraries on macOS, `.dll` on Windows). `--plugin` is repeatable
and modules load before `--project`. A `.jfx` file stores stable effect identifiers
and parameter values; load its modules before opening it.

The example in `sdk/examples/tint/tint.c` registers:

- **Warm Tint**, a native straight-RGBA image effect, `org.joltfx.example.tint`;
- **Kernel Invert**, a compiled Joltscript image kernel, `org.joltfx.example.invert`;
- **Apply Warm Tint**, an editor action, `org.joltfx.example.apply`;
- an owned frame-end event subscription and a host-allocated instance state.

In the desktop, select a clip in NLE or the shared clip selector, open **Plugins**,
enter the module path and click **Load module**. Click **Apply Warm Tint** there or
in **Extensions**. Its parameters are available in **Color Grading**. Both
operators also appear in the node library and Layer Effects catalog.

## Installed SDK and standalone builds

Install a built JoltFX tree to a prefix:

```sh
cmake --install build --prefix "$HOME/.local/joltfx"
cmake -S "$HOME/.local/joltfx/share/joltfx/plugin-sdk/examples/tint" \
      -B tint-build -DCMAKE_PREFIX_PATH="$HOME/.local/joltfx"
cmake --build tint-build
```

For your own module, create this `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyJoltFXPlugin VERSION 1.0.0 LANGUAGES C) # or CXX
find_package(JoltFXPluginSDK 1.0 CONFIG REQUIRED)
jfx_add_plugin(my_plugin SOURCES my_plugin.c)
```

`JoltFX::plugin_sdk` is an interface target carrying the public include directory.
`jfx_add_plugin` creates a hidden-visibility MODULE target, selects C11/C++17 and
exports only explicitly marked symbols. ELF modules are linked with undefined
symbol checks, so an accidental direct engine call fails at link time. The SDK
package has no engine, ImGui, SDL, FFmpeg or GPU-library link dependencies.

## Entry point and initialization

Export one entry point with `JFX_PLUGIN_EXPORT`:

```c
#include "jfx/jfx_plugin_sdk.h"

static jfx_result_t initialize(const jfx_plugin_api_t *api, void **out_userdata);
static void shutdown(void *userdata);

JFX_PLUGIN_EXPORT jfx_result_t jfx_plugin_entry(
    uint32_t major, uint32_t minor, jfx_plugin_definition_t *out) {
    (void)minor;
    if (major != JFX_PLUGIN_SDK_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    if (!out || out->size < sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    *out = (jfx_plugin_definition_t){
        sizeof(*out), JFX_PLUGIN_SDK_MAJOR, 0,
        {sizeof(jfx_plugin_desc_t), "org.example.myplugin", "My Plugin", "Example",
         JFX_PLUGIN_VERSION(1, 0, 0), JFX_PLUGIN_API_VERSION,
         JFX_PLUGIN_CAP_KERNELS | JFX_PLUGIN_CAP_EDITOR},
        initialize, shutdown
    };
    return JFX_SUCCESS;
}
```

The header declares C linkage, including for C++ definitions. Set every
size-guarded structure's `size` to `sizeof` that structure. In `initialize`, check
`api->sdk_major`, `api->sdk_minor` and `api->size` before using services, retain the
borrowed table in your instance state, and make registrations through
`api->context`. Assign `*out_userdata` as soon as state exists: `shutdown` also
runs after initialization failure, including with NULL state.

Registration is initialization-only. Descriptors, names, labels and scalar
metadata are copied; native callbacks and their userdata remain plugin-owned
until shutdown. A failed initialization removes all of its registrations and
subscriptions. Plugin identifiers are unique within a host, effect names are
unique process-wide, and action names are unique within their host.

Supported SDK capabilities:

| Flag | Services |
|---|---|
| `JFX_PLUGIN_CAP_KERNELS` | `register_effect`, `register_kernel` |
| `JFX_PLUGIN_CAP_EDITOR` | `register_action` |
| `JFX_PLUGIN_CAP_EVENTS` | `subscribe` |

Requests for types, assets, backends or IO registration return
`JFX_ERROR_NOT_IMPLEMENTED` in this SDK version. The legacy loader remains
available for modules exporting `jfx_plugin_init`; its API version stays **1**.
The additive Plugin API is **1.1**, Editor API **1.5**, Desktop API **1.1** and the
host-service SDK has its own **1.0** version negotiation.

## Native image effects

Provide a `jfx_plugin_effect_desc_t` with a stable namespaced name, display label,
category, scalar descriptors, callback and userdata. The host exposes it as a
single-image-input/single-image-output graph node and a timeline effect.

Categories `Color Grading` and `Color Calibration` place the operator in those
editor sections; other categories appear in the layer/node catalogs. Effect and
parameter names are at most 63 bytes, beginning with an alphanumeric character
and containing only alphanumerics, `.`, `_` and `-`. Labels and categories are at
most 95 bytes; action names are at most 95 bytes. There can be 128 custom effects
process-wide, 32 plugins per host, 32 actions and 32 event subscriptions per
plugin. Scalar count is bounded by `JFX_NODE_MAX_PARAMS`.

Parameter ranges/defaults/steps must be finite, minima cannot exceed maxima, and
defaults must lie in range. Integral descriptors require integral bounds and
defaults. Parameter names must be unique within an effect.

The callback receives borrowed, tightly packed **straight float RGBA**, parameter
values and time in seconds. Write every output sample; alpha must be in `[0,1]`,
while RGB can carry finite extended-range values. Return `JFX_SUCCESS` only for
complete output. The Execution layer validates input/output and publishes the
frame only on success, preserving caller bytes on callback failure, non-finite
values or incomplete output. Input/output aliasing is supported by the host.

Use `api->allocate`/`deallocate` for state and scratch; the requested alignment
must be a nonzero power of two, at most 4096. Honor `image->scratch_limit`, the
remaining per-call budget. Callbacks run synchronously on the caller and use the
same owner-thread lifecycle as documents. Rendering creates no plugin workers.

## Joltscript kernels

`register_kernel` compiles the supplied library/source once through Glue, reflects
its parameters and executes the immutable program through the bounded image task
runner. Use the image profile documented in [the kernel guide](../kernels/README.md),
for example:

```lisp
(param amount 1 0 1 0)
(defkernel invert [x y c]
  (if (= c 3)
    (sample x y 3 0 0)
    (+ (* (sample x y c 0 0) (- 1 amount))
       (* (- (sample x y 3 0 0) (sample x y c 0 0)) amount))))
```

`sample` takes x, y, channel, interpolation and border mode. Image-profile
programs operate on **premultiplied RGBA**; the host marshals to/from the native
straight representation and validates alpha. Source cannot import files, call
FFI or access system resources. Kernel evaluation is synchronous CPU execution,
with bounded frame scratch and 10,000 AST evaluation steps per pixel for SDK
kernels. Compiler diagnostics, including source location, are available through
`jfx_plugin_host_error`.

## Transactional editor actions

Register a `jfx_plugin_action_desc_t` naming its sequence or graph target. The
callback receives a borrowed editor and zero-based `track`, `clip` and `node`
selection. Use `api->editor_kind`, `sequence_state` and `graph_state` to inspect
state into caller-owned JSON buffers, and `api->editor_command` to make edits.
See [NLE commands](nle.md), [color commands](editor.md) and
[composition commands](composition.md).

The host reserves a baseline, activates the declared document and groups all
successful commands into **one undo step**. Returning an error restores the
baseline and prior preview mode without allocating during rollback. Undo/redo
restores that document and mode while retaining the other document. An action
cannot modify the other document during its transaction; nested edits, load and
undo/redo return `JFX_ERROR_BUSY`. Use service calls rather than direct engine
symbols, and propagate command failures.

## Events, lifetime and embedding

`subscribe` installs an owned synchronous event handler. Event data is borrowed
for the callback; subscriptions are removed on load rollback, unload or host
destruction. Registration, events and document operations are serialized on the
owner thread. Do not load/unload plugins or mutate subscriptions inside callbacks.
`api->log` routes messages to engine diagnostics and the desktop Console.

`jfx_plugin_host_load` loads native modules. `jfx_plugin_host_attach` accepts a
definition from statically linked code, using the same initialization/services:

```cmake
jfx_add_plugin(my_static_plugin STATIC SOURCES my_plugin.c)
target_link_libraries(my_application PRIVATE my_static_plugin)
```

Give each statically linked plugin a distinct definition getter/entry function
when linking several plugins; pass the returned definition to
`jfx_plugin_host_attach`. WASM and mobile embedders can use this path. Native
dynamic loading returns `JFX_ERROR_NOT_IMPLEMENTED` in WASM.

Embedding applications create a host with `jfx_plugin_host_create(engine, &host)`,
enumerate modules/actions with `info_at`/`action_info`, and invoke actions with
`jfx_plugin_host_invoke`. The desktop exposes a borrowed host through
`jfx_desktop_frontend_plugins` and wrappers for load/unload/invoke.

Graphs, duplicated/split timeline clips, transaction baselines, undo/redo history
and export snapshots retain their custom descriptors. `jfx_plugin_host_unload`
returns `JFX_ERROR_BUSY` while any are referenced. Remove live effects/nodes and
clear history to release history-only references; export jobs release their
snapshot on destruction. Host destruction removes events immediately and defers
module finalization until the last document reference is released. Destroy
documents/export jobs and the host before shutting down the engine. Borrowed
catalog pointers are valid only while their plugin remains loaded.

## CLI workflow

```sh
joltfx plugins inspect /absolute/path/my_plugin.so
joltfx edit input.jfx edited.jfx --plugin /absolute/path/my_plugin.so
# In the terminal editor:
# plugin.action org.joltfx.example.apply 0 0 0
# save
joltfx plugins render /absolute/path/my_plugin.so edited.jfx frame.ppm 0
joltfx export-video edited.jfx -o final.mkv --plugin /absolute/path/my_plugin.so
```

Terminal commands also include `plugins`, `plugin.load PATH` and
`plugin.unload ID`. Module paths with spaces are supported. `edit` and
`export-video` accept repeatable `--plugin`; `plugins render` loads one module
before parsing, renders an exact sequence frame (or graph time `FRAME/30`) and
writes binary PPM.

## Verification

`plugin_sdk` covers registration rollback, version/capability validation, copied
metadata, native/Joltscript pixels, transactional actions, owned events and
document/history/export-held unload protection. `plugin_host` covers legacy
modules. `workspace_ui_tests` drives real ImGui tab/dial/wheel input and plugin
actions; `plugin_cli_integration` exercises shared-module inspect/edit/render and
encoded export with independent ffprobe checks when available.
