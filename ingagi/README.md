# Ingagi — development snapshot

Ingagi is a Go-hosted scripting-language prototype for JoltFX's game-engine
workspace. Its language foundation is `frontend/Ingagi.ebnf`. This snapshot has
a parser/AST, bytecode compiler and VM, an ECS package, native math bridges, and
a small CLI. It is not yet connected to the desktop game-engine tab.

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
- Shader and pipeline syntax/metadata exist; shader code generation, GPU
  execution and the advertised multi-target compiler files remain placeholders.
- Bytecode image serialization is experimental. Decoding/validation, reload
  behavior, comprehensive static analysis, module imports, native/JIT targets,
  a C embedding ABI and desktop wiring remain unfinished.

## Regenerating the grammar

After editing `frontend/ingagi.ng`, from the repository root:

```sh
/tmp/opencode/ingagi-novoparse/novoparse generate ingagi/frontend/ingagi.ng \
  --profile C -o ingagi/frontend/generated/ingagi.h
/tmp/opencode/ingagi-novoparse/novoparse compile ingagi/frontend/ingagi.ng \
  -o ingagi/frontend/ingagi.sexp
```

Run the tagged suite to exercise the generated parser, not only grammar checking.
