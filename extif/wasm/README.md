# Wasmtime / joltwasm

`JFX_EXT_WASM=ON` builds `jfx_wasm` against the **Wasmtime 38+ C API**. The
adapter accepts validated WebAssembly binary modules and WAT, uses fuel for
execution budgets and Tilly for bounded linear memory. No WASI, filesystem,
network, process or native-library imports are supplied. Module compilation
uses Wasmtime's compiler; deserialization of precompiled native artifacts is
not exposed. Parallel compilation is disabled.

```sh
# Obtain the C API for the target platform, e.g. Wasmtime 38.0.4:
curl -L -o wasmtime.tar.xz https://github.com/bytecodealliance/wasmtime/releases/download/v38.0.4/wasmtime-v38.0.4-x86_64-linux-c-api.tar.xz
tar xf wasmtime.tar.xz
cmake -S . -B build -DJFX_EXT_WASM=ON \
  -DJFX_WASMTIME_ROOT="$PWD/wasmtime-v38.0.4-x86_64-linux-c-api"
cmake --build build --target jfx_wasm joltfx_cli
build/frontends/cli/joltfx scripts run wasm extif/examples/grade.wat gain 0.75
```

The installed CMake package resolves the consumer's own Wasmtime installation.
The shared Wasmtime library must remain available to the loader when running
installed executables. It is an external dependency, not bundled into JoltFX's
package. Linux x86-64 is verified; other targets need a matching Wasmtime C API
and executable-memory support. This host-side runtime is separate from the
browser's `joltvm.js` engine build.

## Scalar modules

A module without the `jfx_abi_version` export can expose scalar functions.
Parameters/results use strict `i32`, `i64` or `f64` signatures and zero/one result;
native INT maps to the integer signature and FLOAT to f64. No coercion occurs.
The scalar `joltfx.clamp(f64, f64, f64)->f64` import is available.

```wat
(module
  (func (export "gain") (param f64) (result f64)
    local.get 0 f64.const 2 f64.mul))
```

## Typed ABI 1

Typed modules export `memory` and immutable i32 globals:

- `jfx_abi_version = 1`
- `jfx_scratch`: offset of invocation scratch
- `jfx_scratch_size`: scratch capacity

The span must fit initial linear memory. Every callable export has signature
`(i32 args, i32 argc, i32 result)->i32 status`. The host writes input records and
string bytes into scratch, clears the output record and invokes the function.
Status 0 publishes the translated result. The public negative
`JFX_SCRIPT_*` codes propagate; other status values become SCRIPT_ERROR.
Scratch must hold `(argc + 1) * 32` bytes plus argument string bytes.

Records are **32 bytes**, little-endian, independent of native C struct layout:

| Offset | Field |
|---|---|
| 0 | u32 `jfx_value_type_t` tag |
| 4 | reserved u32, zero |
| 8 | payload, up to 16 bytes |
| 24 | reserved 8 bytes, zero |

NIL has zero payload. BOOL is u64 0/1; INT is i64; FLOAT is f64; COLOR is u32.
VEC2/3/4 stores 2–4 f32 lanes. STRING stores u32 memory offset and u32 byte
length, without a NUL terminator. BUFFER/TEXTURE/USERDATA store an opaque u64
invocation token, never a native address. FUNCTION records are not accepted
from guests; subscription/local batch services take callable export-name
strings. Returned strings are copied into the common result-string lifetime.

Import `joltfx.call(op_ptr, op_len, args_ptr, argc, result_ptr)->i32` to invoke
the [shared services](../../docs/extensions.md). All pointers/spans and tags are
checked, and failures latch the invocation's error status even if the guest
ignores the returned code. For a STRING host result, initialize the result's
payload with the destination offset and capacity; the host writes its byte length
back. The destination must fit linear memory and the complete string. Result
records and destination storage should be disjoint.

`extif/examples/grade.wat` implements typed numeric gain and real editor grading
commands. Fuel resets per invocation. Linear-memory growth is bounded and may
return -1 without a trap; other allocation/execution failures reach the common
diagnostics. Only one module can load successfully into a runtime; create a new
runtime to replace it. Runtime destruction owns all instance/store/module state.
Wasmtime's compiler, code and metadata allocate internally, outside the reported
Tilly/linear-memory budget. `ext_perf` measures creation, compilation and cached
dispatch separately; Wasmtime itself is supplied as an external shared/static
library, so its size depends on that distribution.
