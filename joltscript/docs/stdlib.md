# Joltscript Standard Library

## Overview

The Joltscript standard library provides common operations for kernel authoring. All functions are implemented as Joltscript intrinsics or pure Joltscript.

## Status: not loadable by the current implementation

**The six modules in `joltscript/stdlib/` do not compile, and that is expected.** They
are specification artefacts for the full language described in `joltscript/AGENTS.md`;
the only Joltscript implementation in the tree is the bounded CPU image profile, whose
dialect is deliberately narrower. Nothing in the build, the tests or the tools loads
these files, so this does not affect any shipping kernel.

The gap is **syntactic, not a set of missing builtins**, which matters because it cannot
be closed by adding functions:

| construct | these modules write | image profile accepts |
|---|---|---|
| `let` | `(let name value)` | `(let [name value ...] body)` |
| function body | several forms (implicit `do`) | exactly one form |
| values | vectors `[h s v]`, 4-tuples | scalars only (`f64`) |
| top level | `module`, `import`, `export`, `defconst` | `param`, `defn`, `defkernel`, `passes` |

`graphics.jolt` is the closest to loadable — no collections, no FFI, no mutation — and
it still fails on its first `let`. All six modules need collections; `strings.jolt`
additionally needs a string value type; and `math.jolt` needs **23 `extern-c` FFI
calls**, which the profile forbids outright. It guarantees that programs have "no
filesystem, FFI, imports, allocation, or system resources"
(`joltscript/layers/glue/include/joltscript/image_program.h`). Supporting these modules
therefore means implementing a second, general-purpose language with values, mutation
and FFI, or weakening that sandbox guarantee.

For the language that *does* compile today, see [language.md](language.md); the shared
kernel helpers that are actually in use are `kernels/common/*.jolt`, which are prepended
to each kernel as a library by `kernels/CMakeLists.txt`.

Each module carries the same `STATUS:` banner in its header. Do not silence a compile
error here by rewriting call sites — kernels written against the bounded profile would
stop compiling.

## Modules

### jolt.math

Mathematical functions for trigonometry, linear algebra, interpolation, and random numbers.

#### Trigonometry
- `(sin x)` — Sine
- `(cos x)` — Cosine
- `(tan x)` — Tangent
- `(asin x)` — Arcsine
- `(acos x)` — Arccosine
- `(atan x)` — Arctangent
- `(atan2 y x)` — Arctangent (2-arg)

#### Exponential and Logarithmic
- `(exp x)` — Exponential
- `(log x)` — Natural logarithm
- `(log10 x)` — Base-10 logarithm
- `(log2 x)` — Base-2 logarithm
- `(exp2 x)` — Base-2 exponential

#### Power and Root
- `(pow x y)` — Power
- `(sqrt x)` — Square root
- `(cbrt x)` — Cube root
- `(hypot x y)` — Hypotenuse

#### Rounding
- `(floor x)` — Floor
- `(ceil x)` — Ceiling
- `(round x)` — Round
- `(trunc x)` — Truncate

#### Other
- `(abs x)` — Absolute value
- `(sign x)` — Sign
- `(min x y)` — Minimum
- `(max x y)` — Maximum
- `(clamp x lo hi)` — Clamp to range
- `(lerp a b t)` — Linear interpolation
- `(smoothstep edge0 edge1 x)` — Smoothstep interpolation
- `(map-range x in-min in-max out-min out-max)` — Map value between ranges
- `(deg->rad deg)` — Degrees to radians
- `(rad->deg rad)` — Radians to degrees

#### Constants
- `PI` — π
- `TWO-PI` — 2π
- `HALF-PI` — π/2
- `E` — Euler's number
- `SQRT2` — √2
- `SQRT1_2` — 1/√2

#### Vector Operations
- `(dot v1 v2)` — Dot product
- `(length v)` — Vector length
- `(normalize v)` — Normalize vector
- `(cross v1 v2)` — Cross product

