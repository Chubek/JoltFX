# 3D modeling and animation

JoltFX's 3D workspace is a shared scene editor with mesh authoring, transform
animation and deterministic frame evaluation. The desktop presents it as the
**3D Modeling & Animation** tab; web and Android expose an equivalent named
editor section; iOS opens `JFXModeling3DViewController` from the NLE screen.
CLI/terminal and the portable AE/Premiere/Resolve bridges use the same commands,
scene state and renderer.

## Workflow

1. Open the 3D workspace and add a primitive or import a PLY mesh. **More
   primitives** includes cylinders, cones, tori, capsules, pyramids, disks,
   NURBS surfaces and metaballs, plus tubes, hemispheres, wedges, tetrahedra,
   octahedra and icosahedra. Click **Add primitive** to create the chosen shape;
   new desktop objects are selected automatically. Curved primitives default to 64 segments;
   **Smooth shading** and four-sample antialiasing smooth lighting and silhouettes.
2. Select an object in the desktop/iOS outliner (zero-based object index in the
   terminal/Android controls). Edit position, rotation, scale and material color.
   Desktop numeric values commit on **Enter**.
3. Set the frame, then add keys to transform channels. Set a channel's value at
   another frame and add another key. Play or scrub to evaluate the animation.
4. Edit vertices by index/axis, subdivide triangles, or center and align a mesh
   to its principal axes. Mesh edits affect the object's whole animation.
5. For physics, give dynamic objects a positive mass and add a mass-zero plane
   as a floor. Bake to generate position/rotation keys, then preview or edit them.
6. Save a `.jfx` scene, export a selected mesh to PLY, export a frame, or use the
   shared encoded video job. Mesh export contains local geometry; animation and
   object transforms live in the scene document.

Desktop File > Open selects the 3D tab for a scene. File > Save writes the active
scene. Switching to another workspace retains the scene. Its undo/redo history
is separate from sequence/graph history and follows the active document mode.
There are 32 history steps with a combined 32 MiB scene-history limit. Failed
commands/loads preserve the scene; loading a scene clears its history.

The viewport looks toward a configurable target. Its orientation is a normalized
GLM quaternion, allowing unrestricted orbit and roll through either pole. Desktop:
drag to orbit, Shift-drag/middle/right-drag to pan, wheel to zoom, Alt-drag to roll.
The colored gimbal rings rotate about world X/Y/Z; view buttons snap to front,
back, left, right, top or bottom. Escape cancels a drag; release commits one undo
step. Desktop preview follows the viewport's size up to 1280 pixels per dimension.
Web supports the same mouse gestures and axis controls. Android uses one-finger
orbit and two-finger pan/pinch/roll; iOS provides pan, pinch and rotation gestures.
Numeric camera channels retain legacy yaw/pitch, distance, target XYZ and vertical
field of view. Applying legacy yaw or pitch resets orientation and roll. Angles are degrees;
the coordinate system is right-handed, Y-up. The scene defaults to 30 FPS and
120 frames. Translation/rotation/scale keys use integer scene frames; arbitrary
time previews interpolate at `seconds * fps`. No physics simulation runs during
scrubbing: a bake produces ordinary editable keys.

## Libraries and build

| Vendored library | Integrated operation |
|---|---|
| `third_party/cgal` | Triangle degeneracy and geometry validation using `Simple_cartesian<double>` |
| `third_party/bullet3` | Portable CPU rigid-body world, convex mesh colliders, fixed-step animation bake |
| `third_party/tinyply` | ASCII/binary PLY import and binary triangle-mesh export |
| `third_party/VTK` | `vtkMath::Jacobi` covariance eigensystem for principal-axis mesh alignment |
| `third_party/libigl` | Triangle subdivision |
| `third_party/stb` | PNG frame output through `stb_image_write` |
| `third_party/glm` | Camera/object quaternions, geometry generators, perspective projection and shading |

`cmake/Modeling3D.cmake` builds Bullet's LinearMath/Collision/Dynamics and only
VTK's CommonCore module plus its required dependencies. This VTK revision also
requires CommonDataModel in its exported package; it is built for installation.
VTK rendering, wrappers,
examples and tests are disabled. libigl executes serially; it creates no private
worker threads. Scene/history/render buffers use Tilly containers, Bullet's
allocation hooks and stb's write allocator route through Tilly. Vendor-owned
Eigen/tinyply/VTK temporary internals retain their library ownership.

CGAL also requires Boost 1.74+ headers. Install the development headers or pass
`Boost_ROOT` to a compatible header installation, including for cross builds.
Boost is a private compile-time dependency, without linked Boost libraries.

The vendored Eigen is older than the vendored libigl's required indexing APIs.
The build therefore fetches SHA-256-pinned Eigen **3.4.1** headers. For offline
builds provide an extracted compatible header tree:

