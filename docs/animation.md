# 2D animation: Ingagi, vector cels and export

The desktop **2D Animation → Ingagi script...** editor authors animation in
Ingagi. Enable `JFX_INGAGI=ON`, write `fn frame(t: f64)` returning an array of
shapes, and click **Bake Ingagi animation**. Time is in seconds, starting at
zero; stage coordinates are 960 × 540 pixels. Bake uses the drawing's FPS/frame
count and replaces its cels as one undoable operation. Compile, execution and
validation failures preserve the previous artwork and history.

**Save/Load animation .ing** persists source. **Drawing files...** saves baked
artwork as `.jfxdraw`, exports the current frame as SVG, or exports the full
timeline as a standalone **HTML5 Canvas** page. Source and baked artwork are
separate files. The source widget has local text undo; drawing undo restores
artwork. Drawing, selection and transport remain independent of NLE history.

```ingagi
import std::animation2d as a;
fn frame(t: f64) {
    let x = a::tween(120,840,a::pingpong(t,2.5),"smooth");
    return [a::ellipse(vec2(x-40,230),vec2(x+40,310),
        a::rgba(0.2,0.6,0.95,1),a::rgba(0.1,0.15,0.2,1),3)];
}
```

See [`ingagi/stdlib/animation2d.md`](../ingagi/stdlib/animation2d.md) for all 23
operations and [`ingagi/examples/animation2d.ing`](../ingagi/examples/animation2d.ing)
for animated stars, a Bézier motion path and translucent shapes.

## Command line and browser

From `ingagi/`:

```sh
GOROOT=/usr/lib/go go run ./cmd animation -target canvas -fps 24 -frames 120 -o animation.html examples/animation2d.ing
GOROOT=/usr/lib/go go run ./cmd animation -target svg -frame 61 -o frame.svg examples/animation2d.ing
GOROOT=/usr/lib/go go run ./cmd animation -target drawing -o animation.jfxdraw examples/animation2d.ing
```

Canvas HTML includes its sampled vector timeline and player; it needs no server,
WASM download or installed Ingagi runtime. Playback loops at the authored FPS,
with pause and frame seeking. Embedders can call
`globalThis.ingagiAnimation.play()`, `.pause()` and `.seek(zeroBasedFrame)`.
Transparent backgrounds and straight-RGBA colors survive export. Layer order,
held exposures and hidden layers match the desktop document.

This is a **baked vector-animation target**, not general Ingagi-to-JavaScript
compilation. Script state initializes once and `frame(t)` runs in increasing time
order. Browser playback never re-executes source. SVG exports one sampled frame.
Native scalar QBE/WASM and general renderer target support are separate contracts.

## Geometry, ownership and bounds

Vendored **lib2geom** provides Bernstein Bézier evaluation and point/segment
picking through a synchronous, allocation-free private bridge. Only these
header kernels are used; the GTK/GSL/toy build and GPL-option sources are not
linked. Upstream dual LGPL-2.1/MPL-1.1 notices are installed with JoltFX.
CGO-enabled Ingagi uses the same bridge; `CGO_ENABLED=0` uses equivalent portable
Bernstein evaluation. CMake and CGO require Boost headers for lib2geom.

Shapes use owned arrays/maps; transforms and modifiers return copies. Supported
geometry is lines, rectangles, ellipses, open polylines and simple closed concave
polygons. Curves/arcs are explicitly subdivided into polylines. Holes, Boolean
operations, text, gradients, images, skeletal skinning and adaptive curve fitting
are outside this vector library. Desktop polygons use ImGui concave tessellation;
self-intersecting polygons are not a supported portable fill topology.

Authoring limits: 16 KiB source, 1–240 frames, 1–60 FPS, eight layers, 256 shapes
per layer/frame, 2–2048 points per path and one million sampled points per bake
or export. Coordinates are finite and within ±100,000 pixels; widths are 0.5–40.
Each initializer/frame invocation has a 100,000-instruction/64-call-frame budget.
Only bundled standard-library imports resolve in animation authoring. These
execution limits do not make the general-purpose VM a memory/time sandbox.

`JFXDRAW2` adds polygon kind 4; readers retain `JFXDRAW1` support. Loads validate a
candidate before publication. Canvas exports validate and serialize before
replacing the output file. Drawing/HTML publication uses a sibling temporary.
The optional-off desktop retains drawing/SVG and reports unavailable Ingagi/HTML
actions explicitly. Bake/export currently execute synchronously.

## Legacy skeletal runtime

The separate skeletal controls use `jfx_animation_scene_t` as their renderer-neutral model.
Bones store local transforms and parent IDs; evaluation samples continuous-time
keys, applies step/linear/smooth curves, and propagates world transforms from
roots to leaves. A frontend can drive `jfx_animation_tab_tick` from SDL's
monotonic clock or from a browser animation callback and render the resulting
pose through its own backend.

`jfx_animation_compile` emits the deterministic, little-endian JFA1 payload.
The payload contains a versioned header, skeleton transforms, and keyframes;
`jfx_animation_validate` checks its bounds before `jfx_animation_play` decodes
and evaluates it. The same payload is the input contract for the native LIEF
section writer and the WASM player, so neither target needs editor objects.

The current API deliberately keeps rendering and asset resolution outside the
evaluator. That lets SDL, video export, native runtime stubs, and browser
canvas/WebGPU adapters share exactly the same pose and bytecode semantics.
