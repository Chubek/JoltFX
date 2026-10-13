# AGENTS.md — Joltscript

## Overview

Joltscript is a Lisp dialect designed specifically for authoring JoltFX kernels. It prioritizes data-oriented transformation logic, compile-time metaprogramming, and seamless interoperability with C and the JoltFX Core engine.

This document covers the language specification, compiler architecture, and contribution guidelines for the Joltscript compiler (`joltc`), runtime, and standard library.

**Implementation checkpoint — Phases 1–2 complete, execution ABI 0.9:** the
compiler emits legacy f32 JBC1 version 1 or typed version 2 with primitives,
loops, functions/closures and six built-in library modules (262 exports).
`docs/language.md` and `docs/stdlib.md` describe the shipped syntax, execution
budgets and CPU/backend boundaries. The specification/layout below includes
future designs such as macros, user modules, FFI, structs, native source targets
and `jolt.io`; those are not implied by the Phase 1–2 completion checkpoint.

---

## Repository Layout

```
joltscript/
  compiler/              # joltc compiler frontend
    frontend/            # parser, typechecker, AST
    middle/              # IR generation, optimization passes
    backend/
      c/                 # C codegen
      rust/              # Rust codegen
      python/            # Python codegen
      go/                # Go codegen
  runtime/               # Joltscript runtime (ARC, arena, intrinsics)
    arc.c                # Automatic reference counting
    arena.c              # Kernel-scoped arena allocator
    intrinsics/          # Built-in intrinsic implementations
  stdlib/                # Standard library (Joltscript source)
    math.jolt
    graphics.jolt
    geometry.jolt
    time.jolt
    io.jolt
    collections.jolt
    strings.jolt
  tools/
    joltc/               # Compiler driver
    jolti/               # REPL
    joltfmt/             # Formatter
    joltdoc/             # Documentation generator
    joltscript-lsp/      # LSP server
  tests/
    unit/                # Compiler unit tests
    integration/         # End-to-end kernel tests
    conformance/         # Cross-target conformance
  benchmarks/            # Performance benchmarks
```

---

## Design Principles

1. **Kernel-first**: Every Joltscript program is a kernel or composes kernels.
2. **Stateless by default**: Side effects and shared state are explicit, not implicit.
3. **Multi-target**: Compiles to C, Rust, Python, and Go with identical semantics.
4. **Zero-cost abstractions**: High-level constructs compile to efficient native code.
5. **C ABI compatibility**: Full bidirectional interop with C functions and data structures.
6. **Deterministic compilation**: Same input always produces same output (reproducible builds).

---

## Language Specification

### Syntax

Joltscript uses S-expressions exclusively. All code is data; all data is code.

```lisp
;; Atoms
42
3.14159
"hello"
:keyword
'symbol

;; Lists (function calls, macro invocations)
(function arg1 arg2 arg3)

;; Vectors (arrays) — fixed-size or dynamic
[1 2 3 4]
[i32 ; 4]          ;; fixed-size array type
[i32]              ;; slice type

;; Maps (dictionaries)
{:x 100 :y 200 :z 300}

;; Quoting
'(1 2 3)           ;; quoted list (data)
`(1 2 ~x)         ;; quasiquote with unquote
```

### Comments

```lisp
;; Single-line comment

#| Multi-line
   comment |#

