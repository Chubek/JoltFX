# Ingagi — development snapshot

Ingagi is a Go-hosted scripting-language prototype for JoltFX's game-engine
workspace. Its language foundation is `frontend/Ingagi.ebnf`. This snapshot has
a parser/AST, bytecode compiler and VM, module loader, gradual static analyses,
typed scalar QBE and WebAssembly backends, shader artifact lowering, an ECS
package, native math bridges, and a CLI. The desktop Game Engine tab uses the
native embedding ABI when built with `JFX_INGAGI=ON`.

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
the instruction limit. `check` resolves imports and checks scope, statically
known types, declaration constraints, control flow, constant integer arithmetic,
ECS queries/system ordering and shader/pipeline references, then checks bytecode
lowering and validates the lowered tape (pool indices, control-flow targets,
stack shapes, descriptor references and enum payload arities). Dynamic values
and unknown host signatures aren't fully type-verified.
It does not execute the module. `dump` prints
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
aggregates, ECS/shader operations, globals and external host calls are
not yet lowered by this backend.

The CLI and `CompileNativeFile` resolve imported scalar functions before native
lowering. Every declaration in the dependency graph must fit the native subset.

Artifacts:

- `-emit qbe`: portable QBE IL, no native tools needed.
- `-emit asm`: QBE target assembly for `amd64_sysv`, `amd64_apple`, `arm64`,
  `arm64_apple`, or `rv64`. Only assembly generation is tested on non-host targets.
- `-emit obj` (default unless `-target wasm32`/`WASM`): AMD64 ELF relocatable, assembled by **Keystone**, then
  wrapped in ELF by Go. Includes a non-executable-stack annotation.
- `-emit bin`: position-independent x86-64 machine code, entry at byte zero.
- `-emit wasm`: standalone WebAssembly 1.0 binary with the same typed scalar
  subset and arithmetic traps. No external tools, CGO, memory or imports are
  required. `i64` parameters/results use JavaScript BigInt; `bool` uses normalized
  i32. Use `-target wasm32` or leave the target unset.

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

### Explicit scalar JIT

`CompileJIT` / `CompileJITFile` compile the same typed scalar subset through QBE
and Keystone and install its position-independent code in the current process.
This path currently requires **Linux amd64 with CGO**, QBE and Keystone. The entry
accepts up to eight `i64`/`bool` arguments; Go passes/returns `int64`, normalizing
any nonzero boolean argument to true. A private typed trampoline handles register
and stack arguments without mismatched C function-pointer signatures.

```go
fn, err := ingagi.CompileJIT(ctx,
    `fn main(n: i64) -> i64 { return n * 2; }`, ingagi.NativeOptions{})
if err != nil { return err }
defer fn.Close()
value, err := fn.Call(21) // 42
```

Executable mappings transition from read/write to read/execute before publication.
`Close` is idempotent, serializes with calls, and releases the mapping; calls after
close fail. Callers must explicitly close their handles. The build context bounds
compilation, not execution. Arithmetic traps remain native process traps, and
native loops have no VM instruction budget. This explicit entry compilation does
not automatically tier VM functions or support closures/host callbacks.

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

### Source modules

`ingagi.CompileFile(sources fs.FS, filename string)` resolves imports and compiles
an owned, namespaced source graph. `a::b` resolves to `a/b.ing` below the supplied
filesystem. An explicit `module` declaration must match the imported path.
Imported modules expose `public` declarations; root declarations retain their
original names. Cycles, missing files, duplicate aliases, symbol conflicts and
references to private declarations produce diagnostics. Dependencies are included
once in dependency order, including diamond imports. Imports can be aliased:

```ingagi
import math as m;
fn main() { return m::twice(21); }
```

```go
module, err := ingagi.CompileFile(os.DirFS("scripts"), "main.ing")
```

CLI `run`, `check`, `dump`, `build` and `shader` use the input file's directory as
the module filesystem root. `Compile(source)` requires import-free source and
reports unresolved imports rather than silently ignoring them. The C embedding
ABI still accepts source strings only and provides no filesystem resolver.

