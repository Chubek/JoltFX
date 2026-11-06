# `std::animation2d` — implemented 2D animation library

Import with `import std::animation2d as a;`. The module is embedded in the toolchain
and resolved by `module_system.Loader` / `CompileFile`; project files cannot
override `std::` modules. Animation authoring resolves this module without host
filesystem access. Source-only `ingagi.Compile` retains its import-free contract.
The same operations are available as `anim_*` VM builtins without an import.

All operations are CPU/IngagiVM operations. Arguments are checked at runtime;
builtin names also have gradual semantic signatures. Variable-size array/map
wrapper arguments use the gradual `any` annotation. Returned paths, shapes and
layers own their data; modifiers never mutate their input.

| Operation | Arguments and result |
|---|---|
| `rgba` | `(r,g,b,a)` in 0..1 → packed RGBA integer, round to nearest byte |
| `ease` | `(name,t)` → normalized easing value; time clamps to 0..1 |
| `tween` | `(from,to,t,easing)` → interpolated scalar |
| `track` | `(times,values,time,easing)` → sampled scalar; sorted unique times, endpoint holds |
| `loop` | `(time,duration)` → repeating phase in [0,1); negative time wraps |
| `pingpong` | `(time,duration)` → alternating 0..1..0 phase; duration is one leg |
| `bezier` | `(controls,t)` → vec2; 2–4 controls (linear, quadratic, cubic) |
| `curve` | `(controls,steps)` → sampled Bézier path; steps 1..2047 |
| `length` | `(points)` → polyline arc length |
| `along` | `(points,t)` → arc-length-relative point; zero-length paths return endpoint |
| `morph` | `(fromPoints,toPoints,t)` → pointwise interpolation, equal counts required |
| `transform` | `(points,[a,b,c,d,e,f])` → affine path, x'=ax+cy+e, y'=bx+dy+f |
| `translate` | `(points,offsetVec2)` → translated path |
| `rotate` | `(points,radians,pivotVec2)` → rotated path |
| `scale` | `(points,scaleVec2,pivotVec2)` → scaled/reflected path |
| `arc` | `(center,radiiVec2,startRadians,endRadians,steps)` → elliptical arc, 2..1023 steps |
| `star` | `(center,outerRadius,innerRadius,tips,angle)` → alternating-radius polygon, 2..1023 tips |
| `rect` | `(cornerA,cornerB,fill,stroke,width)` → rectangle shape |
| `ellipse` | `(cornerA,cornerB,fill,stroke,width)` → ellipse in bounding rectangle |
| `line` | `(from,to,stroke,width)` → line shape |
| `path` | `(points,closed,fill,stroke,width)` → open polyline or simple closed polygon |
| `opacity` | `(shape,alpha)` → copy with both color alphas multiplied, alpha 0..1 |
| `layer` | `(shapes,index)` → copies assigned to layer 0..7; combine arrays using iteration and `push` |

Colors pack as `R | G<<8 | B<<16 | A<<24`. `0` disables fill/stroke. Widths remain
0.5..40 even with an invisible stroke. Shape maps contain `kind`, `points`, `fill`,
`stroke`, `width`, and `layer`; authors may store them in ordinary collections.
The authoring boundary validates map representations before copying. Paths require
2..2048 points, polygons at least three. Primitive shape corners are absolute
coordinates, not width/height; arrays use `vec2` elements. To rotate/scale shapes,
construct their polygon or sampled ellipse path and transform that path.

Easing names: `step`, `linear`, `smooth`, `smoother`, `quad-in`, `quad-out`,
`quad-in-out`, `cubic-in`, `cubic-out`, `cubic-in-out`, `sine-in-out`,
`expo-in-out`, `back-out`, `bounce-out`, `elastic-out`. Back and elastic curves
intentionally overshoot; keys use the selected curve between each pair. Invalid
names, nonfinite numbers, mismatched arrays and invalid styles fail explicitly.

See [the animation guide](../../docs/animation.md) for the desktop workflow,
Canvas/SVG/drawing targets, supported topology and resource limits.
