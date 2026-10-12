# Ingagi implementation notes

- Follow the root `AGENTS.md`; record progress in root `PROGRESS.md`.
- Run Go commands with `GOROOT=/usr/lib/go`.
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
- The compiler emits nested function bodies inline behind skip jumps. Function
  scopes own slots, nil-propagation fixups and loop contexts. Optimizations must
  relocate all entries, branches and deferred-block targets and handle cycles.
- VM budgets span nested calls; runtime failures must unwind call/operand/iterator
  stacks. These limits are not a sandbox. Failed module initializers restore the
  prior VM script state; host/output/world side effects are not rolled back.
  Reject source/image reload during active execution. Structural validation is
  in `runtime/validate.go` and `runtime/aot.go`; control-flow stack verification
  covers conditional edges and deferred blocks. runtime/descriptors.go checks
  descriptor references; runtime/image_values.go validates snapshot object
  representations and clones/rebinds nested callable values before installation.
  Required host callbacks must be registered first; never mutate shared Decoded
  values while binding them to a VM. Complete opcode-level type verification and
  enum payload signatures remain pending. Check counts before allocating, relative to the activation's stack
  base. Enforce frame limits before growing locals or padding missing arguments.
- Keep snapshot encoding deterministic. Image v1 must reject captured closures
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
- Standalone embedding checks: configure/build `ingagi/CMakeLists.txt`, then run
  CTest. Optional D/Rust/Python/Java smoke tests run when compilers are available.
  Read `README.md` and `native_extension_api/README.md` before describing support.
- Desktop API 1.8's Game Engine tab links the native ABI conditionally through
  JFX_INGAGI. It owns handles until frontend destruction and borrows frontend state
  synchronously in engine_time callbacks. Keep optional builds and ingagi_tab tests
   working. engine_scene_* callbacks reacquire the current scene and mutate via
   shared editor commands/history. Object indices shift on removal and are not
   ECS IDs. Scene/resource associations remain script-owned; GPU linkage is pending.