### VM embedding

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

`VM.Encode`, `runtime.Decode` and `VM.LoadDecoded` provide an experimental v2
snapshot format (v1 images still decode, with unknown enum signatures).
Decode checks lengths, nested-value depth, record shapes,
duplicate entry/type/method/global/prefab records and aggregate keys, trailing bytes, operand widths,
constant indices, opcodes and control-flow targets. A control-flow stack verifier
checks both conditional edges, stack underflow and consistent join heights for
every entry and reachable deferred block. Module loads reject these errors before
replacing the current module. The same gate is applied to modules built in Go.
Descriptor validation checks type kinds, field/variant names, default-field keys,
enum payload signatures, method entries, system hooks, event types and prefab constructors.
Opcode verification checks struct/enum/closure/call descriptor operands, enum
payload arities for both `MakeEnum` construction and static `Type::Variant`
calls, method names and unused-operand hygiene. Snapshot values
are checked for valid object representations, enum variants and payload lengths,
and closure entries.
Loads restore nested builtin and host-callback values by name; required host
callbacks must be registered before loading. Aggregate storage is copied when
loading so separate VMs have independent state and callable bindings. Failed
image validation or callback binding preserves the previously loaded script.
Values remain dynamically typed; Dispatch also rejects
operand underflow relative to the current activation, including host-mutated tapes.

Frame-local indices are limited to 65,536 slots; functions may declare up to
65,536 parameters. Aggregate/call counts must fit the current activation's
operand stack. Repeated arrays are limited to 65,536 elements. These checks stop
specific unchecked allocations, not total script memory usage. Decoder collections
are limited to 65,536 elements, individual strings to 16 MiB, and nested snapshot
values to 64 levels.

Encoding orders symbol/type/method maps deterministically. It rejects cyclic or
deeply nested values and captured closures because snapshots cannot preserve their
environments. Named function closures remain supported. Object alias identity,
world state and dynamic observer registrations are not preserved by snapshots.

Reload replaces globals, arities and optimization profiles/caches while retaining
host registrations and the injected ECS world. Validation or initializer failure
restores the previous module and script state. Output, host callback effects and
ECS world mutations aren't rolled back. Module/image reload during an active
invocation is rejected.

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
`compiler.LookupTarget` and `ingagi targets`. IngagiVM, QBE and WASM are implemented;
the other nine describe planned adapters/shader emitters. `RequireArtifact`
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
- `sema.DefaultOptions()` enables scope, syntax, gradual type, ECS, shader,
  import, control-flow and constant-overflow checks. Checks use known annotations
  and inferred scalar/aggregate types; they don't implement generic
  monomorphization, complete nominal/width checking or dynamic host signatures.
  Security checks report native/pointer boundaries, including declaration defaults
  and shader bodies. Taint checks conservatively follow host/world data through
  locals, aggregate writes, branches, loop fixed points and script return summaries.
  Return summaries are solved before diagnostics, independent of declaration order.
  Output and world-mutation sinks produce `W1001` warnings. Parameters are treated
  as potentially host-supplied, so these review diagnostics can be noisy.
- Standalone GLSL/SPIR-V shader lowering supports the subset described below.
   GPU dispatch and renderer adapters remain pending.
  QBE and WASM are implemented scalar native backends; other target descriptors still
  describe planned adapters rather than claiming GPU integration.
- Bytecode image serialization is experimental (snapshot v2, decodes v1).
  Opcode-level descriptor verification and enum payload signatures are enforced
  at load, at snapshot install and at runtime construction; comprehensive static
  analysis, broader native/JIT targets,
  and aggregate/resource C ABI marshalling remain unfinished. The desktop workspace
  executes source through the scalar ABI; full scene/ECS/render-resource integration
  remains unfinished.

## Shader artifacts

`shader.CompileGLSL` emits Vulkan-compatible GLSL 450 for vertex, fragment and
compute stages. `shader.CompileSPIRV(ctx, declaration, stage, executable)` invokes
`glslangValidator -V --target-env vulkan1.1` and returns SPIR-V bytes. An empty
executable selects PATH; the caller's context bounds the compiler process.

