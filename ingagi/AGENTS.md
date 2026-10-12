# Ingagi implementation notes

- Follow the root `AGENTS.md`; record progress in root `PROGRESS.md`.
- Run Go commands with `GOROOT=/usr/lib/go`.
- `frontend/Ingagi.ebnf` is the language foundation; `frontend/ingagi.ng` is the
  draft native grammar. The `novoparse` tag enables required native recognition
  followed by handwritten typed-AST construction. Keep both paths covered.
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
  stacks. These limits are not a sandbox. Structural module/image validation is
  in `runtime/validate.go` and `runtime/aot.go`; full stack/type verification is
  pending. Check counts before allocating, relative to the activation's stack
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
- QBE generates assembly; `internal/keystone` uses the vendored C ABI to assemble
  it. Preserve CET bytes and explicitly convert decimal AT&T operands to hex
  before Keystone. Object output is a relocatable AMD64 ELF with one exported
  `ingagi_entry` stub; all internal calls remain position-independent.
- Native execution tests require `INGAGI_QBE` and `INGAGI_KEYSTONE_LIBRARY`.
  `scripts/build-native-tools.sh` builds tools outside the source tree. Read
  `ingagi/DEPENDENCIES.md` for the pinned missing-source supplement.
- Native-module ABI 1.0.0 is defined in `native_extension_api/Ingagi-Module.h`;
  `joltfx_embed` implements it with opaque handles and synchronous callbacks.
  Keep layouts/statuses synchronized across all six language bindings. Callbacks
  borrow argument/result buffers only for the call; preserve the Busy reentry
  gate and test failed reloads and callback lifetimes.
- `compiler/targets/*.sexp` are schema-v1 metadata, embedded and validated by
  the compiler. Only IngagiVM/QBE are implemented; a planned descriptor is not
  a working emitter. Most semantic passes and shader emitters remain unfinished.
- Standalone embedding checks: configure/build `ingagi/CMakeLists.txt`, then run
  CTest. Optional D/Rust/Python/Java smoke tests run when compilers are available.
  Read `README.md` and `native_extension_api/README.md` before describing support.
