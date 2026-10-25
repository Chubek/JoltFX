# Ingagi implementation notes

- Follow the root `AGENTS.md`; record progress in root `PROGRESS.md`.
- Run Go commands with `GOROOT=/usr/lib/go`.
- `DEVELOPMENT.md` tracks implemented versus reserved subsystems. The shader
  implementation is in `monkeyshade` (package shader); `shader` is a compatibility
  facade. Put lowering fixes in the implementation, never duplicate them.
- `noise` uses immutable seeded improved-Perlin lattices and strict bounded JSON
  profiles; preserve deterministic seed expansion and normalization. `geometry`
  owns bounded mesh slices, welded outward winding and failure-atomic transforms.
  Procedural VM builtins marshal ordinary arrays/maps with a 65,536-entry limit;
  retain no engine/GPU handles. Keep scope/type checks aligned with builtin names,
  including local shadows, and test malformed aggregate representations.
- Target catalog entries cover all sixteen declared targets. Only IngagiVM,
  QBE and WASM are execution backends. Adapter `.go` scaffolds use the `ignore`
  build constraint until they have real implementations; do not remove those
  guards just to make an empty adapter appear implemented.
- `frontend/Ingagi.ebnf` is the language foundation; `frontend/ingagi.ng` is the
  draft native grammar. The `novoparse` tag enables required native recognition
  followed by handwritten typed-AST construction. Keep both paths covered.
  Member recovery may stop at another declaration keyword: entity/system/pipeline
  loops must still advance on a zero-progress recovery. Keep bounded subprocess
  regressions in parser_hardening_test.go to prevent host memory exhaustion.
- Generated `frontend/generated/ingagi.h` and `frontend/ingagi.sexp` must be
  regenerated with the vendored NovoParse CLI after grammar edits. The native
  test script checks deterministic regeneration.
- `runtime/math_bridge.cc` calls vendored GLM and simdette through a synchronous
  private C bridge. Go owns all passed storage; C must not retain its pointers.
  Keep `CGO_ENABLED=0` working and test both variants. Simdette currently uses
  scalar double-precision vector math here; do not claim hardware acceleration.
- First-class geometry lives in `runtime/geometry_values.go`: immutable owned
  `mesh` storage and by-value `aabb`, detached field arrays, validated affine
  transforms and structural equality. Generator results are typed; legacy map
  inputs remain accepted. Snapshot v3 adds these kinds and reads v1/v2; validate
  malformed payloads and clone mesh storage at every image installation.
  `geometry/triangle_bridge.cc` uses libigl projected triangle double areas with
  Eigen 3.4 from Bullet's vendored ThirdPartyLibs (root Eigen is incompatible).
  Keep it synchronous and fixed-size; no retained pointers or library threads.
  Math builtins/type rules are in `runtime/linear_algebra.go` and `sema/spatial.go`;
  preserve dimension checks, local function shadows and native/portable parity.
- The compiler emits nested function bodies inline behind skip jumps. Function
  scopes own slots, nil-propagation fixups and loop contexts. Optimizations must
  relocate all entries, branches and deferred-block targets and handle cycles.
- VM budgets span nested calls; runtime failures must unwind call/operand/iterator
  stacks. These limits are not a sandbox. Failed module initializers restore the
  prior VM script state; host/output/world side effects are not rolled back.
  Reject source/image reload during active execution. Structural validation is
  in `runtime/validate.go` and `runtime/aot.go`; control-flow stack verification
  covers conditional edges and deferred blocks. runtime/descriptors.go checks
  descriptor references and enum payload-signature consistency; runtime/image_values.go validates snapshot object
  representations and clones/rebinds nested callable values before installation.
  Opcode verification (`verifyOpcodeTypes`) checks struct/enum/closure/call
  descriptor operands, enum payload arities for MakeEnum and static
  Type::Variant calls, method names and unused-operand hygiene; `OpBind` uses
  the bindAB kind (C must be 0) and `OpForEachBegin` uses foreachA (B/C must be
  0) since their pipeline/world operands travel on the stack. Enum arities live
  in `TypeInfo.EnumArity` (nil means unknown, from v1 images); snapshot v2
  preserves them and still decodes v1. `hMakeEnum` and `callNamed` enforce the
  signatures at runtime, and `check` validates the lowered tape.
  Required host callbacks must be registered first; never mutate shared Decoded
  values while binding them to a VM. Check counts before allocating, relative to the activation's stack
  base. Enforce frame limits before growing locals or padding missing arguments.
