# Ingagi in the SDK

Ingagi is JoltFX's Go-hosted scripting-language development snapshot, used by the
desktop Game Engine workspace when `JFX_INGAGI=ON`. The SDK exposes the canonical
**native-module/embedding ABI 1.0.0**, independently versioned from the SDK,
Joltscript Glue and bytecode formats.

```c
#include <joltfx/sdk/ingagi.h>
```

C++17 consumers may also include `<ingagi/Ingagi-Module.hpp>` for the move-only
`ingagi::session` owner. The runtime's own distribution supplies D, Rust, Python
and Java bindings; Java FFM requires Java 22+ and a 64-bit host.

## What is implemented

- Parser/AST, bytecode compiler/VM, module loader and gradual static analysis.
- VM functions/closures, ECS, native/portable linear algebra, procedural noise,
  immutable mesh and AABB values, and deterministic versioned snapshots.
- IngagiVM, QBE and WASM execution backends. Native QBE/WASM lowering currently
  accepts the typed `i64`/`bool` subset with structured control flow and calls.
  Sixteen target descriptors exist; planned adapters are not working emitters.
- Explicit scalar JIT on Linux amd64 with CGO, QBE and Keystone. Native execution
  has no VM instruction budget or runtime trap recovery.
- MonkeyShade shader artifacts: GLSL 450, SPIR-V, HLSL, OpenGL/ES GLSL, MSL, WGSL,
  and Filament `.mat`/`.filamat`. Relevant external tools include
  glslangValidator, SPIRV-Cross, Naga and a matching Filament `matc`.
  Reflection describes declared source resources; graphics linking validates
  supported shader interfaces, not device limits or fixed-function state.
- MonkeyTrace has a CPU reference/native bridge and a dedicated browser WebGPU
  host. General Ingagi GPU dispatch and renderer adapters remain unfinished.

These are language/toolchain capabilities. The C embedding ABI exposes source
loading, named scalar calls and native callback registration. It accepts nil,
bool, signed int64 and float64; strings, collections, mesh/AABB and ECS handles
do not cross this ABI. Source-string loading has no filesystem import resolver;
Go's `CompileFile` and CLI file commands provide module resolution.

## Build and run the SDK example

From a repository checkout, after installing the SDK:

```sh
cmake -S ingagi -B build/ingagi-sdk -DINGAGI_GOROOT=/usr/lib/go
cmake --build build/ingagi-sdk --parallel 4
cmake --install build/ingagi-sdk --prefix /path/to/ingagi
cmake -S JoltFX-SDK/examples/ingagi -B build/sdk-ingagi-example \
  -DCMAKE_PREFIX_PATH="/path/to/sdk;/path/to/ingagi"
cmake --build build/sdk-ingagi-example
ctest --test-dir build/sdk-ingagi-example --output-on-failure
```

Ingagi's CMake runtime build currently targets native Linux/macOS hosts, needs
Go 1.21+, C/C++20 and repository vendored math dependencies, and rejects cross
compilation. Header consumers still need only C11/C++17. The example locates
`ingagi_embed` with `find_library`; set `INGAGI_EMBED_LIBRARY` explicitly if needed.

## Embedding lifecycle

1. Compare `ingagi_abi_version()` with `INGAGI_ABI_VERSION`.
2. Create a session with `ingagi_create`. Budget zero selects 1,000,000
   instructions per invocation; maximum call depth is 256.
3. Register native functions before loading UTF-8 source. Descriptors/names are
   copied; callback code and userdata are borrowed until session destruction.
4. Call a named function and inspect its status before using the result.
5. Destroy the handle after active calls finish. Keep the Go shared library
   loaded for the process lifetime.

Names are ASCII identifiers up to 255 bytes; source is at most 4 MiB. Calls and
module descriptors are limited to 256 arguments/functions. Caller-owned
diagnostic buffers receive truncated NUL-terminated messages. Callbacks execute
synchronously and must not retain borrowed argument/result/error buffers.

Concurrent/reentrant access to one handle returns `INGAGI_BUSY`. Failed source
initialization preserves the prior VM, but callback/world side effects are not
rolled back. Budgets do not bound host callbacks, memory or wall time and do not
constitute a sandbox. Printing is discarded by this embedding surface.

Canonical repository references: `ingagi/README.md`, `ingagi/DEVELOPMENT.md`,
`ingagi/DEPENDENCIES.md`, and `ingagi/native_extension_api/README.md`.
