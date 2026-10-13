# Joltscript Language Reference

## Overview

Joltscript is a Lisp dialect designed for authoring JoltFX kernels. It uses S-expressions exclusively and compiles to JBC1 bytecode for execution on the Joltscript VM.

**Phases 1–2 are implemented (execution ABI 0.9).** This reference covers the
legacy f32 profile (version 1), typed core/standard library (version 2), and the
separate CPU image profile. Typed execution supports loops, functions/closures,
exact primitives and immutable collections; the [six-module library](stdlib.md)
provides 262 exports. Imports select typed execution. See [examples](examples.md)
for complete programs and [tools](tools.md) for current tooling capabilities.

## Syntax

### Atoms

```lisp
42          ; f32 in version 1; i32 in typed version 2
3.14159     ; f32 in version 1; f64 in typed version 2
42i64       ; exact typed integer
true        ; typed boolean
"hello"     ; typed UTF-8 string
\u{1F600}   ; typed Unicode scalar
```

### Lists

```lisp
(function arg1 arg2 arg3)  ; function call
```

### Vectors

```lisp
(import jolt.collections)
(defkernel vector-example [] [1 2 3 4])
```

### Maps

```lisp
(import jolt.collections)
(defkernel map-example [] (hash-map "x" 100 "y" 200 "z" 300))
```

### Comments

```lisp
;; A semicolon starts a comment through the end of the line.
;; Use successive comment lines for longer notes.
```

## Special Forms

Keyword literals, quotation, brace-map syntax and block comments are future
language designs. Use string keys and collection constructors in shipped code.

### defkernel

Defines a kernel — the fundamental unit of composition in Joltscript.

```lisp
(defkernel name [input ...] expression)
(defkernel name [input ...] (rgba r g b a))
```

**Parameters:**
- `name` — Kernel name (symbol)
- `[input ...]` — Input bindings (0-32)
- `expression` — Body expression (single output)
- `(rgba r g b a)` — RGBA output form (4 outputs)

**Example:**
```lisp
(defkernel brightness [r g b a amount]
  (rgba (* r amount) (* g amount) (* b amount) a))
```

### JBC1 bindings and sequencing (execution ABI 0.6)

Scalar kernel bodies accept multiple forms, evaluated left-to-right; the final
value is the output. `(do forms...)` creates a lexical scope and returns its final
value. Bodies must be nonempty.

```lisp
(defkernel offset [x]
  (let delta 2)
  (var result x)
  (set! result (+ result delta))
  result)

(defkernel squared [x]
  (let [offset (+ x 1) result (* offset offset)]
    result))
```

`let` declares an immutable local and `var` a mutable local. `set!` requires a
mutable local. Declarations and assignments return the initialized/assigned f32.
Initializers run exactly once, eagerly, even for unused bindings; numerical errors
in intermediate forms still fail execution. A new binding is visible only after
its initializer. Vector binding lists initialize sequentially, so later bindings
can use earlier ones. Both `let` and `var` support this scoped vector syntax.

Inner `do`/vector-binding scopes may shadow outer names, including inputs.
Duplicate local names within one scope are rejected. Locals cannot escape their
scope; mutation of an outer `var` persists after an inner scope exits. Direct
`rgba` output expressions have separate local scopes. All values remain f32,
with at most 128 simultaneously active locals and 4096 instructions.

The bytecode container remains JBC1 version 1. ABI 0.6 adds `LOAD_LOCAL` (29),
`STORE_LOCAL` (30, copies the stack top) and `DROP` (31, pops one value). Validation
rejects uninitialized/out-of-range local reads, invalid stores and stack underflow.
Older runtimes reject these new opcodes; existing programs retain their bytes.
These additions apply to JBC1, distinct from the bounded CPU image profile below.

### JBC1 lazy conditionals (execution ABI 0.7)

```lisp
(defkernel safe-root [x]
  (if (>= x 0) (sqrt x) 0))

(defkernel categorize [x]
  (cond
    [(< x 0) -1]
    [(= x 0) 0]
    [else 1]))

(defkernel accumulate [x]
  (var result 0)
  (when (> x 0)
    (let delta (* x 2))
    (set! result (+ result delta)))
  (unless (= x 0)
    (set! result (+ result 1)))
  result)
```

Conditions are f32: zero (including negative zero) is false, any other finite
value is true. `if` requires both arms, and evaluates exactly one. `cond` takes
one or more `[test result]` clauses, evaluates tests in order, and evaluates only
the first matching result. Optional `[else result]` must be last. No match yields
`0`. `when` and `unless` require nonempty bodies; a skipped body yields `0`, and
an executed body yields its last value.