```sh
cmake -S . -B build -DJFX_3D_EIGEN_ROOT=/path/to/eigen-3.4.1
cmake --build build --parallel
```

The libraries are hidden behind `jfx_modeling3d.h` (API 1.2) and editor 1.8.
Existing project-kind values and desktop identifiers retain their numeric values;
scene kind and desktop identifiers are appended (desktop API 1.6).

## CLI and terminal commands

```sh
CLI=build/frontends/cli/joltfx
$CLI 3d new scene.jfx
printf '3d.key 0 0 0 0\n3d.key 0 0 60 2\nsave\n' |
  $CLI 3d edit scene.jfx animated.jfx
$CLI 3d info animated.jfx
$CLI 3d render animated.jfx frame.png 1.0
$CLI export-video animated.jfx -o animation.mkv --no-audio
```

The terminal syntax is `OP A B C VALUE TEXT`, with zero-based indices. Unused
arguments are zero. `3d` changes the active mode, `scene3d` prints state JSON,
and `undo`, `redo`, `show`, `save`, `quit` use the normal terminal editor.

| Command | Arguments |
|---|---|
| `3d.new` | Empty default scene |
| `3d.add` | TEXT = `cube`, `sphere`, `plane`, `cylinder`, `cone`, `torus`, `capsule`, `pyramid`, `disk`, `nurbs`, `metaball`, `tube`, `hemisphere`, `wedge`, `tetrahedron`, `octahedron`, `icosahedron`; A = radial segments 8..128 (0 = 64) |
| `3d.import_ply` | TEXT = mesh path; appends object |
| `3d.export_ply` | A = object, TEXT = destination path; no history entry |
| `3d.name` | A = object, TEXT = name (max 127 bytes, no control characters) |
| `3d.remove`, `3d.duplicate` | A = object; duplicate deep-copies mesh and keys |
| `3d.visible` | A = object, VALUE = 0 or 1 |
| `3d.smooth` | A = object, VALUE = 0 flat / 1 smooth, with crease-preserving vertex normals |
| `3d.transform` | A = object, B = channel 0..8, VALUE = base value |
| `3d.vertex` | A = object, B = vertex index, C = axis 0..2, VALUE = local coordinate |
| `3d.color` | A = object, B = RGB channel 0..2, VALUE = 0..1 |
| `3d.subdivide` | A = object; one midpoint subdivision (four triangles per face); rejects nonmanifold edges |
| `3d.align` | A = object; centers local vertices and rotates to principal axes |
| `3d.key` | A = object, B = channel, C = frame, VALUE = key value; inserts/replaces, initially linear |
| `3d.key_remove` | A = object, B = channel, C = existing key frame |
| `3d.interpolation` | A = object, B = channel, C = existing key frame, VALUE = 0 hold / 1 linear / 2 smoothstep |
| `3d.mass` | A = object, VALUE = 0..10000; 0 is static |
| `3d.bake` | C = last bake frame, 1..600 and less than scene duration; replaces dynamic position/rotation keys |
| `3d.camera` | A = camera channel 0..6, VALUE = value |
| `3d.orbit` | TEXT = local yaw/pitch/roll increments in degrees, `X Y Z` |
| `3d.orbit_axis` | TEXT = world rotation axis `X Y Z`, VALUE = degrees |
| `3d.pan` | TEXT = camera-local translation `X Y Z` in world units |
| `3d.dolly` | VALUE = logarithmic zoom increment -10..10; positive zooms in |
| `3d.view` | A = 0 front / 1 right / 2 top / 3 back / 4 left / 5 bottom |
| `3d.navigation_begin/end/cancel` | Group camera gestures into one history step, or restore the starting camera; other scene edits are rejected during a gesture |
| `3d.nurbs_point` | A = object, B = control index 0..15, C = XYZ/weight component 0..3, VALUE = new component |
| `3d.metaball_point` | A = object, B = ball index, C = XYZ/radius component 0..3, VALUE = new component |
| `3d.metaball_add` | A = object, TEXT = `X Y Z RADIUS` |
| `3d.metaball_remove` | A = object, B = ball index; retain at least one ball |
| `3d.resolution` | A = generator object, B = tessellation resolution 8..64 |
| `3d.make_editable` | A = generator object; retains its current tessellation as an ordinary editable mesh |
| `3d.cloner` | A = object, B = 0 off / 1 linear / 2 radial / 3 XZ grid, C = count 1..64, VALUE = spacing/radius 0.01..1000 |
| `3d.cloner_make_real` | A = object, VALUE = snapshot time in seconds; replaces its instances with independently editable objects |
| `3d.script` | A = object, B = transform channel, TEXT = Joltscript source; empty TEXT removes the driver |
| `3d.script_file` | A = object, B = transform channel, TEXT = `.jolt` path; embeds the source in the scene |
| `3d.clock` | A = integer FPS 1..240, B = duration in frames 1..36000 |