#### Matrix Operations
- `(mat2-mul m1 m2)` — 2x2 matrix multiplication
- `(mat2-det m)` — 2x2 matrix determinant

#### Easing Functions
- `(ease-in-quad t)` — Ease in (quadratic)
- `(ease-out-quad t)` — Ease out (quadratic)
- `(ease-in-out-quad t)` — Ease in-out (quadratic)
- `(ease-in-cubic t)` — Ease in (cubic)
- `(ease-out-cubic t)` — Ease out (cubic)
- `(ease-in-out-cubic t)` — Ease in-out (cubic)

### jolt.graphics

Graphics and color operations.

#### Color Space Conversions
- `(rgb->hsv r g b)` — RGB to HSV
- `(hsv->rgb h s v)` — HSV to RGB
- `(rgb->hsl r g b)` — RGB to HSL
- `(hsl->rgb h s l)` — HSL to RGB

#### Color Adjustments
- `(brightness r g b amount)` — Adjust brightness
- `(contrast r g b amount)` — Adjust contrast
- `(saturation r g b amount)` — Adjust saturation
- `(invert r g b a)` — Invert colors
- `(grayscale r g b)` — Convert to grayscale
- `(sepia r g b)` — Apply sepia tone

#### Blend Modes
- `(blend-normal src dst)` — Normal blend
- `(blend-multiply src dst)` — Multiply blend
- `(blend-screen src dst)` — Screen blend
- `(blend-overlay src dst)` — Overlay blend
- `(blend-darken src dst)` — Darken blend
- `(blend-lighten src dst)` — Lighten blend
- `(blend-difference src dst)` — Difference blend

#### Alpha Compositing
- `(alpha-composite src-r src-g src-b src-a dst-r dst-g dst-b dst-a)` — Alpha composite

#### Color Temperature
- `(color-temperature r g b temp)` — Adjust color temperature

#### Gamma Correction
- `(gamma-correct r g b gamma)` — Apply gamma correction
- `(gamma-decode r g b)` — Decode gamma (sRGB to linear)
- `(gamma-encode r g b)` — Encode gamma (linear to sRGB)

### jolt.geometry

Geometric operations for points, vectors, matrices, quaternions, and splines.

#### 2D Points
- `(point2d x y)` — Create 2D point
- `(point2d-x p)` — Get X coordinate
- `(point2d-y p)` — Get Y coordinate
- `(point2d-add p1 p2)` — Add points
- `(point2d-sub p1 p2)` — Subtract points
- `(point2d-scale p s)` — Scale point
- `(point2d-dist p1 p2)` — Distance between points
- `(point2d-lerp p1 p2 t)` — Interpolate between points

#### 3D Points
- `(point3d x y z)` — Create 3D point
- `(point3d-x p)` — Get X coordinate
- `(point3d-y p)` — Get Y coordinate
- `(point3d-z p)` — Get Z coordinate
- `(point3d-add p1 p2)` — Add points
- `(point3d-sub p1 p2)` — Subtract points
- `(point3d-scale p s)` — Scale point
- `(point3d-dist p1 p2)` — Distance between points
- `(point3d-lerp p1 p2 t)` — Interpolate between points

#### Vector Operations
- `(vec2 x y)` — Create 2D vector
- `(vec3 x y z)` — Create 3D vector
- `(vec4 x y z w)` — Create 4D vector
- `(vec-dot v1 v2)` — Dot product
- `(vec-length v)` — Vector length
- `(vec-normalize v)` — Normalize vector
- `(vec-cross v1 v2)` — Cross product
- `(vec-lerp v1 v2 t)` — Interpolate between vectors

#### 2D Transformations
- `(translate2d p tx ty)` — Translate point
- `(scale2d p sx sy)` — Scale point
- `(rotate2d p angle)` — Rotate point

