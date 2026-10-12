# Ingagi — development snapshot

Ingagi is a Go-hosted scripting-language prototype for JoltFX's game-engine
workspace. Its language foundation is `frontend/Ingagi.ebnf`. This snapshot has
a parser/AST, bytecode compiler and VM, a typed scalar QBE native backend, an ECS
package, native math bridges, and a CLI. It is not yet connected to the desktop
game-engine tab.

## Build and run

From `ingagi/`:

```sh
GOROOT=/usr/lib/go go test ./... -timeout 30s
GOROOT=/usr/lib/go go run ./cmd run examples/closures.ing
GOROOT=/usr/lib/go go run ./cmd check examples/closures.ing
GOROOT=/usr/lib/go go run ./cmd dump examples/closures.ing
```

`run` prints script output and then a non-null return value. Options precede the
file: `run -entry main -budget 1000000 FILE`. The budget applies separately to
initialization and the entry call, including their nested calls. Zero disables
the instruction limit. `check` checks parsing and bytecode lowering; it is not
a full static type checker and does not execute the module. `dump` prints
optimized bytecode. Exit codes are 0 for success, 1 for input/compilation/runtime
errors, and 2 for CLI usage errors.

CGO builds require a C++20 compiler and repository-relative vendored GLM and
simdette headers. `CGO_ENABLED=0` selects the portable Go math implementation.

## QBE native compilation and Keystone assembly

The native backend currently supports **`i64` and `bool`**, explicit function
parameter/return types, initialized locals (with type inference), lexical
scopes, direct/recursive calls, arithmetic/bitwise/comparison operators,
short-circuit booleans, ternary expressions, assignments, `if`/`else`, `while`,
`loop`, `break` and `continue`. Unsupported constructs produce source-positioned
diagnostics. Every reachable function exit must explicitly return a value.

Native `i64` uses signed 64-bit two's-complement arithmetic; add/subtract/multiply
wrap. Division by zero, division/remainder of INT64_MIN by -1, and shifts outside
0–63 trap. `bool` uses a normalized 32-bit C ABI integer. Native code has no VM
instruction budget or runtime-error object. Closures, floating-point types,
aggregates, ECS/shader operations, globals, imports and external host calls are
not yet lowered by this backend.

Artifacts:

- `-emit qbe`: portable QBE IL, no native tools needed.
- `-emit asm`: QBE target assembly for `amd64_sysv`, `amd64_apple`, `arm64`,
  `arm64_apple`, or `rv64`. Only assembly generation is tested on non-host targets.
- `-emit obj` (default): AMD64 ELF relocatable, assembled by **Keystone**, then
  wrapped in ELF by Go. Includes a non-executable-stack annotation.
- `-emit bin`: position-independent x86-64 machine code, entry at byte zero.

All artifacts export the selected source function as `ingagi_entry` (the binary
format uses offset zero instead of a symbol). Its parameters and result use that
function's native C ABI. Internal functions are private; no external relocations
or Ingagi runtime library are needed. Assembly/object/bin export defaults to
source entry `main`; select another function with `-entry NAME`.

### Build the vendored tools (Linux)

The current Keystone vendor snapshot lacks its LLVM implementation/build-tool
directories. The script can reconstruct those directories in an **external build
tree**, retaining the vendored engine/headers, from this pinned upstream archive:

```sh
curl --fail --location \
  https://codeload.github.com/keystone-engine/keystone/tar.gz/refs/tags/0.9.2 \
  -o /tmp/opencode/keystone-0.9.2.tar.gz
INGAGI_KEYSTONE_ARCHIVE=/tmp/opencode/keystone-0.9.2.tar.gz \
  bash ingagi/scripts/build-native-tools.sh
```

The script verifies SHA-256
`c9b3a343ed3e05ee168d29daf89820aff9effb2c74c6803c2d9e21d55b5b7c24` before use;
see `ingagi/DEPENDENCIES.md`. It requires CMake, Make, Python and C/C++
compilers, builds QBE from `third_party/qbe`, and builds Keystone's x86 backend.
Set `INGAGI_TOOLS_BUILD` to change the default external build directory. A full
vendor checkout needs no source supplement. An existing compatible Keystone
0.9 library can also be selected explicitly.

### Compile and link

After building tools, from `ingagi/`:

