# WAMR / joltwasm

`JFX_EXT_WASM=ON` builds `jfx_wasm` and `jfx_wamr_runtime` from
**WebAssembly Micro Runtime** in `third_party/wasm-micro-runtime`. The adapter
accepts validated binary `.wasm` modules, uses the classic interpreter with
instruction metering, and routes module, instance, stack and linear-memory
allocations through Tilly's runtime budget. WASI, builtin libc, guest threads,
JIT and AOT are disabled. Only `joltfx.call` and `joltfx.clamp` imports are allowed.

```sh
git clone https://github.com/wasm-micro-runtime/wasm-micro-runtime third_party/wasm-micro-runtime
cmake -S . -B build -DJFX_EXT_WASM=ON
cmake --build build --target jfx_wasm joltfx_cli
build/frontends/cli/joltfx scripts run wasm extif/examples/grade.wasm gain 0.75
```

An existing checkout can also be initialized through `scripts/submodules-init.sh`.
`JFX_WAMR_ROOT` selects an external WAMR source checkout; it must provide
`wasm_runtime_set_instruction_count_limit`. CMake compiles the sources offline.
The installed CMake package includes the interpreter archive, so consumers need
no separate WASM SDK or shared runtime library. Linux x86-64 is verified; native
platform selection also covers Windows, macOS/iOS and Android. This host-side
runtime is separate from the browser's `joltvm.js` engine build.

WAT remains an authoring format. Compile it with WABT before loading:

```sh
wat2wasm extif/examples/grade.wat -o extif/examples/grade.wasm
```

The checked-in `grade.wasm` and binary conformance fixtures allow builds and
tests without WABT. Start sections and automatic `__post_instantiate` /
`__wasm_call_ctors` exports are rejected because WAMR executes them before a
metered execution environment exists. Use an explicit initialization export.

Sanitizer builds use WAMR's upstream UBSan profile, which excludes alignment
checks on its four-byte VM stack. The JoltFX adapter retains full ASAN/UBSan
instrumentation.

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
commands; `grade.wasm` is its compiled counterpart. Instructions reset per invocation
(limits above `INT_MAX` are conservatively capped). Linear-memory growth is bounded and may
return -1 without a trap; other allocation/execution failures reach the common
diagnostics. Only one module can load successfully into a runtime; create a new
runtime to replace it. Runtime destruction owns all execution/instance/module state.
Shared WAMR bootstrap allocations use Tilly outside the per-runtime byte counter;
they live until the last WASM runtime is destroyed. All runtime lifecycle and
execution operations use the serialized extension-owner thread. GC controls are
no-ops for the non-GC interpreter. `ext_perf` measures creation, loading and cached
dispatch separately.
