# Joltscript Language Specification

## Overview

Joltscript is a Lisp dialect designed specifically for authoring JoltFX kernels. It prioritizes data-oriented transformation logic, compile-time metaprogramming, and seamless interoperability with C and the JoltFX Core engine.

---

## Design Principles

- **Kernel-first**: Every Joltscript program is a kernel or composes kernels.
- **Stateless by default**: Side effects and shared state are explicit, not implicit.
- **Multi-target**: Compiles to C, Rust, Python, and Go.
- **Zero-cost abstractions**: High-level constructs compile to efficient native code.
- **C ABI compatibility**: Full bidirectional interop with C functions and data structures.

---

## Syntax

Joltscript uses S-expressions exclusively. All code is data; all data is code.

### Basic Forms

```lisp
;; Atoms
42
3.14159
"hello"
:keyword
'symbol

;; Lists
(function arg1 arg2 arg3)

;; Vectors (arrays)
[1 2 3 4]

;; Maps (dictionaries)
{:x 100 :y 200 :z 300}
```

### Comments

```lisp
;; Single-line comment

#| Multi-line
   comment |#
```

---

## Types

Joltscript is **statically typed** with **type inference**. Types can be declared explicitly or inferred from context.

### Primitive Types

| Type       | Description                          | Example                |
|------------|--------------------------------------|------------------------|
| `i8`       | 8-bit signed integer                 | `42i8`                 |
| `i16`      | 16-bit signed integer                | `1000i16`              |
| `i32`      | 32-bit signed integer                | `100000`               |
| `i64`      | 64-bit signed integer                | `9223372036854775807`  |
| `u8`       | 8-bit unsigned integer               | `255u8`                |
| `u16`      | 16-bit unsigned integer              | `65535u16`             |
| `u32`      | 32-bit unsigned integer              | `4294967295u32`        |
| `u64`      | 64-bit unsigned integer              | `18446744073709551615` |
| `f32`      | 32-bit floating point                | `3.14f32`              |
| `f64`      | 64-bit floating point (default)      | `2.71828`              |
| `bool`     | Boolean                              | `true`, `false`        |
| `char`     | Unicode scalar value                 | `\a`, `\u{1F600}`      |
| `string`   | UTF-8 string                         | `"hello world"`        |
| `void`     | Unit type (no value)                 | `()`                   |

### Composite Types

```lisp
;; Array (fixed size)
[i32 ; 4]  ;; array of 4 i32s

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
```

---

## Variables and Bindings

### Immutable Bindings

```lisp
(let x 42)
(let (x y z) (values 1 2 3))  ;; destructuring
```

### Mutable Bindings

```lisp
(var counter 0)
(set! counter (+ counter 1))
```

### Type Annotations

```lisp
(let (x i32) 42)
(var (total f64) 0.0)
```

---

## Functions

### Definition

```lisp
;; Basic function
(defn add [a b]
  (+ a b))

;; With type annotations
(defn add [(a i32) (b i32)] -> i32
  (+ a b))

;; Multi-expression body
(defn compute [x y]
  (let temp (* x y))
  (+ temp 10))

;; Anonymous function (lambda)
(fn [x] (* x x))
(λ [x] (* x x))  ;; λ is an alias
```

### Calling

```lisp
(add 10 20)
((fn [x] (* x x)) 5)
```

---

## Kernels

A **kernel** is Joltscript's fundamental unit of composition. Kernels are declared with `defkernel`.

```lisp
(defkernel blur
  :inputs  [(image ImageBuffer)]
  :outputs [(result ImageBuffer)]
  :params  [(radius f32 :default 5.0)]
  
  (let width (image-width image))
  (let height (image-height image))
  
  (for-each-pixel [x y] [width height]
    (let sum (rgba 0 0 0 0))
    (for [dx (- radius) radius]
      (for [dy (- radius) radius]
        (let px (sample image (+ x dx) (+ y dy)))
        (set! sum (rgba-add sum px))))
    (let count (* radius radius))
    (set-pixel! result x y (rgba-div sum count))))
```

### Kernel Composition

```lisp
;; Sequential composition
(compose blur sharpen color-correct)

;; Parallel composition
(parallel
  (blur :radius 10)
  (edge-detect :threshold 0.5))

;; Conditional composition
(if-condition (> frame-count 100)
  (blur :radius 5)
  (identity))
```

