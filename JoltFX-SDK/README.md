# JoltFX SDK 1.4

A C11/C++17 development package over JoltFX's existing public contracts. The SDK
does not duplicate the engine or expose third-party implementation types.

The package covers native plugins/renderers, editing and export, composition and
3D, audio and animation, runtime services, **Ingagi embedding**, and **Joltscript
compilation/execution**. See the [complete facet guide](docs/facets.md),
[Ingagi guide](docs/ingagi.md), [Joltscript guide](docs/joltscript.md), and
[OpenFX inspection guide](docs/openfx.md).
Planned and in-progress SDK work is tracked in [FUTURE_OF_SDK.md](FUTURE_OF_SDK.md).

| CMake target | Public include | Purpose |
| --- | --- | --- |
| `JoltFXSDK::plugins` | `joltfx/sdk/plugins.h` | Image effects, Joltscript kernels, editor actions, events |
| `JoltFXSDK::renderer` | `joltfx/sdk/renderer.h` | Renderer-module ABI and synchronous host services |
| `JoltFXSDK::loader` | `joltfx/sdk/loader.h` | Renderer loading/static attachment; renderer and plugin metadata inspection |
| `JoltFXSDK::openfx` | `joltfx/sdk/openfx.h` | Native OpenFX module metadata inspection |
| `JoltFXSDK::backend` | `joltfx/sdk/backend.h` | Existing backend HAL |
| `JoltFXSDK::frontend` | `joltfx/sdk/frontend.h` | Shared editor and export API |
| `JoltFXSDK::gui` | `joltfx/sdk/gui.h` | Toolkit-independent descriptors, transactions, viewport helpers |
| `JoltFXSDK::facet` | `joltfx/sdk/facet.h` | Composition, color and 3D scene APIs |
| `JoltFXSDK::libraries` | `joltfx/sdk/libraries.h` | Engine, images, LUTs, buffers and textures |
| `JoltFXSDK::module` | `joltfx/sdk/module.h` | Existing plugin definitions and lifecycle |
| `JoltFXSDK::tools` | `joltfx/sdk/tools.h` | Project, scene and Ingagi Animation Pack tooling |
| `JoltFXSDK::audio` | `joltfx/sdk/audio.h` | Mixing, MIDI, automation, recording and VST3 |
| `JoltFXSDK::timeline` | `joltfx/sdk/timeline.h` | Sequences, tracks, clips and keyframe animation |
| `JoltFXSDK::nle` | `joltfx/sdk/nle.h` | Atomic Joltscript edits, relinking, clap/loudness analysis, OTIO, transitions and conversion |
| `JoltFXSDK::events` | `joltfx/sdk/events.h` | Synchronous engine event subscriptions |
| `JoltFXSDK::runtime` | `joltfx/sdk/runtime.h` | Engine lifecycle, memory, scheduler and result codes |
| `JoltFXSDK::ingagi` | `joltfx/sdk/ingagi.h` | Canonical Ingagi native-module/embedding ABI 1.0.0 |
| `JoltFXSDK::joltscript` | `joltfx/sdk/joltscript.h` | Compiler, typed VM, image/audio tasks and pipelines |
| `JoltFXSDK::jolttype` | `joltfx/sdk/jolttype.h` | Page projects, inventory/Typelets, output targets and WASM module actions |

`JoltFXSDK::headers` supplies the installed public include directory. Link the
named facet targets for equivalent build-tree usage. `loader` and `openfx` are
static implementation libraries; the other targets are header-only contracts.

Header-only targets supply contracts, not engine implementations. Engine API
consumers additionally link `JoltFX::jfx_core` from the full JoltFX installation.
Modules using host-service tables need **no engine linkage**. GUI toolkit widgets,
new foreign-language bindings and unsupported plugin capabilities are not invented
by these adapters; existing API limitations still apply. Ingagi needs the
separately built `ingagi_embed` shared library. Direct Joltscript calls need
`JoltFX::joltscript_glue` / `JoltFX::joltscript_execution` from a full installation.
Finding an SDK component confirms its headers, not runtime or GPU availability.

## Build/install the standalone SDK