```sh
export INGAGI_QBE=/tmp/opencode/ingagi-native-tools/qbe/qbe
export INGAGI_KEYSTONE_LIBRARY=/tmp/opencode/ingagi-native-tools/keystone-build/llvm/lib/libkeystone.so.0
GOROOT=/usr/lib/go go run ./cmd build -o /tmp/opencode/native.o examples/native.ing
cc examples/native_driver.c /tmp/opencode/native.o -o /tmp/opencode/native-example
/tmp/opencode/native-example
```

The example prints `33`. `cc` compiles the C driver and links the Keystone-produced
object; it never assembles QBE's generated code. The CLI also accepts explicit
`-qbe /path/to/qbe` and `-keystone /path/to/libkeystone.so` options. Output files
are replaced only after a successful build. The bridge currently requires Linux
with CGO enabled; portable builds can still export QBE IL and target assembly.

Run native integration tests with both environment variables set:

```sh
GOROOT=/usr/lib/go go test ./compiler -timeout 60s
```

These tests execute linked objects and compare them to VM results, including
recursion, loops, short-circuit evaluation, signed division, call argument order,
and arguments passed on the stack. Without the variables, tool-dependent tests
are explicitly skipped. Add `-tags novoparse` and the library flags from the next
section to use native scannerless recognition in this compilation path too.

For embedding, `ingagi.CompileNative(ctx, source, ingagi.NativeOptions{Emit: "obj"})`
returns artifact bytes. It does not load or execute them. The context bounds the
QBE subprocess; Keystone assembly is synchronous.

## NovoParse scannerless backend

The `novoparse` build tag makes native recognition mandatory in `frontend.Parse`.
It links the C runtime from `third_party/novoparse`, using generated C grammar
tables in `frontend/generated/ingagi.h`. `frontend.ParseConcrete` exposes its
concrete syntax tree as owned Go JSON, byte spans and ambiguity status. Native
parser instances are destroyed after each parse.

On Linux, build the native runtime and run the full tagged suite from the repo:

```sh
bash ingagi/scripts/test-native.sh
```

The script requires CMake and a D compiler (LDC, DMD or GDC). It uses an offline
build in `/tmp/opencode/ingagi-novoparse` by default; override with
`INGAGI_NATIVE_BUILD`. Generated artifacts are checked for staleness. To run the
CLI through this backend after building:

```sh
cd ingagi
GOROOT=/usr/lib/go CGO_ENABLED=1 \
  CGO_LDFLAGS='-L/tmp/opencode/ingagi-novoparse -Wl,-rpath,/tmp/opencode/ingagi-novoparse' \
  go run -tags novoparse ./cmd run examples/closures.ing
```

This is a transitional two-stage frontend: NovoParse recognizes the source,
then the handwritten Go parser constructs the typed AST. `ParseExpression` and
direct `NewParser` use remain handwritten. Without the tag, `Parse` also uses
the handwritten implementation. The grammar uses deterministic first-production
disambiguation; the concrete-tree result reports ambiguity. A rule-name map is
not a proof of grammar equivalence. Direct concrete-tree-to-AST lowering,
reserved-word handling and complete grammar parity remain follow-up work.

## Embed in Go

```go
import (
    "github.com/Chubek/JoltFX/ingagi"
    rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

module, err := ingagi.Compile(`fn main() { return host_double(21); }`)
if err != nil { return err }

vm := rt.New()
vm.SetBudget(rt.Budget{MaxInstrs: 100000, MaxFrames: 256})
vm.RegisterHostFn("host_double", func(_ *rt.VM, args []rt.Value) (rt.Value, error) {
    return rt.IntValue(args[0].I * 2), nil
})
if err := vm.LoadModule(module); err != nil { return err }
value, err := vm.Call("main") // value is 42
```

Host callbacks should validate their arguments. `SetStdout` redirects printing,
`SetWorld` injects an ECS world, and `RunHook` drives system lifecycle hooks.
Use a VM from one goroutine at a time. Instruction and call-depth limits do not
limit memory, wall-clock time or host callbacks; this VM is not a sandbox.
Failed calls unwind execution stacks but do not roll back global/world changes.

### Bytecode images and reload

`VM.Encode`, `runtime.Decode` and `VM.LoadDecoded` provide an experimental v1
snapshot format. Decode checks lengths, nested-value depth, record shapes,
duplicate entry/type/method/global records, trailing bytes, operand widths,
constant indices, opcodes and control-flow targets. Module loads reject these
structural errors before replacing the current module. The same gate is applied
to modules built in Go; it is not full stack/type verification and does not make
arbitrary bytecode safe to execute.