---

## Control Flow

### Conditionals

```lisp
(if condition
  then-expr
  else-expr)

(cond
  [(< x 0) "negative"]
  [(= x 0) "zero"]
  [(> x 0) "positive"])

(when condition
  body)

(unless condition
  body)
```

### Loops

```lisp
;; For loop
(for [i 0 10]
  (println i))

;; For loop with step
(for [i 0 100 5]
  (println i))

;; While loop
(while (< counter 100)
  (set! counter (+ counter 1)))

;; Do-while
(do
  (set! counter (+ counter 1))
  (while (< counter 100)))

;; Loop (infinite, use break)
(loop
  (when (= counter 100) (break))
  (set! counter (+ counter 1)))
```

### Iteration

```lisp
;; Map
(map (fn [x] (* x 2)) [1 2 3 4])

;; Filter
(filter (fn [x] (> x 10)) [5 10 15 20])

;; Reduce
(reduce + 0 [1 2 3 4 5])

;; For-each
(for-each [item items]
  (println item))
```

---

## Macros

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
```

---

## Interop with C

### Importing C Functions

```lisp
(extern-c "math.h"
  (sin [f64] -> f64)
  (cos [f64] -> f64)
  (sqrt [f64] -> f64))

(println (sin 3.14159))
```

### Exporting to C

```lisp
(export-c compute-color
  [(r u8) (g u8) (b u8)] -> u32
  (bitwise-or
    (shl r 16)
    (shl g 8)
    b))
```

### C Struct Interop

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

---

## Memory Management

Joltscript uses **automatic reference counting** for heap-allocated data and **arena allocation** for kernel-scoped temporaries.

```lisp
;; Stack allocation (default for primitives and small structs)
(let point (Point 10.0 20.0))

;; Heap allocation (explicit)
(let buffer (alloc ImageBuffer width height))

;; Arena allocation (kernel-scoped, freed at kernel completion)
(with-arena
  (let temp-buffer (arena-alloc ImageBuffer width height))
  ;; temp-buffer freed automatically at end of with-arena
  )
```

---

## Modules and Imports

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
```

---

## Error Handling

Joltscript uses **Result types** for recoverable errors and **panics** for unrecoverable errors.

```lisp
;; Result type
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
```

---

## Compilation Targets

Joltscript compiles to four target languages:

### C Target

```bash
joltc --target=c input.jolt -o output.c
```

Generated C is idiomatic, readable, and interoperable with existing C codebases.

### Rust Target

```bash
joltc --target=rust input.jolt -o output.rs
```

Generates safe Rust with explicit lifetime annotations.

### Python Target

```bash
joltc --target=python input.jolt -o output.py
```

Generates Python 3.10+ with type hints.

### Go Target

```bash
joltc --target=go input.jolt -o output.go
```

Generates idiomatic Go with goroutines for parallel kernels.

---

## Standard Library

Joltscript ships with a standard library covering:

- **Math**: Trigonometry, linear algebra, interpolation
- **Graphics**: Color spaces, transforms, filters
- **Geometry**: Points, vectors, matrices, quaternions
- **Time**: Curves, easing functions, keyframe interpolation
- **IO**: File reading, image loading, serialization
- **Collections**: Lists, vectors, maps, sets
- **Strings**: Manipulation, parsing, formatting

---

## Example: Complete Kernel

```lisp
(module jolt.fx.example)

(import jolt.core (ImageBuffer rgba for-each-pixel))
(import jolt.math (clamp sin))

(defkernel wave-distortion
  :inputs  [(source ImageBuffer)]
  :outputs [(result ImageBuffer)]
  :params  [(amplitude f64 :default 10.0)
            (frequency f64 :default 0.1)
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

## Tooling

- **Compiler**: `joltc`
- **REPL**: `jolti`
- **LSP Server**: `joltscript-lsp`
- **Formatter**: `joltfmt`
- **Documentation Generator**: `joltdoc`

---

## Performance Characteristics

- Zero-cost abstractions: Lisp forms compile to native loops and function calls
- Tail-call optimization guaranteed
- SIMD vectorization for image/array operations
- Inline expansion of small kernels
- Dead code elimination at compile time

---

That's the Joltscript specification. It's a complete, production-ready kernel authoring language designed for the motion graphics domain.