Tests and individual arms have private declaration scopes. Their declarations
do not escape or appear in sibling arms, but updates to an outer `var` persist.
All arms are checked at compile time, including unselected ones. Runtime numeric
errors occur only in evaluated expressions: `(if 1 7 (/ 1 0))` succeeds, while
`(select 1 7 (/ 1 0))` fails because `select` remains eager. `and` and `or` also
retain their eager semantics. The 64-level nesting budget includes successive
`cond` clauses, which lower to nested conditionals.

JBC1 version 1 adds `JUMP` (32) and `BRANCH_IF_ZERO` (33). Both use an absolute
byte-offset target at a strictly later instruction boundary, optionally the
program end. `BRANCH_IF_ZERO` consumes its condition. Validation rejects backward,
unaligned or out-of-range targets, unreachable instructions, mismatched stack
depths/output counts at joins, and reads not initialized on **every** incoming
path. There may be at most 256 pending distinct branch targets during validation.
Forward-only control flow preserves the 4096-instruction execution bound.

### Typed core language (execution ABI 0.9, container version 2)

The compiler selects version 2 when source contains primitive type annotations
or suffixes, boolean/string/character literals, imports, or loop/function forms. Sources
using only the original f32 grammar retain version 1 and its semantics. In
version 2, unsuffixed integers infer `i32`, decimal/exponent literals infer
`f64`, and unannotated input/function parameters default to `f64`. Use explicit
annotations when exact integer inputs or binary32 arithmetic are required.

Supported types: `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`,
`char`, `string`, and unit `void` (`()`). Integer and floating literals accept
their type-name suffix. Out-of-range literals are compile errors. Characters
are Unicode scalar values (`\a`, `\newline`, `\space`, `\u{1F600}`); strings
are immutable UTF-8 and support `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, and
`\u{...}` escapes. Surrogates and invalid UTF-8 are rejected.

```lisp
(defn factorial [(n i64)] -> i64
  (if (<= n 1i64) 1i64 (* n (factorial (- n 1i64)))))

(defkernel typed-example [(amount i64)] -> i64
  (var (total i64) 0i64)
  (for [i 0 10]
    (when (= i 3) (continue))
    (set! total (+ total (i64 i))))
  (while (< total amount)
    (set! total (+ total 1i64)))
  (let add-total (fn [(x i64)] -> i64 (+ total x)))
  (add-total (factorial 5i64)))
