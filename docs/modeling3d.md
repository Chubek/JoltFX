# 3D modeling and animation

JoltFX's 3D workspace is a shared scene editor with mesh authoring, transform
animation and deterministic frame evaluation. The desktop presents it as the
**3D Modeling & Animation** tab; web and Android expose an equivalent named
editor section; iOS opens `JFXModeling3DViewController` from the NLE screen.
CLI/terminal and the portable AE/Premiere/Resolve bridges use the same commands,
scene state and renderer.

## Workflow

1. Open the 3D workspace and add a cube, sphere or plane, or import a PLY mesh.
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

The viewport looks toward a configurable target. Camera channels are orbit yaw,
orbit pitch, distance, target XYZ and vertical field of view. Angles are degrees;
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
| `third_party/libigl` | Triangle subdivision and face normals for the shaded viewport |
| `third_party/stb` | PNG frame output through `stb_image_write` |

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

The libraries are hidden behind `jfx_modeling3d.h` (API 1.0) and editor 1.8.
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
| `3d.add` | TEXT = `cube`, `sphere`, `plane` |
| `3d.import_ply` | TEXT = mesh path; appends object |
| `3d.export_ply` | A = object, TEXT = destination path; no history entry |
| `3d.name` | A = object, TEXT = name (max 127 bytes, no control characters) |
| `3d.remove`, `3d.duplicate` | A = object; duplicate deep-copies mesh and keys |
| `3d.visible` | A = object, VALUE = 0 or 1 |
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

Meshes are embedded in the document; opening a scene does not load external
assets, modules or native code. Unknown directives, invalid indices/keys,
degenerate triangles and non-finite values are rejected before replacement.
Limits: 8 MiB serialized document; 64 objects; 65536 vertices and 131072 triangles
per object; 4096 keys per object; 16 MiB aggregate live mesh/key payload.

`jfx_editor_command`, load/save, render/render_frame and write_frame dispatch to
the scene engine. `jfx_editor_scene3d_state` supplies JSON with `active`, clock,
camera, objects, base transforms, colors, mesh counts, keys and history flags.
`jfx_editor_scene3d` returns the editor-owned borrowed handle. Standalone C/C++
clients use `jfx_scene3d_create/destroy/command/load/save/sample/render`; object,
vertex and camera queries support native inspectors. Output buffers remain
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

This is a polygon/transform authoring workspace. The viewport is a CPU,
z-buffered, flat-shaded perspective renderer with one fixed diffuse light and
two-sided faces. Maximum raster is 2048 × 2048. Triangles intersecting the near
plane are omitted. Desktop preview uses the common preview raster; exports can
use higher resolution. Materials are solid RGB, without textures, PBR, shadows,
ray tracing or GPU 3D dispatch. Selection is through the outliner/index controls;
transforms and vertex coordinates are numeric.

PLY input accepts float/double XYZ vertices and 32-bit triangle indices, ASCII
or binary. Polygons requiring triangulation and point-cloud-only files are
rejected. Other modeling formats, booleans, sculpting, extrusion, skeletons,
skinning, morph targets, constraints and integration as an NLE/composition node
are not implemented. Physics uses convex hulls (concave meshes become convex),
gravity -9.81 on Y, four fixed substeps per frame, friction 0.5, and static
colliders; no cloth/soft bodies or animated kinematic colliders. Baking starts
from base transforms, replacing dynamic position/rotation keys while preserving
scale keys. Hidden objects still participate in physics.

## Checks

`modeling3d` covers geometry, keys, round trips, failed edits, physics and PNG/PLY;
`modeling3d_ui_tests` clicks real ImGui buttons and compares rendered pixels;
`modeling3d_frontend_conformance` compares native desktop/web/mobile/host scenes;
`modeling3d_cli` exercises actual CLI files and failure preservation. Web tests
cover mounted controls; `tests/wasm.test.mjs` adds real-module coverage when built
with Emscripten.
