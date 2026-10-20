# JoltFX SDK 1.0

A C11/C++17 development package over JoltFX's existing public contracts. The SDK
does not duplicate the engine or expose third-party implementation types.

| CMake target | Public include | Purpose |
| --- | --- | --- |
| `JoltFXSDK::plugins` | `joltfx/sdk/plugins.h` | Image effects, Joltscript kernels, editor actions, events |
| `JoltFXSDK::renderer` | `joltfx/sdk/renderer.h` | Renderer-module ABI and synchronous host services |
| `JoltFXSDK::loader` | `joltfx/sdk/loader.h` | Native renderer loading or static attachment |
| `JoltFXSDK::backend` | `joltfx/sdk/backend.h` | Existing backend HAL |
| `JoltFXSDK::frontend` | `joltfx/sdk/frontend.h` | Shared editor and export API |
| `JoltFXSDK::gui` | `joltfx/sdk/gui.h` | Toolkit-independent controls/descriptors and transactions |
| `JoltFXSDK::facet` | `joltfx/sdk/facet.h` | Composition, color and 3D scene APIs |
| `JoltFXSDK::libraries` | `joltfx/sdk/libraries.h` | Engine, images and LUTs |
| `JoltFXSDK::module` | `joltfx/sdk/module.h` | Existing plugin definitions and lifecycle |
| `JoltFXSDK::tools` | `joltfx/sdk/tools.h` | Project and scene tooling |

Header-only targets supply contracts, not engine implementations. Engine API
consumers additionally link `JoltFX::jfx_core` from the full JoltFX installation.
Modules using host-service tables need **no engine linkage**. GUI toolkit widgets,
new foreign-language bindings and unsupported plugin capabilities are not invented
by these adapters; existing API limitations still apply (`docs/plugins.md`).

## Build/install the standalone SDK

```sh
cmake -S JoltFX-SDK -B build/sdk -DCMAKE_INSTALL_PREFIX=/path/to/sdk
cmake --build build/sdk
cmake --install build/sdk
cmake -S plugins/JoltRenderer -B build/renderer -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build/renderer
```

The root JoltFX build also builds/installs this package. A relocated SDK needs no
source tree. When building SDK sources outside this repository, set
`JOLTFX_SDK_API_ROOT` to the public `include` tree. `examples/gain` is a complete
standalone image-effect module:

```cmake
find_package(JoltFXSDK 1 REQUIRED CONFIG)
joltfx_sdk_add_plugin(MyEffect effect.c) # exports jfx_plugin_entry
joltfx_sdk_add_renderer(MyRenderer renderer.cpp) # exports jfx_renderer_entry
```

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

See [`plugins/JoltRenderer`](../plugins/JoltRenderer/README.md) for the in-tree
renderer implementation and CPU/GPU limits.
