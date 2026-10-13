# Joltscript Examples

## Overview

These complete kernels demonstrate the implemented Phase 1–2 language and
standard library (execution ABI 0.9). Save a block as `example.jolt` and run
`joltc --check example.jolt`. Pixel examples take floating inputs and return
four numeric RGBA lanes; examples with imports use typed JBC1 version 2 and
execute on the CPU. The resource-based CPU image profile is documented separately
in [language.md](language.md).

## Basic Kernels

### Brightness Adjustment

```lisp
;; Adjusts the brightness of an image by a given factor.
;; @kernel brightness
;; @category color
;; @description Adjusts image brightness
;; @complexity Low
;; @gpu Yes
;; @since 1.0.0

(defkernel brightness [r g b a amount]
  (rgba (* r amount) (* g amount) (* b amount) a))
```

### Invert Colors

```lisp
;; Inverts the colors of an image.
;; @kernel invert
;; @category color
;; @description Inverts image colors
;; @complexity Low
;; @gpu Yes
;; @since 1.0.0

(defkernel invert [r g b a]
  (rgba (- 1.0 r) (- 1.0 g) (- 1.0 b) a))
```

### Grayscale

```lisp
;; Converts an image to grayscale.
;; @kernel grayscale
;; @category color
;; @description Converts to grayscale
;; @complexity Low
;; @gpu No
;; @since 1.0.0

(import jolt.math)
(defkernel grayscale [r g b a]
  (let gray (+ (+ (* 0.2126 r) (* 0.7152 g)) (* 0.0722 b)))
  (rgba gray gray gray a))
```

### Gamma Correction

```lisp
;; Applies gamma correction to an image.
;; @kernel gamma
;; @category color
;; @description Applies gamma correction
;; @complexity Low
;; @gpu Yes
;; @since 1.0.0

(defkernel gamma [r g b a gamma]
  (rgba (pow r gamma) (pow g gamma) (pow b gamma) a))
```

## Intermediate Kernels

### Contrast Adjustment

```lisp
;; Adjusts the contrast of an image.
;; @kernel contrast
;; @category color
;; @description Adjusts image contrast
;; @complexity Medium
;; @gpu No
;; @since 1.0.0

(import jolt.math (clamp))
(defkernel contrast [r g b a amount]
  (let factor (/ (* 259.0 (+ amount 255.0)) (* 255.0 (- 259.0 amount))))
  (rgba (clamp (* factor (- r 0.5)) 0.0 1.0)
        (clamp (* factor (- g 0.5)) 0.0 1.0)
        (clamp (* factor (- b 0.5)) 0.0 1.0)
        a))
```

### Saturation Adjustment

```lisp
;; Adjusts the saturation of an image.
;; @kernel saturation
;; @category color
;; @description Adjusts image saturation
;; @complexity Medium
;; @gpu No
;; @since 1.0.0

(import jolt.math (clamp lerp))
(defkernel saturation [r g b a amount]
  (let gray (+ (+ (* 0.2126 r) (* 0.7152 g)) (* 0.0722 b)))
  (rgba (clamp (lerp gray r amount) 0.0 1.0)
        (clamp (lerp gray g amount) 0.0 1.0)
        (clamp (lerp gray b amount) 0.0 1.0)
        a))
```

### Threshold

```lisp
;; Applies a threshold to an image.
;; @kernel threshold
;; @category color
;; @description Applies threshold
;; @complexity Medium
;; @gpu Yes
;; @since 1.0.0

(defkernel threshold [r g b a t]
  (rgba (select (> r t) r 0.0)
        (select (> g t) g 0.0)
        (select (> b t) b 0.0)
        a))
```

### Color Balance

```lisp
;; Adjusts the color balance of an image.
;; @kernel color-balance
;; @category color
;; @description Adjusts color balance
;; @complexity Medium
;; @gpu No
;; @since 1.0.0

(import jolt.math (clamp))
(defkernel color-balance [r g b a r-gain g-gain b-gain]
  (rgba (clamp (* r r-gain) 0.0 1.0)
        (clamp (* g g-gain) 0.0 1.0)
        (clamp (* b b-gain) 0.0 1.0)
        a))
```

## Advanced Kernels

### Sepia Tone

```lisp
;; Applies a sepia tone effect to an image.
;; @kernel sepia
;; @category color
;; @description Applies sepia tone
;; @complexity Medium
;; @gpu No
;; @since 1.0.0

(import jolt.math (clamp))
(defkernel sepia [r g b a]
  (let tr (+ (+ (* 0.393 r) (* 0.769 g)) (* 0.189 b)))
  (let tg (+ (+ (* 0.349 r) (* 0.686 g)) (* 0.168 b)))
  (let tb (+ (+ (* 0.272 r) (* 0.534 g)) (* 0.131 b)))
  (rgba (clamp tr 0.0 1.0) (clamp tg 0.0 1.0) (clamp tb 0.0 1.0) a))
```

### Vignette

```lisp
;; Applies a vignette effect to an image.
;; @kernel vignette
;; @category effect
;; @description Applies vignette effect
;; @complexity High
;; @gpu No
;; @since 1.0.0

(import jolt.math (clamp))
(defkernel vignette [r g b a x y width height strength]
  (let cx (/ width 2.0))
  (let cy (/ height 2.0))
  (let dx (- x cx))
  (let dy (- y cy))
  (let dist (sqrt (+ (* dx dx) (* dy dy))))
  (let max-dist (sqrt (+ (* cx cx) (* cy cy))))
  (let factor (- 1.0 (* strength (pow (/ dist max-dist) 2.0))))
  (rgba (clamp (* r factor) 0.0 1.0)
        (clamp (* g factor) 0.0 1.0)
        (clamp (* b factor) 0.0 1.0)
        a))
```

