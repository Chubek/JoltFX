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
  stacks. These limits are not a sandbox. Serialization still needs validation.
- Tests: `GOROOT=/usr/lib/go go test ./... -timeout 30s` from `ingagi/`,
  `GOROOT=/usr/lib/go CGO_ENABLED=0 go test ./... -timeout 30s`, and
  `bash ingagi/scripts/test-native.sh` from repository root for NovoParse.
- Read `README.md` before describing support. Shader/native target files and most
  semantic passes are placeholders, not implemented backends or analyses.