#;(discard this form)  ;; discard next form (reader conditional)
```

### Types

Joltscript is **statically typed** with **type inference**. Types can be declared explicitly or inferred from context.

#### Primitive Types

| Type       | Description                          | Literal Suffix | Example                |
|------------|--------------------------------------|----------------|------------------------|
| `i8`       | 8-bit signed integer                 | `i8`           | `42i8`                 |
| `i16`      | 16-bit signed integer                | `i16`          | `1000i16`              |
| `i32`      | 32-bit signed integer (default int)  | —              | `100000`               |
| `i64`      | 64-bit signed integer                | `i64`          | `9223372036854775807i64` |
| `u8`       | 8-bit unsigned integer               | `u8`           | `255u8`                |
| `u16`      | 16-bit unsigned integer              | `u16`          | `65535u16`             |
| `u32`      | 32-bit unsigned integer              | `u32`          | `4294967295u32`        |
| `u64`      | 64-bit unsigned integer              | `u64`          | `18446744073709551615u64` |
| `f32`      | 32-bit floating point                | `f32`          | `3.14f32`              |
| `f64`      | 64-bit floating point (default float)| —              | `2.71828`              |
| `bool`     | Boolean                              | —              | `true`, `false`        |
| `char`     | Unicode scalar value                 | —              | `\a`, `\u{1F600}`      |
| `string`   | UTF-8 string                         | —              | `"hello world"`        |
| `void`     | Unit type (no value)                 | —              | `()`                   |

#### Composite Types

```lisp
;; Fixed-size array
[i32 ; 4]              ;; array of 4 i32s

;; Slice (view into array or dynamic sequence)
[i32]

;; Tuple
(i32 f64 string)

;; Struct
(defstruct Point
  (x f64)
  (y f64))

;; Enum (tagged union)
(defenum Color
  Red
  Green
  Blue
  RGB (r u8) (g u8) (b u8))

;; Function type
(fn [i32 i32] -> i32)

;; Pointer (unsafe, FFI only)
(* i32)

;; Opaque handle (engine-managed resource)
(handle ImageBuffer)
```

#### Type Annotations

```lisp
;; On bindings
(let (x i32) 42)
(var (total f64) 0.0)

;; On function signatures
(defn add [(a i32) (b i32)] -> i32
  (+ a b))

;; On struct fields (redundant but allowed)
(defstruct Point
  (x f64)
  (y f64))
```

### Variables and Bindings

#### Immutable Bindings (Preferred)

```lisp
(let x 42)
(let (x y z) (values 1 2 3))   ;; destructuring
(let [a b c] [1 2 3])          ;; array destructuring
(let {:keys [x y]} {:x 1 :y 2}) ;; map destructuring
```

#### Mutable Bindings (Explicit Opt-In)

```lisp
(var counter 0)
(set! counter (+ counter 1))

;; Scoped mutation
(with-mutation
  (var local-x 10)
  (set! local-x 20))
```

#### Constants

```lisp
(const PI 3.141592653589793)
(const MAX_ITERATIONS 100)
```

### Functions

#### Definition

```lisp
;; Basic function
(defn add [a b]
  (+ a b))

;; With type annotations
(defn add [(a i32) (b i32)] -> i32
  (+ a b))

;; Multi-expression body (implicit do)
(defn compute [x y]
  (let temp (* x y))
  (+ temp 10))

;; Variadic
(defn sum [& nums]
  (reduce + 0 nums))

;; Anonymous function (lambda)
(fn [x] (* x x))
(λ [x] (* x x))     ;; λ is an alias for fn
```

#### Calling

```lisp
(add 10 20)
((fn [x] (* x x)) 5)
(apply + [1 2 3 4])
```

#### Higher-Order

```lisp
(map (fn [x] (* x 2)) [1 2 3 4])
(filter (fn [x] (> x 10)) [5 10 15 20])
(reduce + 0 [1 2 3 4 5])
```

### Kernels

A **kernel** is Joltscript's fundamental unit of composition. Kernels are declared with `defkernel`.

```lisp
(defkernel gaussian-blur
  :inputs  [(src ImageBuffer)]
  :outputs [(dst ImageBuffer)]
  :params  [(radius f32 :default 2.0 :range [0.5 64.0])
            (quality i32 :default 3 :range [1 8])
            (alpha-aware bool :default true)]
  
  (let width (image-width src))
  (let height (image-height src))
  
  ;; Implementation using intrinsics
  (for-each-pixel [x y] [width height]
    (let result (js_gaussian_sample src x y radius quality alpha-aware))
    (set-pixel! dst x y result)))