```sh
cmake -S JoltFX-SDK -B build/sdk -DCMAKE_INSTALL_PREFIX=/path/to/sdk
cmake --build build/sdk
cmake --install build/sdk
cmake -S plugins/JoltRenderer -B build/renderer -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build/renderer
```

The root JoltFX build also builds/installs this package. A relocated SDK needs no
source tree. Standalone packaging needs CMake 3.20+, C and C++ compilers; it does
not build the language runtimes or require Go. When building SDK sources outside
this repository, set these paths to canonical headers or an existing installation:

| CMake cache variable | Repository default | Installed equivalent |
| --- | --- | --- |
| `JOLTFX_SDK_API_ROOT` | `src/include` | `<prefix>/include` |
| `JOLTFX_SDK_GLUE_INCLUDE_DIR` | `joltscript/layers/glue/include` | `<prefix>/include` |
| `JOLTFX_SDK_EXECUTION_INCLUDE_DIR` | `joltscript/layers/execution/include` | `<prefix>/include` |
| `JOLTFX_SDK_INGAGI_API_DIR` | `ingagi/native_extension_api` | `<prefix>/include/ingagi` |
| `JOLTFX_SDK_TILLY_INCLUDE_DIR` | `tilly/include` | `<prefix>/include` |
| `JOLTFX_SDK_OPENFX_INCLUDE_DIR` | `third_party/openfx/include` | `<prefix>/include/openfx` |
| `JOLTFX_SDK_JOLTTYPE_INCLUDE_DIR` | `jolttype/include` | `<prefix>/include` |

`examples/gain` is a complete standalone image-effect module:

```cmake
find_package(JoltFXSDK 1 REQUIRED CONFIG)
joltfx_sdk_add_plugin(MyEffect effect.c) # exports jfx_plugin_entry
joltfx_sdk_add_renderer(MyRenderer renderer.cpp) # exports jfx_renderer_entry
```

Request new facets with `find_package(JoltFXSDK 1.4 REQUIRED CONFIG COMPONENTS
ingagi joltscript jolttype openfx nle)`. Required unknown components fail configuration. SDK package
version 1.4.0 is additive; individual engine/language/module ABI versions are
independent. Existing 1.0 consumers remain supported.

JoltType consumers link `JoltFX::jolttype` separately; see
[JoltType target/frontend guide](docs/jolttype.md).

Installed examples live in `share/joltfx/sdk/examples`; guides are under
`${CMAKE_INSTALL_DOCDIR}/sdk`. Language examples check results and release their
owned resources: `examples/ingagi` calls a script through the scalar C ABI;
`examples/joltscript` compiles and runs an exact-integer typed kernel.

## Renderer contract

`jfx_renderer_entry(major, minor, &definition)` negotiates renderer ABI 1.0.
This is a separate module kind from the existing image-effect `jfx_plugin_entry`;
do **not** load renderer modules with CLI `--plugin` or the image Plugins panel.
Use `jfx_renderer_module_open`, render synchronously through
`jfx_renderer_module_render`, then close after all uses complete. Static/mobile/
WASM hosts use `jfx_renderer_module_attach`. Hosts supply allocator and backend
services, keeping their scene representation opaque to modules. Contexts and
snapshots must outlive the call; callbacks retain nothing. Loader/render/lifecycle
operations on a handle must be serialized on its owner thread. Recursive render
or close during a render returns `BUSY`.

The loader uses the host allocator and rolls back failed loads. Native modules
are trusted code, **not sandboxed**. Size/version guards permit additive ABI
evolution. A host must never dispatch its backend callback back to the same
module. Explicit device errors propagate; availability reports build support,
not guaranteed GPU-driver operation. Output must remain untouched on error.

`jfx_renderer_module_peek` and `jfx_plugin_module_peek` copy module metadata
without a host table or engine initialization. They call the native entry point,
validate its size/version/strings and unload before returning. Output is unchanged
on failure. These inspect SDK entry points, not legacy `jfx_plugin_init` modules;
native loading still executes module code. WASM returns `NOT_IMPLEMENTED`.

See repository guide `plugins/JoltRenderer/README.md` for the in-tree renderer
implementation and CPU/GPU limits.