### Color Temperature

`temp` is a positive color temperature in kelvin (for example, 6500).

```lisp
;; Adjusts the color temperature of an image.
;; @kernel color-temperature
;; @category color
;; @description Adjusts color temperature
;; @complexity High
;; @gpu No
;; @since 1.0.0

(import jolt.math (clamp log))
(defkernel color-temperature [r g b a temp]
  (let t (/ temp 100.0))
  (let red (if (<= t 66.0) 255.0
            (clamp (* 329.698727446 (pow (- t 60.0) -0.1332047592)) 0.0 255.0)))
  (let green (if (<= t 66.0)
            (clamp (- (* 99.4708025861 (log t)) 161.1195681661) 0.0 255.0)
            (clamp (* 288.1221695283 (pow (- t 60.0) -0.0755148492)) 0.0 255.0)))
  (let blue (if (>= t 66.0) 255.0
            (if (<= t 19.0) 0.0
            (clamp (- (* 138.5177312231 (log (- t 10.0))) 305.0447927307) 0.0 255.0))))
  (rgba (/ red 255.0) (/ green 255.0) (/ blue 255.0) a))
```

### Blend Modes

```lisp
;; Blends two images using a specified blend mode.
;; @kernel blend
;; @category composite
;; @description Blends two images
;; @complexity Medium
;; @gpu No
;; @since 1.0.0

(import jolt.math)
(defkernel blend [src-r src-g src-b src-a dst-r dst-g dst-b dst-a mode]
  (let blended-r (select (= mode 0) src-r
                  (select (= mode 1) (* src-r dst-r)
                  (select (= mode 2) (- 1.0 (* (- 1.0 src-r) (- 1.0 dst-r)))
                  (select (= mode 3) (min src-r dst-r)
                  (select (= mode 4) (max src-r dst-r)
                            (abs (- src-r dst-r))))))))
  (let blended-g (select (= mode 0) src-g
                  (select (= mode 1) (* src-g dst-g)
                  (select (= mode 2) (- 1.0 (* (- 1.0 src-g) (- 1.0 dst-g)))
                  (select (= mode 3) (min src-g dst-g)
                  (select (= mode 4) (max src-g dst-g)
                            (abs (- src-g dst-g))))))))
  (let blended-b (select (= mode 0) src-b
                  (select (= mode 1) (* src-b dst-b)
                  (select (= mode 2) (- 1.0 (* (- 1.0 src-b) (- 1.0 dst-b)))
                  (select (= mode 3) (min src-b dst-b)
                  (select (= mode 4) (max src-b dst-b)
                            (abs (- src-b dst-b))))))))
  (rgba blended-r blended-g blended-b src-a))
```

## Pipeline Examples

### Brightness + Contrast

```lisp
;; Pipeline: brightness followed by contrast
(import jolt.math (clamp))
(defkernel brightness-contrast [r g b a brightness contrast]
  (let bright-r (* r brightness))
  (let bright-g (* g brightness))
  (let bright-b (* b brightness))
  (let factor (/ (* 259.0 (+ contrast 255.0)) (* 255.0 (- 259.0 contrast))))
  (rgba (clamp (* factor (- bright-r 0.5)) 0.0 1.0)
        (clamp (* factor (- bright-g 0.5)) 0.0 1.0)
        (clamp (* factor (- bright-b 0.5)) 0.0 1.0)
        a))
```

### Gamma + Saturation

```lisp
;; Pipeline: gamma correction followed by saturation
(import jolt.math (clamp lerp))
(defkernel gamma-saturation [r g b a gamma saturation]
  (let gamma-r (pow r gamma))
  (let gamma-g (pow g gamma))
  (let gamma-b (pow b gamma))
  (let gray (+ (+ (* 0.2126 gamma-r) (* 0.7152 gamma-g)) (* 0.0722 gamma-b)))
  (rgba (clamp (lerp gray gamma-r saturation) 0.0 1.0)
        (clamp (lerp gray gamma-g saturation) 0.0 1.0)
        (clamp (lerp gray gamma-b saturation) 0.0 1.0)
        a))
```

## Typed values, functions and collections

The evaluator accepts a complete kernel. For example,
`jolti -e '(defkernel exact [] 18446744073709551615u64)'` preserves every digit.
This program uses a typed function, loop and immutable vector:

```lisp
(import jolt.collections (map))
(defn sum-to [(n i32)] -> i32
  (var total 0)
  (for [i 0 n] (set! total (+ total i)))
  total)
(defkernel totals [] (map sum-to [3 4 5]))
;; [3 6 10]
```

Use `jolti` or the tagged-value API for collection output. See [stdlib.md](stdlib.md)
for all six modules, callback signatures, strings/regex and execution budgets.

## Tips

### Performance

- Use lazy `if`/`cond` when unselected branches should not execute; `select` is eager
- Minimize the number of operations in a kernel
- Use constants instead of repeated literals

### Readability

- Use descriptive binding names
- Add comments for complex expressions
- Break complex kernels into smaller functions

### Debugging

- Use `joltc --check` to validate syntax
- Use `joltc --dump` to inspect bytecode
- Use `jolti` to evaluate complete kernels interactively