```

#### Kernel Composition

```lisp
;; Sequential composition
(compose gaussian-blur sharpen color-correct)

;; Parallel composition (independent kernels)
(parallel
  (gaussian-blur :radius 10)
  (edge-detect :threshold 0.5))

;; Conditional composition
(if-condition (> frame-count 100)
  (gaussian-blur :radius 5)
  (identity))
```

#### Kernel Metadata

Required metadata fields (as comments at file top):
- `@kernel` — Kernel name (snake_case)
- `@category` — Category folder name
- `@description` — One-line description
- `@complexity` — Low | Medium | High
- `@gpu` — Yes | No | Partial
- `@since` — Version introduced (e.g., `2.1.0`)

### Control Flow

#### Conditionals

```lisp
(if condition
  then-expr
  else-expr)

(cond
  [(< x 0) "negative"]
  [(= x 0) "zero"]
  [(> x 0) "positive"]
  [else "unreachable"])

(when condition
  body...)

(unless condition
  body...)
```

#### Loops

```lisp
;; For loop (range)
(for [i 0 10]          ;; i from 0 to 9
  (println i))

(for [i 0 100 5]      ;; step of 5
  (println i))

;; While loop
(while (< counter 100)
  (set! counter (+ counter 1)))

;; Loop with break/continue
(loop
  (when (= counter 100) (break))
  (when (even? counter) (continue))
  (set! counter (+ counter 1)))

;; Iterator-based
(for-each [item items]
  (println item))

(for-each-indexed [i item items]
  (println i item))
```

### Iteration Helpers

```lisp
(map fn coll)           ;; lazy sequence
(filter pred coll)
(reduce fn init coll)
(take n coll)
(drop n coll)
(partition n coll)
(partition-all n coll)
```

### Macros

Joltscript supports **hygienic macros** with full compile-time metaprogramming.

```lisp
;; Simple macro
(defmacro unless [condition body]
  `(if (not ~condition)
     ~body))

;; Macro with gensym (guaranteed unique symbol)
(defmacro with-temp [binding body]
  (let temp (gensym))
  `(let ~temp ~(second binding)
     (let ~(first binding) ~temp
       ~body)))

;; Variadic macro
(defmacro benchmark [name & body]
  `(do
     (let start (time-now))
     ~@body
     (let end (time-now))
     (println ~name " took " (- end start) "ms")))

;; Macro expanding to multiple forms
(defmacro defkernel* [name & clauses]
  `(do
     (defkernel ~name ~@clauses)
     (register-kernel-metadata ~name)))
```

### Interop with C

#### Importing C Functions

```lisp
(extern-c "math.h"
  (sin [f64] -> f64)
  (cos [f64] -> f64)
  (sqrt [f64] -> f64))

(println (sin 3.14159))
```

#### Exporting to C

```lisp
(export-c compute-color
  [(r u8) (g u8) (b u8)] -> u32
  (bitwise-or
    (shl r 16)
    (shl g 8)
    b))
```

#### C Struct Interop

```lisp
;; Import C struct
(extern-struct SDL_Rect
  (x i32)
  (y i32)
  (w i32)
  (h i32))

;; Use it
(let rect (SDL_Rect 10 20 100 50))
(println (. rect x))
```

### Memory Management

Joltscript uses **automatic reference counting (ARC)** for heap-allocated data and **arena allocation** for kernel-scoped temporaries.

```lisp
;; Stack allocation (default for primitives and small structs)
(let point (Point 10.0 20.0))

;; Heap allocation (explicit, ARC-managed)
(let buffer (alloc ImageBuffer width height))

;; Arena allocation (kernel-scoped, freed at kernel completion)
(with-arena
  (let temp-buffer (arena-alloc ImageBuffer width height))
  ;; temp-buffer freed automatically at end of with-arena
  )

