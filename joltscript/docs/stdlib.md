# Joltscript Standard Library

The six modules in `joltscript/stdlib/` are executable intrinsic interfaces for
the typed JBC1 version-2 runtime (execution ABI **0.9**). C and Zoltan compile
them independently into identical bytecode. The build generates the runtime
catalogue from these declarations; imports resolve against that embedded
catalogue, with no runtime filesystem lookup or FFI access.

| Module | Contents |
|---|---|
| [`jolt.math`](../stdlib/math.jolt) | Trigonometry, exponentials, rounding, interpolation, deterministic random, linear algebra |
| [`jolt.graphics`](../stdlib/graphics.jolt) | RGB/HSV/HSL and gamma transforms, color adjustments, blending, compositing, sample filters |
| [`jolt.geometry`](../stdlib/geometry.jolt) | Points, vectors, transforms, matrices, quaternions, splines, bounding boxes |
| [`jolt.time`](../stdlib/time.jolt) | Frame/time conversion, easing, scalar curves, keyframe interpolation, looping |
| [`jolt.collections`](../stdlib/collections.jolt) | Immutable lists, vectors, maps, sets, higher-order operations, bounded sequence generators |
| [`jolt.strings`](../stdlib/strings.jolt) | UTF-8 manipulation, parsing, formatting, PCRE2 regular expressions |

The linked `.jolt` files are the complete signature reference (262 exports).
`number` accepts any numeric primitive; `sequence` accepts a list, vector or set;
`value` accepts any value. `?` introduces optional parameters, and `&` a repeated
parameter, up to the language's 32-argument limit. These two signature categories
(`number`, `sequence`) are private to intrinsic interfaces. Use concrete types
or `value` in user functions.

## Imports and execution

```lisp
(import jolt.math (sin HALF-PI))
(import jolt.collections (map reduce))

(defkernel example []
  (let samples (map (fn [x] (* x x)) [1 2 3]))
  (+ (reduce + 0.0 samples) (sin HALF-PI)))
;; 15
```

`(import jolt.math)` imports every export. Selective imports restrict the visible
names. Imported names are also available fully qualified, such as
`jolt.math/length`. If two imports expose the same unqualified name, qualify it
or select only the intended export. Lexical function bindings shadow imports.
Unknown modules, unknown exports, wrong arity and statically incompatible
arguments are source-located compile errors.

The six library files contain `(module jolt.name)`, checked `(intrinsic ... )`
declarations and `(export *)`. They compile as zero-input/zero-output library
artifacts, which cannot be invoked as kernels. This is the built-in module
interface mechanism; arbitrary user module files, import aliases and re-exports
are not implemented.

```sh
joltc joltscript/stdlib/math.jolt --check
zoltan compile joltscript/stdlib/math.jolt -o math.jbc
jolti -e '(import jolt.math) (defkernel k [] (sin HALF-PI))'
```

Imports select version 2. Native execution and Zoltan CPU preview support the
library; Vulkan uses its typed CPU fallback. Object exports preserve the typed
bytecode. Standalone HTML/WAT exporters still reject version 2. The bounded CPU
image profile has its own operations and does not load these modules.

## Math

Numeric library operations return finite `f64` values. Imported `min`, `max`,
`abs`, `floor`, `pow` and `sqrt` use these library signatures; unimported core
arithmetic retains its typed integer/float rules. Numeric domain failures return
`JOLT_ERR_NUMERIC`; invalid bounds/dimensions return `JOLT_ERR_ARGUMENT`.

- Angles are radians; `deg->rad` and `rad->deg` convert units.
- `mod` is nonnegative modulo `abs(divisor)`.
- `clamp(value, minimum, maximum)` requires ordered bounds.
- `lerp(a, b, t)` is `a + t*(b-a)`.
- `smoothstep(edge0, edge1, x)` clamps the normalized coordinate and applies
  `t*t*(3-2*t)`; edges must be strictly increasing.
- `map-range(x, in0, in1, out0, out1)` maps without clamping.
- Constants are `PI`, `TWO-PI`, `HALF-PI`, `E`, `SQRT2`, `SQRT1_2`.
- `(random-seed 42u32)` creates an invocation-local zero-argument generator.
  Each call advances `state = state*1664525 + 1013904223` modulo 2^32 and returns
  `state / 2^32` in `[0,1)`. There is no global random state.
- `dot`, Euclidean `length`, `normalize`, `cross`, `mat2-mul` and `mat2-det`
  share the geometry conventions below.

## Geometry and graphics