Frame-local indices are limited to 65,536 slots; functions may declare up to
65,536 parameters. Aggregate/call counts must fit the current activation's
operand stack. Repeated arrays are limited to 65,536 elements. These checks stop
specific unchecked allocations, not total script memory usage. Decoder collections
are limited to 65,536 elements, individual strings to 16 MiB, and nested snapshot
values to 64 levels.

Encoding orders symbol/type/method maps deterministically. It rejects cyclic or
deeply nested values and captured closures because v1 cannot preserve their
environments. Named function closures remain supported. Object alias identity,
world state and dynamic observer registrations are not preserved by v1.

Reload replaces globals, arities and optimization profiles/caches while retaining
host registrations and the injected ECS world. An initializer failure is still
non-transactional; structural rejection happens before installation.

Robustness tests include saved fuzz inputs plus decoder, parser and expression
fuzz targets. Short development checks from `ingagi/`:

```sh
GOROOT=/usr/lib/go go test -race ./... -timeout 60s
GOROOT=/usr/lib/go go test ./runtime -run '^$' -fuzz '^FuzzDecode$' -fuzztime 5s -parallel 2
GOROOT=/usr/lib/go go test ./frontend -run '^$' -fuzz '^FuzzParseExpression$' -fuzztime 5s -parallel 2
```

Decoder fuzzing tests decoding and installation; execution regressions cover
specific malformed tapes separately.

## Native module bindings and target discovery

`native_extension_api/Ingagi-Module.h` defines scalar embedding ABI **1.0.0**.
`joltfx_embed` implements source loading, named calls, native callback registration,
instruction budgets and opaque session handles. C++, D, Python, Rust and Java
bindings share this ABI. Values crossing it are nil, bool, int64 and float64.
See [the ABI guide](native_extension_api/README.md) for ownership, callback
lifetimes and language examples. Java uses Java 22+ FFM on 64-bit hosts.

```sh
cmake -S ingagi -B /tmp/opencode/ingagi-embed -DINGAGI_GOROOT=/usr/lib/go
cmake --build /tmp/opencode/ingagi-embed --parallel 4
ctest --test-dir /tmp/opencode/ingagi-embed --output-on-failure
/tmp/opencode/ingagi-embed/bin/ingagi targets
```

The root CMake build also accepts `JFX_INGAGI=ON`. The twelve embedded target
descriptors are validated schema-v1 metadata, exposed by `compiler.Targets`,
`compiler.LookupTarget` and `ingagi targets`. IngagiVM and QBE are implemented;
the other ten describe planned adapters/shader emitters. `RequireArtifact`
rejects planned targets and unsupported output formats. QBE artifact selection
uses this gate; its architecture-specific checks remain in the native builder.

## Current implementation boundaries

- Functions, captured-by-value closures, arithmetic, arrays, loops, matches,
  deferred blocks, structs and enums execute as dynamically checked bytecode.
- ECS storage, queries, hierarchy, system ordering and command queues have
  package-level tests. The VM has draft ECS builtins, hooks and render-binding
  metadata; full engine integration and conformance coverage remain pending.
- CGO math calls **GLM** for matrix multiplication, projection, view matrices
  and point transforms. **Simdette** `vec3<double>` implements batch addition and
  scaling. Its float hardware batches cannot preserve the VM's float64 values,
  so this bridge uses scalar linear algebra, not hardware SIMD. Go implements
  the remaining scalar vector/quaternion operations. Native and portable math
  are compared against reference results, including tails and in-place buffers.
- `sema.DefaultOptions()` enables the draft scope pass. Requesting any of the
  nine unimplemented passes returns `E0001`; none silently reports success.
- Shader and pipeline syntax/metadata exist; shader code generation and GPU
  execution remain placeholders. QBE is the implemented scalar native backend;
   other target descriptors describe planned implementations.
- Bytecode image serialization is experimental. Full bytecode stack/type verification,
  transactional reload, comprehensive static analysis, module imports, broader native/JIT targets,
   aggregate/resource C ABI marshalling and desktop wiring remain unfinished.

## Regenerating the grammar

After editing `frontend/ingagi.ng`, from the repository root:

```sh
/tmp/opencode/ingagi-novoparse/novoparse generate ingagi/frontend/ingagi.ng \
  --profile C -o ingagi/frontend/generated/ingagi.h
/tmp/opencode/ingagi-novoparse/novoparse compile ingagi/frontend/ingagi.ng \
  -o ingagi/frontend/ingagi.sexp
```

Run the tagged suite to exercise the generated parser, not only grammar checking.