;; Manual retain/release (rare, for FFI)
(retain buffer)
(release buffer)
```

### Modules and Imports

```lisp
;; Define module
(module jolt.fx.color
  (export
    rgb
    rgba
    hsv->rgb
    rgb->hsv))

;; Import module
(import jolt.fx.color)
(rgb 255 128 64)

;; Selective import
(import jolt.fx.color (rgb rgba))

;; Alias
(import jolt.fx.color :as color)
(color/rgb 255 128 64)

;; Re-export
(module jolt.fx.compositing
  (re-export jolt.fx.color))
```

### Error Handling

Joltscript uses **Result types** for recoverable errors and **panics** for unrecoverable errors.

```lisp
;; Result type (built-in)
(defenum (Result T E)
  (Ok T)
  (Err E))

;; Function returning Result
(defn divide [(a f64) (b f64)] -> (Result f64 string)
  (if (= b 0.0)
    (Err "division by zero")
    (Ok (/ a b))))

;; Pattern matching on Result
(match (divide 10.0 2.0)
  [(Ok value) (println "Result: " value)]
  [(Err msg) (println "Error: " msg)])

;; Unwrap (panics on Err)
(let result (unwrap (divide 10.0 2.0)))

;; Unwrap with default
(let result (unwrap-or (divide 10.0 0.0) 0.0))

;; Try-catch for panics
(try
  (risky-operation)
  (catch [e] (println "Caught: " e)))
```

---

## Compilation Targets

Joltscript compiles to four target languages:

### C Target

```bash
joltc --target=c input.jolt -o output.c
```

Generated C is idiomatic, readable, and interoperable with existing C codebases. Uses Tilly's C runtime for ARC and arena management.

### Rust Target

```bash
joltc --target=rust input.jolt -o output.rs
```

Generates safe Rust with explicit lifetime annotations. Uses `Arc<T>` for ARC types.

### Python Target

```bash
joltc --target=python input.jolt -o output.py
```

Generates Python 3.10+ with type hints. Uses reference counting (native) for ARC.

### Go Target

```bash
joltc --target=go input.jolt -o output.go
```

Generates idiomatic Go with goroutines for parallel kernels. Uses Go's GC; ARC mapped to reference semantics.

---

## Standard Library

Joltscript ships with six checked intrinsic-interface modules (262 exports):

| Module | Contents |
|--------|----------|
| `jolt.math` | Trigonometry, linear algebra, interpolation, random |
| `jolt.graphics` | Color spaces, transforms, filters, blend modes |
| `jolt.geometry` | Points, vectors, matrices, quaternions, splines |
| `jolt.time` | Curves, easing functions, keyframe interpolation |
| `jolt.collections` | Lists, vectors, maps, sets, sequences |
| `jolt.strings` | Manipulation, parsing, formatting, regex |

The `.jolt` interfaces generate a build-time catalogue; implementations are
budgeted typed-VM intrinsics. `jolt.io` remains a design target; built-in imports
provide no filesystem access. See `docs/stdlib.md` for the implemented API.

---

## Example: Complete Kernel

```lisp
;; kernels/distortion/wave-distortion.jolt
// @kernel      wave_distortion
// @category    distortion
// @description Applies a sine-wave horizontal displacement to an image.
// @complexity  Medium
// @gpu         Yes
// @since       2.0.0

(module jolt.fx.kernels.distortion)

(import jolt.core (ImageBuffer rgba for-each-pixel image-width image-height))
(import jolt.math (clamp sin))

(defkernel wave-distortion
  :inputs  [(source ImageBuffer)]
  :outputs [(result ImageBuffer)]
  :params  [(amplitude f64 :default 10.0 :range [0.0 100.0])
            (frequency f64 :default 0.1 :range [0.01 10.0])
            (time f64 :default 0.0)]
  
  (let width (image-width source))
  (let height (image-height source))
  
  (for-each-pixel [x y] [width height]
    (let offset-x (+ x (* amplitude (sin (* frequency (+ y time))))))
    (let offset-y y)
    (let clamped-x (clamp offset-x 0 (- width 1)))
    (let pixel (sample source clamped-x offset-y))
    (set-pixel! result x y pixel)))