#### 3D Transformations
- `(translate3d p tx ty tz)` — Translate point
- `(scale3d p sx sy sz)` — Scale point
- `(rotate3d-x p angle)` — Rotate around X axis
- `(rotate3d-y p angle)` — Rotate around Y axis
- `(rotate3d-z p angle)` — Rotate around Z axis

#### Quaternion Operations
- `(quat x y z w)` — Create quaternion
- `(quat-identity)` — Identity quaternion
- `(quat-mul q1 q2)` — Multiply quaternions
- `(quat-normalize q)` — Normalize quaternion
- `(quat-from-axis-angle axis angle)` — Create from axis-angle
- `(quat-rotate q v)` — Rotate vector by quaternion

#### Spline Operations
- `(catmull-rom-spline p0 p1 p2 p3 t)` — Catmull-Rom spline
- `(bezier-spline p0 p1 p2 p3 t)` — Bezier spline

#### Bounding Boxes
- `(bbox2d points)` — 2D bounding box
- `(bbox3d points)` — 3D bounding box
- `(point-in-bbox2d p bbox)` — Point in 2D bbox test
- `(point-in-bbox3d p bbox)` — Point in 3D bbox test

### jolt.time

Time and animation functions.

#### Time Conversion
- `(fps->frame-time fps)` — FPS to frame time
- `(frame-time->fps frame-time)` — Frame time to FPS
- `(seconds->frames seconds fps)` — Seconds to frames
- `(frames->seconds frames fps)` — Frames to seconds

#### Keyframe Interpolation
- `(keyframe-value keyframes time)` — Interpolate keyframe value

#### Easing Functions
- `(ease-linear t)` — Linear
- `(ease-in-quad t)` — Ease in (quadratic)
- `(ease-out-quad t)` — Ease out (quadratic)
- `(ease-in-out-quad t)` — Ease in-out (quadratic)
- `(ease-in-cubic t)` — Ease in (cubic)
- `(ease-out-cubic t)` — Ease out (cubic)
- `(ease-in-out-cubic t)` — Ease in-out (cubic)
- `(ease-in-quart t)` — Ease in (quartic)
- `(ease-out-quart t)` — Ease out (quartic)
- `(ease-in-out-quart t)` — Ease in-out (quartic)
- `(ease-in-quint t)` — Ease in (quintic)
- `(ease-out-quint t)` — Ease out (quintic)
- `(ease-in-out-quint t)` — Ease in-out (quintic)
- `(ease-in-sine t)` — Ease in (sine)
- `(ease-out-sine t)` — Ease out (sine)
- `(ease-in-out-sine t)` — Ease in-out (sine)
- `(ease-in-expo t)` — Ease in (exponential)
- `(ease-out-expo t)` — Ease out (exponential)
- `(ease-in-out-expo t)` — Ease in-out (exponential)
- `(ease-in-circ t)` — Ease in (circular)
- `(ease-out-circ t)` — Ease out (circular)
- `(ease-in-out-circ t)` — Ease in-out (circular)
- `(ease-in-elastic t)` — Ease in (elastic)
- `(ease-out-elastic t)` — Ease out (elastic)
- `(ease-in-out-elastic t)` — Ease in-out (elastic)
- `(ease-in-back t)` — Ease in (back)
- `(ease-out-back t)` — Ease out (back)
- `(ease-in-out-back t)` — Ease in-out (back)
- `(ease-in-bounce t)` — Ease in (bounce)
- `(ease-out-bounce t)` — Ease out (bounce)
- `(ease-in-out-bounce t)` — Ease in-out (bounce)

#### Animation Curves
- `(linear-curve t)` — Linear curve
- `(bezier-curve t p0 p1 p2 p3)` — Bezier curve
- `(catmull-rom-curve t p0 p1 p2 p3)` — Catmull-Rom curve

#### Time-based Animation
- `(animate duration time easing-fn)` — Animate with easing
- `(ping-pong t)` — Ping-pong time
- `(loop-time t duration)` — Loop time
- `(repeat-count t duration)` — Repeat count