```sh
GOROOT=/usr/lib/go go run ./cmd shader -shader Scale -stage compute \
  -emit glsl -o /tmp/opencode/scale.comp examples/compute.ing
GOROOT=/usr/lib/go go run ./cmd shader -shader Scale -stage compute \
  -emit spirv -o /tmp/opencode/scale.spv examples/compute.ing
spirv-val --target-env vulkan1.1 /tmp/opencode/scale.spv
```

The supported subset includes 32-bit scalar/vector/matrix types, structs,
constants, specialization constants with defaults, helper functions, explicit
typed locals, arithmetic, calls, indexing/member access, conditionals and loops.
Uniform/storage buffers and separate texture/sampler resources accept literal
`@set(N)` and `@binding(N)` attributes. Bindings default to set zero and the
resource's declaration index; duplicate bindings are rejected. Compute options
are `local_size` (x), `local_size_x`, `local_size_y`, `local_size_z`, defaulting to
one. Stage builtins use their GLSL names, such as `gl_GlobalInvocationID` and
`gl_Position`.

Fixed one-dimensional `[element; literal_length]` arrays (1..65536 elements)
are supported in shader structs and uniform/storage buffer fields.
Geometry and tessellation stages accept explicit layout options; storage images
use floating `rgba32f` texels. Unsupported host statements, inferred local types, resource/field attributes,
stage options and shader types fail with source-positioned errors. GLSL emission
is source lowering; SPIR-V compilation performs downstream GPU type/link checks.
Mesh/task lowering, stage-interface declarations, GPU execution and
Cg/DXIL/DXBC/SLang emitters remain pending.
Tests compile vertex/fragment/compute examples and validate their binaries with
`spirv-val` when both tools are installed.

### Cross-compiled shader sources

`shader.CompileArtifact(ctx, declaration, stage, format, shader.ToolOptions{})`
supports `glsl`, `spirv`, `hlsl` (Shader Model 5.0), `glsl-opengl` (450),
`glsl-es` (300, vertex/fragment only), `msl` (Metal 2.0) and `wgsl`
(vertex/fragment/compute). Cross-compiled source
passes through glslang's SPIR-V intermediate and vendored SPIRV-Cross. Both
processes use the caller context; failed builds preserve existing CLI outputs.

```sh
bash ingagi/scripts/build-shader-tools.sh
export INGAGI_SPIRV_CROSS=/tmp/opencode/ingagi-spirv-cross/spirv-cross
cd ingagi
GOROOT=/usr/lib/go go run ./cmd shader -emit hlsl -stage compute \
  -o /tmp/opencode/scale.hlsl examples/compute.ing
```