```

---

## Compiler Architecture

```
Source (.jolt)
    │
    ▼
Parser (recursive descent, tree-sitter grammar)
    │
    ▼
AST (S-expression based, typed nodes)
    │
    ▼
Type Checker (Hindley-Milner with extensions)
    │
    ▼
IR (Joltscript IR — SSA-based, kernel-aware)
    │
    ├─► C Backend        ──► .c/.h
    ├─► Rust Backend     ──► .rs
    ├─► Python Backend   ──► .py
    └─► Go Backend       ──► .go
```

### IR Design

- SSA form with explicit phi nodes
- Kernel boundaries preserved as first-class IR constructs
- Intrinsic calls lowered to target-specific implementations
- Memory operations annotated with lifetime (arena/heap/stack)

---

## Tooling

The JBC1 compiler now implements f32 `let`/`var`/`set!`, scoped vector bindings,
`do` and implicit scalar kernel sequences. Execution ABI 0.7 adds forward-only
branches for lazy `if`/`cond`/`when`/`unless`, with path-sensitive stack, output
and local-initialization validation. Keep C and Zoltan emission identical and add fixtures to
`tests/conformance/*.jolt` for `scripts/check-bytecode-parity.sh`. See
`docs/language.md` for current semantics; the full type/module language above
remains the design target.

`tests/conformance/check_conditionals.py` checks C/Rust byte parity, rejects
invalid scopes/arity, and executes HTML exports with Node. Pass `--wasm` with
Python wasmtime installed to assemble and execute WAT exports as well.

Phase 1's remaining core forms/types use version-2 tree bytecode (ABI 0.8).
`layers/glue/src/typed_compiler.c` is the bounded reader/serializer;
`layers/execution/src/typed_program.c` validates types and executes budgeted
loops, calls and closures with exact primitive values. Preserve the original
version-1 path. `value.h` owns the tagged-value/lifetime contract. Tests live in
`tests/unit/test_typed.c` and `tests/conformance/check_phase1.py`; the latter
compares independent C/Rust emission and exact REPL results. Zoltan shares the
portable typed checker/VM via a Rust allocator bridge, not the C emitter.

Phase 2 (execution ABI 0.9) makes all six `stdlib/*.jolt` interfaces executable.
`stdlib/generate.py` generates the catalogue used by `typed_program.c`;
`stdlib_runtime.inc` implements its intrinsics with the invocation arena/budget.
Imports are embedded built-in module resolution, not filesystem loading.
Add an execution/parity case in `tests/conformance/check_phase2.py` for every new
export; it enforces complete catalogue coverage. `test_stdlib` covers aggregate
ownership, callbacks, limits and failure atomicity. See `docs/stdlib.md` for the
authoritative API and build dependencies (Python 3, CMake, vendored PCRE2).

Native compiler/tool buffers use Tilly's MemTKX-backed allocation. Pair process
helpers in `tilly/memory.h` with `tilly_mem_free`; validate size arithmetic, OOM
and input boundaries before access. The LSP caps exact-length messages at 16 MiB
and frees the current request before exit. `memory_tool_edges` exercises empty,
large and malformed formatter/documentation/LSP input under sanitizers.

| Tool | Purpose |
|------|---------|
| `joltc` | JBC1 compiler/checker; separate CPU image validation |
| `jolti` | Independent complete-kernel evaluations, exact typed/collection printing |
| `joltfmt` | Preliminary token formatter; currently drops comments/metadata |
| `joltdoc` | Preliminary source/comment documentation extraction (Markdown/HTML) |
| `joltscript-lsp` | Stdio protocol scaffold; semantic services are Phase 3 work |

### Planned formatter rules (`joltfmt`)

- 2-space indentation
- Align `let`/`var` bindings vertically when > 2
- Trailing commas in multi-line collections
- Max line width: 100 columns
- Sort imports alphabetically by module path

---

## Planned performance characteristics

- **Zero-cost abstractions**: Lisp forms compile to native loops and function calls
- **Tail-call optimization**: Guaranteed for self-recursive and mutual recursion
- **SIMD vectorization**: Automatic for image/array operations (`map`, `for-each-pixel`)
- **Inline expansion**: Small kernels inlined at call sites
- **Dead code elimination**: At compile time across kernel boundaries
- **Constant propagation**: Across kernel composition boundaries

---

## Contribution Guidelines

### Adding a New Intrinsic

1. Add declaration to `compiler/frontend/intrinsics.jolt`
2. Implement in `runtime/intrinsics/<name>.c` (C reference implementation)
3. Add lowering rules in each backend (`compiler/backend/<target>/intrinsics/`)
4. Add round-trip test in `tests/integration/intrinsics/`
5. Document in `stdlib/` if user-facing

### Adding a New Stdlib Function

1. Declare the checked interface in `stdlib/<module>.jolt`; `stdlib/generate.py`
   generates the shared catalogue.
2. Implement the intrinsic in `layers/execution/src/stdlib_runtime.inc`, using
   the invocation allocator/budget and the existing typed callback machinery.
3. Add execution/parity coverage in `tests/conformance/check_phase2.py` and
   relevant runtime/ownership cases in `tests/unit/test_stdlib.c`.
4. Update `docs/stdlib.md` and preserve C/Rust emission parity. Formatter and
   documentation-generator semantic integration is Phase 3 work.

### Modifying the Type System

1. Update `compiler/frontend/typechecker.c`
2. Update all four backends' type mapping
3. Run full conformance suite: `ctest -R joltscript_conformance`
4. Get two reviewer sign-offs (compiler team gate)

---

## Testing

```bash
# Unit tests
ctest --test-dir build -R joltscript_unit

# Integration tests (all targets)
ctest --test-dir build -R joltscript_integration

# Cross-target conformance (same kernel, all 4 targets)
ctest --test-dir build -R joltscript_conformance

# Performance benchmarks
./build/joltscript/benchmarks/run_benchmarks.sh
```

**Minimum coverage** for new language features:
- Parser tests (valid + invalid syntax)
- Typechecker tests (inference + explicit + errors)
- Codegen tests (all 4 targets produce valid output)
- Runtime tests (execution matches semantics)

---

## PR Checklist

- [ ] `joltfmt` passes on all changed `.jolt` files
- [ ] All compiler unit tests pass
- [ ] Integration tests pass on all 4 targets
- [ ] Conformance tests pass (no target-specific behavior differences)
- [ ] No regressions in benchmarks (>5% slowdown requires justification)
- [ ] Documentation updated (`joltdoc` output)
- [ ] Two reviewer sign-offs for compiler changes

---

## Common Mistakes

**Implicit mutation in pure functions.** Joltscript functions are pure by default. Use `var`/`set!` explicitly for mutation.

**Forgetting arena scope.** Kernel bodies execute in an implicit arena. Don't manually manage memory inside kernels.

**Type annotation drift.** Keep annotations in sync with inference; run `joltc --check-types` in CI.

**Target divergence.** A kernel must produce bit-identical results across all 4 targets (modulo float non-determinism). Test with `joltc --verify-determinism`.

**Macro hygiene violations.** Use `gensym` for generated symbols; avoid capturing user bindings.

---

## Contacts

- **Language design**: `#joltfx-language`
- **Compiler implementation**: `#joltfx-compiler`
- **Runtime / intrinsics**: `#joltfx-runtime`
- **Tooling (LSP, formatter, etc.)**: `#joltfx-tooling`
- **Standard library**: `#joltfx-stdlib`
