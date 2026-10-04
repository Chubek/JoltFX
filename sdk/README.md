# JoltFX Plugin SDK 1.0

Create C11 or C++17 plugins using `jfx/jfx_plugin_sdk.h` and the header-only
`JoltFX::plugin_sdk` CMake target. Modules receive host services for image effects,
Joltscript image kernels, transactional editor actions, events, allocation and
logging.

## Standalone build

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyPlugin LANGUAGES C)
find_package(JoltFXPluginSDK 1.0 CONFIG REQUIRED)
jfx_add_plugin(my_plugin SOURCES my_plugin.c)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/joltfx/install
cmake --build build
joltfx plugins inspect /absolute/path/build/my_plugin.so
```

Export `JFX_PLUGIN_EXPORT jfx_result_t jfx_plugin_entry(...)` and register through
the supplied service table. The module needs no engine linkage. `jfx_add_plugin`
also accepts `STATIC` for embedding with `jfx_plugin_host_attach`.

## Example

`examples/tint` is a complete standalone project registering Warm Tint, a
Joltscript invert kernel, an Apply Warm Tint editor action and a frame-end
subscription. After installing JoltFX:

```sh
cmake -S /path/to/install/share/joltfx/plugin-sdk/examples/tint \
      -B tint-build -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build tint-build
jfx_desktop --plugin /absolute/path/tint-build/jfx_example_plugin.so
```

Use your platform's actual module suffix. See [the SDK guide](../docs/plugins.md)
for ABI/versioning, image and editor contracts, ownership, budgets and CLI usage.
The installed guide is `share/doc/JoltFX/docs/plugins.md` under the install prefix
(the doc directory can be configured with `CMAKE_INSTALL_DOCDIR`).