- Keep snapshot encoding deterministic. Snapshots must reject captured closures
  rather than silently losing environments; cyclic/deep values must fail cleanly.
  Reload must reset globals/arities and code-site profiles/caches. Decode fuzzing
  must not execute arbitrary accepted tapes without full verification.
- Tests: `GOROOT=/usr/lib/go go test ./... -timeout 30s` from `ingagi/`,
  `GOROOT=/usr/lib/go CGO_ENABLED=0 go test ./... -timeout 30s`, and
  `bash ingagi/scripts/test-native.sh` from repository root for NovoParse.
- Native scalar lowering is in `compiler/qbe.go`: explicit `i64`/`bool`
  signatures, lexical local scopes, arithmetic, calls and structured control
  flow. Reject unsupported declarations/expressions rather than omitting them.
- `compiler/wasm.go` emits WebAssembly 1.0 binaries using the shared typed-scalar
  gate. Keep wrap/trap semantics aligned with QBE and normalize bool arguments.
  Node execution tests validate real binaries, recursion and structured branches.
- QBE generates assembly; `internal/keystone` uses the vendored C ABI to assemble
  it. Preserve CET bytes and explicitly convert decimal AT&T operands to hex
  before Keystone. Object output is a relocatable AMD64 ELF with one exported
  `ingagi_entry` stub; all internal calls remain position-independent.
- Native execution tests require `INGAGI_QBE` and `INGAGI_KEYSTONE_LIBRARY`.
  Explicit scalar JIT uses compiler/jit.go and internal/machine: Linux amd64 CGO,
  up to eight scalar arguments through a uniform eight-i64 trampoline. Mapping
  transitions RW to RX; Call/Close serialize and no executable pointer escapes.
  Keep portable stubs working. Native execution has no VM budget/trap recovery.
  `scripts/build-native-tools.sh` builds tools outside the source tree. Read
  `ingagi/DEPENDENCIES.md` for the pinned missing-source supplement.
- Native-module ABI 1.0.0 is defined in `native_extension_api/Ingagi-Module.h`;
  `joltfx_embed` implements it with opaque handles and synchronous callbacks.
  Keep layouts/statuses synchronized across all six language bindings. Callbacks
  borrow argument/result buffers only for the call; preserve the Busy reentry
  gate and test failed reloads and callback lifetimes.
- `compiler/targets/*.sexp` are schema-v1 metadata, embedded and validated by
  the compiler. IngagiVM/QBE/WASM are implemented; a planned descriptor is not
  a working emitter. Sema implements scope, syntax, gradual types, ECS, shader,
  import, logic, constant-overflow, security and conservative taint checks. Taint
  summaries and loop flows use fixed points; visit all children even after finding
  taint, preserve lexical shadows and do not let declaration order change findings.
- `module_system.Loader` resolves imports below an explicit fs.FS, deduplicates
  dependency units and qualifies an owned AST while preserving lexical bindings.
  Keep qualified names through struct/ECS lowering. CLI file commands share this
  resolver; source-only embedding must reject unresolved imports.
- `shader.CompileGLSL` lowers an explicit GPU subset to GLSL 450;
  `CompileSPIRV` uses a context-bounded glslangValidator process. Test the emitted
  vertex/fragment/compute binaries with spirv-val when available. GPU execution
  remains unfinished. `CompileArtifact` cross-compiles HLSL, OpenGL/ES GLSL and
  MSL with context-bounded SPIRV-Cross; storage images use rgba32f texels.
  WGSL uses Naga 29.0.4 via a bounded subprocess and is validated by WGSL/SPIR-V
  round trips; geometry/tessellation aren't WebGPU stages and must reject.
  Don't mark renderer adapters as
  implemented solely because shader artifacts can be generated.