Transform channels: **0..2 position XYZ**, **3..5 Euler rotation XYZ in degrees**,
**6..8 scale XYZ**. Scale must be positive (at least 0.001). Euler rotation is
applied X then Y then Z. Key interpolation is component-wise; it does not use
quaternion shortest paths. The first/last key is held outside its keyed range.
Channels with no keys use their base transform. To alter animated values, edit
keys rather than only changing the base transform.

Camera channels: **0 yaw**, **1 pitch** (-89..89), **2 distance** (0.1..10000),
**3..5 target XYZ**, **6 FOV** (10..120). Positions/rotation values are finite
and bounded to magnitude 100000. Shortening the clock past existing keys fails.

## Solid primitive sizes

Tubes have outer radius 1, inner radius 0.6 and height 2, with annular ends.
Hemispheres have radius 1, sit above Y=0 and include a flat base. Wedges are
triangular prisms spanning -1..1. Tetrahedra, octahedra and icosahedra have unit
circumradius and flat shading. Tube/hemisphere curved surfaces use smooth shading;
**Segments** controls their radial tessellation. All six are editable meshes
that support transforms, keys, subdivision, PLY interchange and scene history.

## NURBS and metaballs

The NURBS primitive is a **bicubic rational 4 × 4 patch**, with clamped knots
`[0,0,0,0,1,1,1,1]` in U and V. Controls are indexed `v * 4 + u`. Edit XYZ
and rational weight in the inspector to reshape it; changing a weight changes
the rational blend rather than moving the control point. Weights are 0.01..100.
The tessellation is regenerated after each successful control/resolution edit.
This initial surface tool has one fixed-degree patch and fixed clamped knots.

Metaballs are a shared implicit field of up to 16 editable centers/radii. Their
compact-support cubic fields add together, so nearby balls blend into a connected
surface. The field uses support radius `1.5 * radius` and isovalue 0.2. A common
cubic lattice is tessellated by marching tetrahedra with welded edge vertices and
analytic field normals. Radius is 0.01..100. Surface resolution controls sampling;
features smaller than a lattice cell may require increasing it. An edit that
produces an empty, degenerate or oversized mesh is rejected atomically.

**Make generator editable** preserves the evaluated polygon mesh and enables
vertex editing, subdivision and alignment. PLY export always writes current local
tessellated geometry; procedural controls remain in `.jfx`.

## Cloners and scripted animation

Cloners instance the selected object's geometry, material, visibility and keyed
transform. Linear offsets follow local X, radial offsets form a local XZ ring,
and grid offsets use a square XZ arrangement (row width is `ceil(sqrt(count))`).
Offsets rotate with the source object's keyed orientation. Instances retain a
common orientation. The inspector edits mode, count and spacing/radius without
duplicating mesh storage. **Make clones real at playhead** snapshots the evaluated
transforms into independent objects, clearing drivers/keys on those snapshots.
The scene-wide 64-object limit applies to this conversion. Physics baking requires
real objects; convert cloners before baking. PLY exports the source mesh.

Each transform channel can have one immutable compiled Joltscript scalar driver.
In **Joltscript animation**, select the channel, enter source, and click **Apply
animation script**. **Load assigned animation script** retrieves the saved source.
For example, drive Rotation Y with:

```lisp
(defkernel spin [time frame index value]
  (+ value (* time 90)))
```

Inputs are positional: seconds, fractional scene frame, zero-based clone index,
and the channel's keyed value after clone placement. A driver outputs the final
channel value. Drivers are pure, bounded JBC1 programs executed by the existing
Joltscript VM at arbitrary preview/export times; no frame baking is required.
Use zero to four input bindings and exactly one scalar output. Supported scalar
operators are arithmetic (`+ - * / min max abs floor pow sqrt`), comparisons,
logical/bitwise operations and `select`; operators retain the compiler's fixed
arity. Sources are limited to 4096 bytes. Invalid source is rejected on attachment;
non-finite/out-of-range runtime values fail the frame without publishing partial
pixels. Scripts do not access files or mutate the scene during evaluation.

Examples in `examples/modeling3d/` demonstrate spin, looping bounce and indexed
clone offsets. Load a file through the CLI, then render the same animation:

```sh
printf '3d.script_file 0 4 0 0 examples/modeling3d/spin.jolt\nsave\n' |
  $CLI 3d edit scene.jfx scripted.jfx
$CLI 3d render scripted.jfx scripted.png 1.0
```

## Scene format and APIs