```

Bindings infer types or accept `(name type)`. `set!` preserves the binding's
type. Widening numeric assignments are implicit; narrowing requires `(type
expression)`. Arithmetic promotes to the wider integer type, or to `f32`/`f64`
when either operand is floating. Mixed signed/unsigned arithmetic requires a
signed type that covers both domains; mixing `u64` and signed integers requires
an explicit cast. Comparisons return `bool`. Equality also supports strings,
characters, booleans and unit. Logical operations accept numeric/bool values and
remain eager. Bitwise operations require integer operands.

Integer arithmetic wraps at the result type's width, including signed minimum
divided by `-1`. Division by zero and shifts outside `[0, bit-width)` fail.
Signed right shift sign-extends. Explicit integer casts truncate low bits;
float-to-integer casts truncate toward zero and reject out-of-range values.
Float results must be finite, and `f32` rounds after each operation. Character
casts reject values outside the Unicode scalar domain. Strings cast only to
strings; there is no implicit string/numeric conversion.

`for [name start end step?]` evaluates bounds and step once, left-to-right, then
iterates over an end-exclusive integer range. Step defaults to `1`; a zero step
fails. Negative steps work with signed bounds. Crossing the induction type's
boundary terminates the range instead of wrapping. The iteration binding is
immutable and private to the body. `while` tests before each iteration; `loop`
continues until `break` or budget exhaustion. `break`/`continue` have no operands,
target the nearest enclosing loop, and cannot cross a function boundary.

In the typed profile, loops and `when`/`unless` are statements returning unit.
`if` arms must have compatible types. `cond` returns the common clause type;
without a matching clause it returns that type's zero/false/empty value.
For dynamically typed `value` results, the unmatched result is unit.
Function-valued `cond` requires an `else`. Declarations in tests/arms are scoped;
mutations to enclosing variables persist. All arms are checked before execution.

`defn` forms precede or follow the single kernel at file scope. Forward calls
are resolved; recursive cycles require explicit return annotations. `fn` values
capture lexical cells, including mutable cells, and can be bound, called
directly, or returned from another function. Parameters can use concrete types,
`value` for dynamically checked data, or `function` for callbacks. Closures are
internal values and cannot be kernel inputs/outputs.
Functions without return annotations infer their body result. Calls have a
bounded depth; tail-call optimization is later compiler/runtime work.
`rgba` is permitted only as the final kernel expression, with four data lanes.
Single-result kernels can return primitives or immutable collections.

**Budgets and ABI:** sources are at most 1 MiB; version-2 programs at most 2 MiB,
4096 nodes (including the reserved zero node), and 128 syntax/evaluation levels.
Execution defaults to 100,000 node/iteration steps; `jolt_run_options_t` permits
up to 10,000,000. Each invocation owns at most 65,535 lexical cells, including
captured cells. Exhaustion returns `JOLT_ERR_BUDGET`. No outputs are published
on validation, argument, numeric, type, memory or budget failure.

`joltscript/value.h` defines exact tagged values and `jolt_vm_run_values`;
`jolt_program_run_values` is the compiled-program convenience API.
`jolt_bytecode_signature` reports input/output types (including signature-only
`JOLT_TYPE_VALUE` for dynamically checked results). Output strings and nested
collection views are VM-owned until its next successful typed run or destruction.
Inputs remain caller-owned, and aliased inputs/outputs are supported. The float
API accepts floating input signatures and converts numeric outputs to f32;
string/collection outputs require the typed API. `jolti` prints exact typed results.

Phase 2 adds the six built-in modules, import resolution, vector literals,
immutable lists/maps/sets, higher-order collection operations, bounded generators
and string/regex operations. See [stdlib.md](stdlib.md) for signatures, semantics,
additional budgets and examples. Imports select version 2; the original f32
profile remains available for existing kernels.

Version 2 keeps the magic and four leading header words, then adds node count,
token-pool size, root index and reserved zero. Each 28-byte preorder node stores
tag, first child, next sibling, token offset/length, line and column (u32 LE).
Tags are atom/list/vector/string (0/1/2/3). Index zero is reserved; root is one.
The UTF-8 token pool follows all nodes. Validation checks structure and budgets,
resolves lexical bindings and verifies types before execution. C and Rust have
independent readers/serializers and use the same portable typed verifier/VM.

Native preview and CPU backends execute version 2. Vulkan routes it through CPU
fallback (`used_gpu=false`). Object export preserves the bytecode; standalone
HTML/WAT exporters currently accept version 1 only and reject typed programs
before replacing the output artifact. Typed shader/export lowering is subsequent
backend work.

## Operations

### Arithmetic

| Operation | Description | Example |
|-----------|-------------|---------|
| `+` | Addition | `(+ x y)` |
| `-` | Subtraction | `(- x y)` |
| `*` | Multiplication | `(* x y)` |
| `/` | Division | `(/ x y)` |
| `min` | Minimum | `(min x y)` |
| `max` | Maximum | `(max x y)` |
| `abs` | Absolute value | `(abs x)` |
| `floor` | Floor | `(floor x)` |
| `pow` | Power | `(pow x y)` |
| `sqrt` | Square root | `(sqrt x)` |

### Comparison

| Operation | Description | Example |
|-----------|-------------|---------|
| `<` | Less than | `(< x y)` |
| `>` | Greater than | `(> x y)` |
| `<=` | Less than or equal | `(<= x y)` |
| `>=` | Greater than or equal | `(>= x y)` |
| `=` | Equal | `(= x y)` |
| `!=` | Not equal | `(!= x y)` |

### Logical

| Operation | Description | Example |
|-----------|-------------|---------|
| `and` | Logical AND | `(and x y)` |
| `or` | Logical OR | `(or x y)` |
| `not` | Logical NOT | `(not x)` |

### Bitwise

| Operation | Description | Example |
|-----------|-------------|---------|
| `bitwise-and` | Bitwise AND | `(bitwise-and x y)` |
| `bitwise-or` | Bitwise OR | `(bitwise-or x y)` |
| `bitwise-xor` | Bitwise XOR | `(bitwise-xor x y)` |
| `shl` | Shift left | `(shl x y)` |
| `shr` | Shift right | `(shr x y)` |

### Control

| Operation | Description | Example |
|-----------|-------------|---------|
| `select` | Eager conditional selection | `(select condition then else)` |

## Legacy f32 types

Version 1 uses **f32** (32-bit floating point) for all values. The typed profile
has distinct integer, float, bool, char, string and collection types as described
above; these legacy literal rules apply only to version 1.

### Literal Format

- **Integer:** `42`, `-17`
- **Float:** `3.14159`, `-2.5`, `1.0e10`
- **RGBA:** `(rgba r g b a)` — four f32 values

## Error Handling

### Compile-time Errors

- **Syntax error:** Malformed expressions
- **Undefined binding:** Reference to unbound variable
- **Duplicate binding:** Same name bound twice
- **Arity error:** Wrong number of arguments
- **Budget exceeded:** Too many instructions or nesting

### Runtime Errors

- **Division by zero:** `JOLT_ERR_NUMERIC`
- **Non-finite result:** `JOLT_ERR_NUMERIC`
- **Invalid bytecode:** `JOLT_ERR_BYTECODE`

## Legacy bytecode format (JBC1 version 1)

Version 2 uses the typed tree format described in the typed-language section
above; it does not use the instruction pairs or opcodes below.

```
offset  size  field
0       4     magic (0x3143424a "JBC1")
4       4     version (1)
8       4     input binding count
12      4     output count
16      8*n   instruction pairs: u32 opcode, u32 operand
```

### Opcodes

| Opcode | Name | Arity | Description |
|--------|------|-------|-------------|
| 1 | CONST | 0 | Push constant |
| 2 | INPUT | 0 | Push input binding |
| 3 | ADD | 2 | Addition |
| 4 | SUB | 2 | Subtraction |
| 5 | MUL | 2 | Multiplication |
| 6 | DIV | 2 | Division |
| 7 | MIN | 2 | Minimum |
| 8 | MAX | 2 | Maximum |
| 9 | ABS | 1 | Absolute value |
| 10 | FLOOR | 1 | Floor |
| 11 | POW | 2 | Power |
| 12 | SQRT | 1 | Square root |
| 13 | LT | 2 | Less than |
| 14 | SELECT | 3 | Conditional |
| 15 | OUTPUT | 0 | Pop and output |
| 16 | GT | 2 | Greater than |
| 17 | LE | 2 | Less than or equal |
| 18 | GE | 2 | Greater than or equal |
| 19 | EQ | 2 | Equal |
| 20 | NE | 2 | Not equal |
| 21 | AND | 2 | Logical AND |
| 22 | OR | 2 | Logical OR |
| 23 | NOT | 1 | Logical NOT |
| 24 | BITWISE_AND | 2 | Bitwise AND |
| 25 | BITWISE_OR | 2 | Bitwise OR |
| 26 | BITWISE_XOR | 2 | Bitwise XOR |
| 27 | SHL | 2 | Shift left |
| 28 | SHR | 2 | Shift right |
| 29 | LOAD_LOCAL | 0 | Push an initialized local slot |
| 30 | STORE_LOCAL | 1 | Copy stack top to a local without popping |
| 31 | DROP | 1 | Pop one value |
| 32 | JUMP | 0 | Jump to a later byte offset |
| 33 | BRANCH_IF_ZERO | 1 | Pop condition; jump if zero |

## Examples

### Simple Kernel

```lisp
(defkernel half [x] (* x 0.5))
```

### Brightness Adjustment

```lisp
(defkernel brightness [r g b a amount]
  (rgba (* r amount) (* g amount) (* b amount) a))
```

### Conditional Processing

```lisp
(defkernel threshold [r g b a t]
  (rgba (select (> r t) r 0.0)
        (select (> g t) g 0.0)
        (select (> b t) b 0.0)
        a))
```

### Complex Expression

```lisp
(defkernel complex [x y z]
  (+ (* x y) (- z x)))
```

## Legacy version-1 limits

- **Maximum inputs:** 32
- **Maximum outputs:** 4 (RGBA)
- **Maximum instructions:** 4096
- **Maximum nesting depth:** 64
- **Maximum source size:** 1 MiB
- **Maximum token size:** 63 bytes

## CPU image profile

The additive `jolt_image_compile` / `jolt_image_program_run` API supports
frame-aware Joltscript, including reusable scalar functions, sequential lexical
`let` bindings, lazy `if`, bounded `sum` loops, image sampling, named uniforms,
and read-only resource arrays. These programs run on the CPU AST interpreter;
they are not serialized as JBC1 or accepted by the existing GPU bytecode path.
The exact grammar, limits, and examples are in `kernels/README.md`.
Use `joltc --check --image-library kernels/common/image.jolt KERNEL.jolt` to
check a bundled source. Existing single-expression JBC1 compilation is unchanged.