Geometry operates on numeric vectors, with exact dimension checks for indexed
accesses and matching dimensions for pairwise operations. General vector
operations accept 1–64 components. Matrices are flat row-major arrays;
`mat-mul(a,b,n)` and `mat-transpose(a,n)` support square dimensions 1–8.
`mat2-mul` uses ordinary row/column multiplication, not component-wise products.
Zero-vector normalization returns the zero vector.

Points have 2 or 3 components. Transform angles use radians. Quaternions use
`[x y z w]`, with identity `[0 0 0 1]`; axis-angle construction normalizes the
axis, and quaternion rotation accounts for quaternion magnitude. A zero axis
produces identity; a zero rotation quaternion is invalid. Splines take four
point vectors followed by `t`. Bounds are `[min-x min-y max-x max-y]` or
`[min-x min-y min-z max-x max-y max-z]`; bound tests include the boundary.
Bounding-box construction accepts 1–64 points.

Colors use RGB components in `[0,1]` and hue in turns. Color-space conversions
return three components. Color adjustments return `[r g b 1]`, except `invert`
preserves its fourth alpha argument and `color-temperature` returns RGB.
Brightness multiplies by its factor; contrast scales around 0.5; saturation
interpolates from Rec.709 luminance. These three adjustments and sepia clamp
their output to `[0,1]`. Gamma decode/encode use the piecewise sRGB transfer
functions; `gamma-correct` raises each component to the supplied exponent.
Color temperature is in Kelvin, bounded to 1000–40000.

Blend functions take `(source, destination)` scalars. Overlay chooses its branch
from the destination. `alpha-composite(sr,sg,sb,sa,dr,dg,db,da)` implements
straight-alpha source-over and returns RGBA; fully transparent output has zero
RGB. `convolve(samples,weights)` returns the normalized weighted sum, `box-filter`
returns the sample mean, and `gaussian-weight(distance,sigma)` returns a normalized
one-dimensional Gaussian weight. Empty filters, mismatched weights, zero total
weight and nonpositive sigma fail.

## Time and animation

Easing functions cover linear, quadratic, cubic, quartic, quintic, sine,
exponential, circular, elastic, back and bounce curves. The domain is `[0,1]`;
elastic/back easing may overshoot. In/out/in-out variants preserve endpoints.
Scalar Bézier and Catmull–Rom curves take `(t,p0,p1,p2,p3)`; geometric splines
take the four vectors before `t`.

`keyframe-value(keyframes,time)` accepts unsorted `[time value]` pairs, linearly
interpolates adjacent times, and clamps before/after the range. Empty input
returns zero. At an exact duplicate timestamp, the last matching pair wins.
`animate(duration,time,easing)` invokes its callback with clamped `time/duration`.
`ping-pong` has period two; `loop-time(time,duration)` wraps negative time;
`repeat-count` returns the floored cycle count. Durations must be positive.

## Collections and higher-order functions

`[a b c]` creates a heterogeneous immutable vector. `list`, `vector`, `hash-map`
and `set` construct the other collection values. Maps store alternating keys and
values; repeated keys replace their value, preserving insertion order. Sets
deduplicate values and retain insertion order. Collection keys/membership use
type-sensitive structural equality; map/set equality ignores insertion order.
`length` counts entries, not both halves of a map pair.

Collection transformations allocate new values. `vec-set`, `vec-push`, `assoc`
and `dissoc` never mutate their argument. Indices are zero-based, slice ends
exclusive; invalid indices fail. `get`/`map-get` return unit on a missing key,
or their optional default. `map-get-in` returns unit for a missing/non-map path.
`take`/`drop` clamp to collection size. `partition` drops an incomplete final
group; `partition-all` includes it. `interleave` includes remaining elements of
longer inputs. `sort` is stable and accepts a strict-less-than callback.

```lisp
(import jolt.collections)
(defkernel example []
  (let square (fn [(x value)] (* x x)))
  (let xs (map square [1 2 3]))
  (reduce + 0 xs))
;; exact i32 result: 14
```

Unannotated callback parameters still default to `f64`. Annotate `(x value)`
for heterogeneous/exact values, or use concrete types. `function` annotations
accept callbacks. Dynamic values are checked when consumed or called. Core
operators and imported functions can be passed as callback values.

`map` accepts multiple sequences and stops at the shortest. `map-indexed` passes
`(index,item)`, with an `i64` index. `filter`/`remove`, `some`/`every?`, and
take/drop-while accept numeric/bool predicates (unit is false). `keep` retains
non-unit callback results, including false and zero. `reduce-kv` passes
`(accumulator,key,value)`; `apply-kv` spreads alternating keys/values as arguments.

