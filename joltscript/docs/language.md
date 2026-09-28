# Joltscript Language Reference

## Overview

Joltscript is a Lisp dialect designed for authoring JoltFX kernels. It uses S-expressions exclusively and compiles to JBC1 bytecode for execution on the Joltscript VM.

## Syntax

### Atoms

```lisp
42          ; integer
3.14159     ; float
"hello"     ; string
:keyword    ; keyword
'symbol     ; quoted symbol
```

### Lists

```lisp
(function arg1 arg2 arg3)  ; function call
```

### Vectors

```lisp
[1 2 3 4]   ; vector literal
```

### Maps

```lisp
{:x 100 :y 200 :z 300}  ; map literal
```

### Comments

```lisp
;; Single-line comment

#| Multi-line
   comment |#
```

## Special Forms

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
| `select` | Conditional | `(select condition then else)` |

## Types

Joltscript uses **f32** (32-bit floating point) as its primary numeric type. All numeric literals and operations use f32.

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

## Bytecode Format (JBC1)

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

## Limits

- **Maximum inputs:** 32
- **Maximum outputs:** 4 (RGBA)
- **Maximum instructions:** 4096
- **Maximum nesting depth:** 64
- **Maximum source size:** 1 MiB
- **Maximum token size:** 63 bytes