- Shader `interface` declarations carry `ShaderStruct.Interface`; do not erase
  them into ordinary buffer structs. `inputs`/`outputs` options use the existing
  grammar and access generated variables through reserved `ig_interface_` names.
  Keep stage inputs read-only and reject namespace capture. Shader-local names
  are private to the GPU declaration during module qualification.
- `monkeyshade.Reflect` reports declared source layouts (not optimized SPIR-V).
  Preserve std140/std430 matrix/array strides, vec3 packing, bounded recursion and
  byte/location budgets. `LinkGraphics` matches locations/types/qualifiers and
  merges structurally compatible descriptors; it is not fixed-state or device
  validation. Keep `shader` compatibility exports and CLI reflection aligned.
  Extend real glslang/spirv-val/decoration tests and source-link failures before
  broadening interface types, layouts or pipeline stage topologies.
- `monkeyshade/filament.go` adds `shader(target:"filament")` surface materials.
  Reuse the GLSL body emitter and existing reflection/link checks; `.mat` and
  `.filamat` are whole-material artifacts (`stage="material"`). Uniform fields
  become named parameters, textures become Filament samplers, and smooth vec4
  interface locations 0..3 become custom varyings. Reject unsupported bindings,
  stages and namespace shadows. `ReflectFilament` describes parameter names, not
  Vulkan layouts. `matc` runs with bounded context/output and validates package
  framing; set `INGAGI_MATC` to the matching vendored SDK tool for real tests.
  No renderer/device support follows implicitly from material compilation.
- Standalone embedding checks: configure/build `ingagi/CMakeLists.txt`, then run
  CTest. Optional D/Rust/Python/Java smoke tests run when compilers are available.
  Read `README.md` and `native_extension_api/README.md` before describing support.
- `monkeytrace` snapshots Ingagi ECS geometry/material values and compiles
  `trace.ing` through the shader package. Its CPU reference and dedicated browser
  WebGPU host share a fixed std430/std140 scene layout. Preserve shader/CPU
  parity, owned snapshots, edge-invocation guards and 256-byte readback padding.
  `LoadScene` uses a fresh budgeted IngagiVM with typed `trace_*` hosts and real
  ECS handles; scene errors must not publish a partially authored world.
  Snapshot BVHs are deterministic preorder trees with conservative float32
  bounds and skip links. Closest-hit ties preserve primitive/entity-ID order;
  shadow rays exit early. Keep capacity constants, Ingagi arrays, binding 3 and
  the node count in `sampling.y` synchronized (4096 primitives / 8191 nodes).
  Secondary-ray offsets are scale-aware to survive float32 rounding; preserve
  CPU/shader parity. `MONKEYTRACE_WEBGPU_TEST=1` opts into real browser tests for
  empty, demo, procedural and full-capacity large-coordinate scenes.
  Fixed GPU arrays are bounded literal fields, not general host array lowering.
  Browser integration is optional via `monkeytrace/scripts/test-webgpu.mjs`;
  other shader targets and general Ingagi GPU dispatch remain separate work.
- `monkeytrace/native` exposes native ABI 1.0 for synchronous CPU rendering of
  borrowed C triangle/camera descriptors. Copy inputs into an owned ECS world,
  retain no C pointers, and publish RGBA only on success. JFX_MONKEYTRACE builds
  this bridge independently of the scripting tab; no shader tools are required.
  The CMake bridge uses `-tags=monkeytrace_native` to exclude VM-authoring and
  shader/browser tools from this CPU-only dependency graph. The ordinary package
  still includes those APIs. Test the isolated bridge with
  `GOROOT=/usr/lib/go go test -tags=monkeytrace_native ./monkeytrace/native ./ecs`.
- Desktop API 1.8's Game Engine tab links the native ABI conditionally through
  JFX_INGAGI. It owns handles until frontend destruction and borrows frontend state
  synchronously in engine_time callbacks. Keep optional builds and ingagi_tab tests
   working. engine_scene_* callbacks reacquire the current scene and mutate via
   shared editor commands/history. Object indices shift on removal and are not
   ECS IDs. Scene/resource associations remain script-owned; GPU linkage is pending.