Sequence generators are stateful zero-argument closures local to one invocation:

- `range-seq(start,end,step=1)`: exact `i64`, end-exclusive, signed nonzero step;
  reaching integer bounds terminates instead of wrapping.
- `iterate(f,initial)`: yields initial, then repeated applications of `f`.
- `repeatedly(f)`: invokes a zero-argument callback per item.
- `cycle(sequence)`: repeats a finite sequence; empty input terminates.
- `lazy-seq(f)`: invokes `f(state)`, initially unit; callback returns
  `[item,next-state]` or unit to terminate.

Unit terminates generators. `take(n,generator)` materializes at most `n` items;
`drop(n,generator)` consumes items and returns that same generator. Generator
state is shared by its aliases. Higher-order `map`/`filter`/`reduce` consume finite
collections; use `take` to materialize a generator first.

## Strings and regular expressions

Strings are immutable validated UTF-8. `str-length`, `char-at`, `substring`,
reverse and padding operate on Unicode scalar values, not bytes or grapheme
clusters. Trimming recognizes Unicode whitespace. Case conversion is ASCII-only
and leaves other codepoints unchanged. Comparisons use lexicographic UTF-8 order.

Replacement searches the original text, so replacement text is never searched
again. Split/replacement separators must be nonempty. Split preserves empty
fields, including trailing fields. Padding requires a single Unicode scalar.

`parse-int` parses a complete signed decimal `i64` with overflow checking.
`parse-float` requires a complete finite decimal/exponent form; neither accepts
whitespace. `parse-bool` accepts case-insensitive `true/t/yes/1` and
`false/f/no/0`; other values fail. `format-int` preserves all signed/unsigned
64-bit digits; `format-float(value,precision)` accepts precision 0–17.
`format(template,args...)` supports `{}`, indexed `{0}`, and escaped `{{`/`}}`;
primitive arguments only, with malformed/missing placeholders rejected.

Regex functions take `(pattern,subject[,replacement])`. They use vendored PCRE2
with UTF and Unicode-property support, no JIT. `regex-match?` searches anywhere;
use anchors for full matching. `regex-find` returns the first matched substring
or unit. `regex-replace` replaces all matches and supports PCRE2 extended
replacement syntax (`$1`, `${name}`, escapes). Invalid patterns are errors.

## Budgets, ownership and builds

All operations share the VM's step budget, including higher-order callbacks and
PCRE2 match callouts. Each collection has at most 4096 stored elements (2048 map
pairs); nested data is bounded to 64 levels at the API boundary. An invocation
has at most 16 MiB of temporary arena allocations. Constructed strings are
bounded to 1 MiB; regex patterns to 1024 bytes, match depth to 128 and match heap
to 1 MiB. Expensive operations can exhaust the default 100,000 steps; the typed
run API allows up to 10,000,000. Geometry and regex have additional bounds above.

The public tagged-value API adds list/vector/map/set views without changing the
value struct layout. `JOLT_TYPE_VALUE` is signature-only; actual data carries a
concrete tag. Aggregate output storage, including nested strings, is VM-owned
until the next successful typed invocation or destruction. Failed runs preserve
both previous storage and caller output bytes. Input/output aliasing is supported.
Function/generator values cannot cross the public data boundary. Numeric preview
rejects aggregate/string outputs; `jolti` recursively prints collection results.

CMake and Zoltan build the catalogue using Python 3 and build the vendored PCRE2
archive using CMake and a C compiler. Zoltan remains offline/no-registry-dependency;
its build additionally needs an archiver. `CC`, `AR`, `PYTHON` and
`CMAKE_TOOLCHAIN_FILE` support toolchain configuration (cross builds must configure
both the bridge compiler and the PCRE2 toolchain). Library sources install under
`share/joltfx/stdlib`; compiled runtimes embed their interfaces.

## Verification

```sh
ctest --test-dir build -R 'test_(stdlib|typed)' --output-on-failure
cargo test --manifest-path zoltan/Cargo.toml --all-features
python3 joltscript/tests/conformance/check_phase2.py \
  --joltc build/joltscript/tools/joltc/joltc \
  --jolti build/joltscript/tools/jolti/jolti \
  --zoltan zoltan/target/debug/zoltan
scripts/check-bytecode-parity.sh
```

The export-coverage gate requires an execution/parity case for every declared
intrinsic. Native tests additionally cover aggregate ownership, aliasing,
failure-atomic publication, invalid dimensions/types, callback errors, regex
limits and budget exhaustion at successive execution boundaries.