Select tools with `-glslang` / `-spirv-cross` or `INGAGI_GLSLANG` /
`INGAGI_SPIRV_CROSS`; unset paths fall back to PATH. Tests round-trip HLSL and
OpenGL GLSL through glslang and spirv-val, validate GLSL ES syntax, and check Metal
source generation when SPIRV-Cross is available. Metal compilation requires the
Apple toolchain. WGSL uses Naga 29.0.4 (matching vendored wgpu-native's lockfile)
to translate the SPIR-V intermediate while preserving coordinate space. Select it
with `-naga`, `ToolOptions.Naga` or `INGAGI_NAGA`. Set `INGAGI_BUILD_NAGA=1` when
running `build-shader-tools.sh` to install the pinned CLI through Cargo; its locked
crate dependencies are downloaded on the first build. Tests reparse emitted WGSL
and validate its SPIR-V round trip. Shader artifacts do not imply working renderer
adapters.

### MonkeyTrace ray-tracing renderer

[`monkeytrace`](monkeytrace/README.md) builds ECS-backed scenes, renders headless
PNGs with a portable CPU backend, and exports a self-contained WebGPU browser
host that executes an Ingagi compute shader. It supports spheres/triangles,
directional hard shadows, mirror reflections, emissive materials and bounded
supersampling. Run `go run ./monkeytrace/cmd/monkeytrace -o scene.png` with
`GOROOT=/usr/lib/go`; use `-backend webgpu -o scene.html` for GPU browser export
(glslang and Naga required). This dedicated renderer does not implement general
Ingagi pipeline dispatch or desktop GPU linkage.

### Desktop Game Engine workspace

Enable `JFX_INGAGI=ON` in the root CMake build and select **Game Engine**. The
workspace edits bounded, import-free source, opens/saves `.ing` files, and offers
Load/Reload, Call entry, and Run source. Each frontend owns a separate VM/world;
failed reloads preserve its installed script. Calls use 100,000 instructions and
256 frames. `engine_time()` reads the shared transport clock synchronously.
The scalar host bridge also exposes the shared editor's 3D scene:

| Callback | Behavior |
| --- | --- |
| `engine_scene_count()` | Current object count |
| `engine_scene_add(primitive)` | Create and return a zero-based object index; 0 cube, 1 sphere, 2 plane, 3 cylinder, 4 cone, 5 torus, 6 capsule, 7 pyramid, 8 disk, 9 tube, 10 hemisphere, 11 wedge, 12 tetrahedron, 13 octahedron, 14 icosahedron, 15 nurbs, 16 metaball |
| `engine_scene_remove(object)` | Remove an object; later indices shift |
| `engine_scene_get(object, channel)` | Read the stored transform channel |
| `engine_scene_set(object, channel, value)` | Commit an undoable transform edit |
| `engine_scene_key(object, channel, frame, value)` | Commit a linearly interpolated keyframe |
| `engine_scene_sample(object, channel, seconds)` | Read the animated transform channel |
| `engine_scene_cloner(object, mode, count, spacing)` | `3d.cloner` arrangement over source geometry |
| `engine_scene_cloner_real(object)` | `3d.cloner_make_real` at transport time; returns object count |
| `engine_scene_procedural(object, field)` | 0 generator, 1 instances, 2 cloner mode, 3 smooth |
| `engine_scene_color(object, channel, value)` | `3d.color` material channel 0..2 |

The 3D tab adds an **Ingagi drive** runner (loads shared source, calls the
named entry) and **.iap export/inspect** (`jfx_desktop_frontend_drive_3d`,
`jfx_desktop_frontend_export_iap`, `jfx_desktop_frontend_iap_info`). The
script-side library lives in `stdlib/ecs/joltfx_scene_ecs.ing`,
`stdlib/shader/joltfx_material.ing`,
`stdlib/gameplay-animation-ui/joltfx_drive.ing` and
`stdlib/assets-resources/joltfx_iap.ing`, with runnable
`examples/joltfx_3d_drive.ing` and `examples/joltfx_iap_pack.ing`. The pack
format is `docs/iap.md` (`IAP1` v1: animation event stream + assets + CRC);
the player is future work.

Channels 0–2 are position XYZ, 3–5 Euler degrees XYZ, and 6–8 scale XYZ. Indices
require integers; values/times accept finite integers or floats. Invalid arguments
and editor failures become script runtime errors. Callbacks reacquire the current
scene each time; edits share modeling history, rendering and persistence. The Game
Engine tab previews/scrubs this scene after a scene edit. A failed script/reload
does not roll back already committed scene edits. Scene indices are not persistent
ECS entity IDs; scripts must maintain their own associations and account for
removal, scene resets and editor undo/load.
Results and diagnostics are shown in the workspace. The scalar ABI discards
script printing and cannot return aggregate values; use Go embedding for those.
Desktop API 1.8 adds the workspace/panel and `jfx_desktop_frontend_game_*` entry
points without altering existing identifiers or config layouts.

## Regenerating the grammar

After editing `frontend/ingagi.ng`, from the repository root:

```sh
/tmp/opencode/ingagi-novoparse/novoparse generate ingagi/frontend/ingagi.ng \
  --profile C -o ingagi/frontend/generated/ingagi.h
/tmp/opencode/ingagi-novoparse/novoparse compile ingagi/frontend/ingagi.ng \
  -o ingagi/frontend/ingagi.sexp
```

Run the tagged suite to exercise the generated parser, not only grammar checking.