### jolt.collections

Collection operations for lists, vectors, maps, sets, and sequences.

#### List Operations
- `(list & args)` — Create list
- `(empty? coll)` — Check if empty
- `(not-empty? coll)` — Check if not empty
- `(first coll)` — First element
- `(rest coll)` — Rest of list
- `(last coll)` — Last element
- `(butlast coll)` — All but last
- `(take n coll)` — Take first n
- `(drop n coll)` — Drop first n
- `(take-while pred coll)` — Take while predicate
- `(drop-while pred coll)` — Drop while predicate
- `(partition n coll)` — Partition into groups
- `(partition-all n coll)` — Partition all
- `(interpose sep coll)` — Interpose separator
- `(interleave & colls)` — Interleave collections
- `(zipmap keys vals)` — Zip keys and values
- `(frequencies coll)` — Count frequencies
- `(group-by key-fn coll)` — Group by key function

#### Vector Operations
- `(vector & args)` — Create vector
- `(vec-get v i)` — Get element
- `(vec-set v i x)` — Set element
- `(vec-push v x)` — Push element
- `(vec-pop v)` — Pop element
- `(vec-peek v)` — Peek element

#### Map Operations
- `(map-get m k)` — Get value
- `(map-get-in m ks)` — Get nested value
- `(map-keys m)` — Get keys
- `(map-vals m)` — Get values
- `(map-merge & maps)` — Merge maps
- `(map-invert m)` — Invert map

#### Set Operations
- `(set-union s1 s2)` — Union
- `(set-intersection s1 s2)` — Intersection
- `(set-difference s1 s2)` — Difference

#### Sequence Operations
- `(lazy-seq f)` — Create lazy sequence
- `(iterate f x)` — Iterate function
- `(repeatedly f)` — Repeatedly call function
- `(cycle coll)` — Cycle collection
- `(range-seq start end step)` — Range sequence

#### Higher-order Functions
- `(map-indexed f coll)` — Map with index
- `(keep f coll)` — Keep elements
- `(remove pred coll)` — Remove elements
- `(reduce-kv f init m)` — Reduce key-value
- `(apply-kv f m)` — Apply to key-value

### jolt.strings

String manipulation, parsing, formatting, and regex.

#### String Manipulation
- `(str-length s)` — String length
- `(str-empty? s)` — Check if empty
- `(str-not-empty? s)` — Check if not empty
- `(str-trim s)` — Trim whitespace
- `(str-trim-left s)` — Trim left
- `(str-trim-right s)` — Trim right
- `(str-upper s)` — Uppercase
- `(str-lower s)` — Lowercase
- `(str-capitalize s)` — Capitalize
- `(str-replace s old new)` — Replace
- `(str-replace-all s old new)` — Replace all
- `(str-split s sep)` — Split
- `(str-join sep strs)` — Join
- `(str-concat & strs)` — Concatenate

#### String Predicates
- `(str-contains? s substr)` — Contains
- `(str-starts-with? s prefix)` — Starts with
- `(str-ends-with? s suffix)` — Ends with

#### String Parsing
- `(parse-int s)` — Parse integer
- `(parse-float s)` — Parse float
- `(parse-bool s)` — Parse boolean

#### String Formatting
- `(format fmt & args)` — Format string
- `(format-int n)` — Format integer
- `(format-float n precision)` — Format float

#### String Slicing
- `(char-at s i)` — Character at index
- `(substring s start end)` — Substring
- `(str-reverse s)` — Reverse
- `(str-repeat s n)` — Repeat
- `(str-pad-left s len char)` — Pad left
- `(str-pad-right s len char)` — Pad right

#### String Comparison
- `(str-compare s1 s2)` — Compare
- `(str-equals? s1 s2)` — Equals
- `(str-less? s1 s2)` — Less than
- `(str-greater? s1 s2)` — Greater than