The `.jfx` scene starts with `scene3d 1`, then `clock FPS FRAMES` and
`camera YAW PITCH DISTANCE TARGET_X TARGET_Y TARGET_Z FOV`. Each object has:

```text
object "Object name" VISIBLE MASS
transform TX TY TZ RX RY RZ SX SY SZ
color R G B
v X Y Z
# more vertices
f I J K
# more zero-based triangle indices
key CHANNEL FRAME VALUE INTERPOLATION
# keys sorted by channel then frame
end
```

Optional `quaternion X Y Z W` stores the normalized camera orientation. Old
documents without this record derive it from legacy yaw/pitch. Objects also store
`smooth`, `generator KIND RESOLUTION`, `control X Y Z WEIGHT`, `ball X Y Z RADIUS`,
`cloner MODE COUNT SPACING`, and `script CHANNEL HEX_UTF8_SOURCE` records. Script
source is hex encoded to preserve multiline text exactly. Generator meshes are
recreated from controls on load; ordinary meshes are embedded. Scripts compile
on load, without running them until evaluation.

Meshes are embedded in the document; opening a scene does not load external
assets, modules or native code. Unknown directives, invalid indices/keys,
degenerate triangles and non-finite values are rejected before replacement.
Limits: 8 MiB serialized document; 64 objects; 65536 vertices and 131072 triangles
per object; 4096 keys per object; 16 MiB aggregate live mesh/key/script payload;
64 instances per cloner and 2,097,152 aggregate instanced triangles.

`jfx_editor_command`, load/save, render/render_frame and write_frame dispatch to
the scene engine. `jfx_editor_scene3d_state` supplies JSON with `active`, clock,
camera, objects, base transforms, colors, mesh counts, keys and history flags.
`jfx_editor_scene3d` returns the editor-owned borrowed handle. Standalone C/C++
clients use `jfx_scene3d_create/destroy/command/load/save/sample/render`; object,
vertex and camera queries support native inspectors. API 1.1 adds
`jfx_scene3d_procedural_info`, `jfx_scene3d_script`, `jfx_scene3d_sample_instance`
and `jfx_scene3d_camera_quaternion` (XYZW). Output buffers remain
untouched on rendering/serialization errors.

Frontend wrappers: `jfx_desktop_frontend_scene3d_state`,
`jfx_web_session_scene3d_state`, `jfx_mobile_player_scene3d_state`, and
`jfx_host_scene3d_create/state`. Host scenes reuse the portable session's
load/save/edit/render/export functions. Actual proprietary host panel registration
still belongs to the host SDK adapter. Android/iOS use their portable mobile
session for shared project handling and playback. Browser imports live in the
WASM virtual filesystem; the PLY export button downloads the native output.

Encoded exports take a scene snapshot, use its FPS/duration and render through
the same evaluator. They are silent; scene documents have no audio tracks. PNG
and PLY exports exclusively create a temporary sibling and replace the destination after
success. PPM frame output is also available through shared frontend APIs.

## Current rendering and modeling scope

This is a polygon/procedural-surface and transform authoring workspace. The
viewport is a CPU z-buffered perspective renderer with smooth or flat normals,
one fixed diffuse light and two-sided faces. Four subpixel samples are averaged
for antialiased output. Triangles are clipped against the near plane before
projection. Maximum output raster is 2048 × 2048. Materials are solid RGB, without textures, PBR, shadows,
ray tracing or GPU 3D dispatch. Selection is through the outliner/index controls;
transforms and vertex coordinates are numeric.

PLY input accepts float/double XYZ vertices and 32-bit triangle indices, ASCII
or binary. Polygons requiring triangulation and point-cloud-only files are
rejected. Other modeling formats, booleans, sculpting, extrusion, skeletons,
skinning, morph targets, constraints and integration as an NLE/composition node
are not implemented. Physics uses convex hulls (concave meshes become convex),
gravity -9.81 on Y, four fixed substeps per frame, friction 0.5, and static
colliders; no cloth/soft bodies or animated kinematic colliders. Baking starts
from base transforms, replacing dynamic position/rotation keys and drivers while preserving
scale keys. Hidden objects still participate in physics.

## Checks

`modeling3d` covers geometry, keys, round trips, failed edits, physics and PNG/PLY;
`modeling3d_tools` covers generators, cloners, drivers, camera orientation and
failure preservation; `modeling3d_ui_tests` sends real ImGui button/viewport input
and compares rendered pixels and one-step gesture undo;
`modeling3d_frontend_conformance` compares native desktop/web/mobile/host scenes;
`modeling3d_cli` exercises actual CLI files and failure preservation. Web tests
cover mounted controls; `tests/wasm.test.mjs` adds real-module coverage when built
with Emscripten.
