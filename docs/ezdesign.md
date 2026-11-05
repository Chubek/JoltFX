# EZDesign (desktop 1.16)

EZDesign is a native workspace for editable polygonal direct modeling, separate
from the existing triangle-mesh Design/Animation scene. This is a usable first
increment, **not the entire CAD/modeling roadmap**.

## Open and build

```sh
cmake --preset default
cmake --build build --target jfx_desktop
./build/frontends/desktop/jfx_desktop
```

Choose **EZDesign** in the workspace tab bar (scroll the bar to the right).
View > EZDesign restores a hidden tab. File > Open/Save while in this workspace
opens its file controls. Native projects use `.jfx`, with header `ezdesign 1`;
`--project design.jfx` detects this header and selects the tab automatically.
These documents are currently desktop-only, not accepted by the CLI/web/NLE
`jfx_editor` project parser. They do not overwrite the other workspaces' scenes.

## Working controls

| Tool/control | Behavior |
|---|---|
| V: Select | One vertex, edge, face, group or component; nearest visible geometry |
| L: Line | First click starts; subsequent clicks commit chained segments; Escape ends |
| R: Rectangle | Two corners on the construction plane; second click commits |
| C: Circle | Center, radius; sampled closed polygon, exact radius supported |
| A: Arc | Center, radius/start, counterclockwise end direction; sampled open edges |
| P: Push/Pull | Click face, move vertically, click again or type depth and Enter |
| O: Opening | Two corners on a visible prism face; inner window or boundary door notch |
| F: Offset | Click isolated face, enter signed distance, inspect preview, Apply |
| T: Dimension | Two snapped endpoints/points create a persistent distance dimension |
| X/Y/Z | Toggle world-axis lock; directions outside the work plane are rejected |
| Shift | Hold current Line direction while the cursor remains in the viewport |
| RMB / MMB / wheel | Orbit / pan / cursor-target zoom; no text-field navigation |
| Ctrl/Cmd-Z, Shift-Z | Independent EZDesign undo/redo |
| Ctrl/Cmd-S, Ctrl/Cmd-O | Save current EZDesign path / file controls |
| Escape | Discard preview; otherwise leave the isolated edit context |

The measurement field accepts finite scalar lengths with `m`, `mm`, `cm`, `ft`,
`in`, `'` or `"`; without a suffix it uses the displayed unit. It does not
evaluate arithmetic or compound feet-and-inches expressions. Rectangle sizes
come from snapped corners; for exact widths/depths, construct the perimeter
with typed Line lengths. The construction plane defaults to XZ, Y up. Select a
face and **Use face plane** to draw elsewhere. Top/Bottom/Front/Back/Left/Right,
Iso, orthographic/perspective, Fit model and Fit selection are available; Fit
selection currently fits a selected face, or falls back to model extents.

Endpoint, midpoint, edge, face, origin, axis/alignment and plane inference show
the resolved marker/hint. Ranking is deterministic and endpoint hysteresis is
12 px versus the normal 9 px radius. Exact input replaces the marker as well as
the preview/commit point. Plane intersections are resolved in double precision.

### Components and editing scopes

**New empty group** creates/enters an isolated scope without moving other objects.
**Group entire context** or **Component from context** wraps all geometry and
nested instances in the active scope, not just the selection. Select an instance,
then **Edit definition**, double-click, or Enter. Definition editing shows only
that definition in its local coordinates. Snapping is restricted to editable
geometry in that scope; other instances are selectable but not external snap
targets. Separate scopes never weld, even when they touch in world space.

**Linked copy** adds an instance with the entered translation (metres); changes
to the definition update both instances. **Make Unique** clones the immediate
definition with new entity IDs, preserving nested definition references. The
inspector offers ImGuizmo Move/Rotate/Scale, local/world handles and Ctrl handle
snapping for instances, vertices, edges and faces. A handle drag previews an affine matrix and commits once
on release; Escape and tab switches cancel. Reflections/singular scales are
rejected. Subobject handles pivot at the mean of their unique vertices, including
face holes. Shared vertices move together, updating adjacent faces and endpoint
dimensions; invalid nonplanar/degenerate faces reject the entire edit. Rotation
and scale use world-aligned axes initially. Transforms do not weld intersections
or perform solid contact resolution. Subobject copies are not implemented.

The **Materials** inspector section edits the document-wide plain-color palette
(three defaults, up to 32 entries). Choose a palette entry and edit its RGB color;
**Apply color** updates every face using that entry, while **Create material**
adds a new entry from the staged color. Each button is one undo step. Staged
colors are discarded when changing entries, changing the document or leaving the
workspace. Native save/reopen preserves the palette and face assignments.

Select a face and click a swatch to paint its front, or enable **Paint back side**
to paint its back. The inspector shows both assigned palette numbers.
**Reverse face** reverses the outer and hole loops and swaps front/back slots,
preserving colors on their physical sides. It is an individual orientation edit;
it does not automatically reorient the rest of a connected shell.

Dimensions
attached to endpoint IDs follow vertex edits; point dimensions remain fixed.
Deleted targets are labeled **Unresolved target** in the inspector. Grouping
updates dimension scope references. Dimensions are drawn at screen-constant text
size in their owning edit scope, not duplicated across every instance.

## Room acceptance workflow

1. Start blank. In Top view, use Line with X/Z locks and typed lengths to close
   a 6 m × 6 m floor. Escape ends the chain. Use Dimension on two endpoints.
   Pull the floor down `150 mm`; group it so wall geometry does not weld to it.
2. Use New empty group for each wall. Draw its footprint on XZ, pull up `3 m`,
   and return to the root context. Instance handles position/rotate each wall.
   Alternatively use a vertical face construction plane and extrude thickness.
3. Enter a wall, use Front/Right/Iso to see its broad vertical face. Opening's
   first corner may lie on the bottom edge for a door; the second lies inside
   the face (e.g. a 1 m × 2.1 m notch). An entirely interior rectangle creates a
   window. The wall must be one isolated, closed, constant-depth prism.
4. In a new group, make a small reusable object; Linked copy it at the root.
   Enter its definition and pull its cap or paint a face. Check both instances.
5. Undo/redo major steps. Save `.jfx`, close, reopen and check the dimension,
   door/window, face color and shared definition IDs. Export PLY or OBJ from
   EZDesign files. PLY can be read by the existing 3D Design PLY importer.

The automated room fixture constructs a dimensioned 6 m room, four 3 m walls,
door/window, two shared furniture instances and a shared-definition edit, checks
undo/redo for every command, saves/reopens and reads its PLY with Core's independent
tinyply importer:

```sh
cmake --build build --target test_ezdesign_room
./build/tests/unit/test_ezdesign_room room.jfx
./build/frontends/desktop/jfx_desktop --project room.jfx
```

The fixture is reproducible acceptance data, not a special UI-only room command.
Manual mouse placement, visual clarity at different DPI/zoom and native
macOS/Windows input still require manual viewport validation.

## Geometry and transaction contract

`frontends/desktop/ezdesign/model.{h,cpp}` contains the GPU/GUI-independent C++17
document and commands. `tools.{h,cpp}` owns tool state, camera, inference and the
pick BVH. `src/ezdesign_workspace.cpp` is only host/UI/raster adaptation.

- Persistent 64-bit IDs identify scopes (group/definition geometry), instances,
  vertices, edges, faces and dimensions, independently of vector offsets. Undo
  branches retain an ID high-water mark. Scope references form a bounded DAG;
  cycles, singular transforms and excessive expansion are rejected.
- Vertices are double-precision metres. Edges are unique unordered endpoint
  pairs. Faces own oriented vertex-ID loops whose segments reference those
  edges: outer counterclockwise about the derived normal, holes clockwise.
  Shared edges are one entity; incidence is derived from face loops. Loose
  edges, open shells and holes are permitted.
- Welding is `1e-7 m`, plane tolerance `1e-6 m`, reserved solid-conversion budget
  `1e-6 m`, and snapping `9 px`. All are centralized in `Tolerances`. Projection
  subtracts a nearby face origin; render/gizmo conversion subtracts the camera
  target before converting to float. Tests include centimetre/building geometry
  around coordinates of ten million metres.
- Line insertion splits endpoint/interior intersections and collinear overlaps
  on the work plane, walks the planar edge arrangement, extracts bounded regions
  and assigns contained holes. Existing holes are not refilled by unrelated
  lines. Triangulation validates loops and checks covered area. Earcut diagonals
  never enter the edge document. Change maps list surviving/created/deleted IDs
  and geometric edge/face descendants; there is no general Boolean merge map.
- Standalone Push/Pull adds reversed base, moved cap and oriented side faces for
  outer and inner loops. Shared shell cap motion moves its vertices, validates
  adjacent planar faces and closed opposite edge incidence, and rejects boundary
  crossing. Arbitrary concave/ambiguous cap contacts are rejected, not guessed.
- Openings are restricted to parallel caps with perpendicular, constant-depth
  prism sides. Interior cuts add two holes and tunnel sides. Boundary door
  notches use Clipper2 planar difference and rebuild the prism, retaining IDs
  where source geometry survives. This is **not** general solid subtraction.
  Notches splitting a wall into multiple solids are rejected.
- Offset uses local 2D coordinates, `1e6` integer units/metre and a conservative
  `1e8 m` local coordinate/distance range. Collapsed profiles fail transactionally;
  multiple contours/holes are preserved. Only standalone faces are replaced;
  old wire topology remains until explicit Remove stray topology.
- Every committed geometry edit operates on a candidate and publishes only after
  validation/history/change-map allocations succeed. Previews do not touch the
  document. Escape, failed commands and invalid loads preserve it and selection.
  History follows the existing vector-editor independent-history pattern, bounded
  to 32 entries/32 MiB (documents limited to 16 MiB and 8192 entities).
- Deleting an edge removes its incident faces; deleting a vertex also removes
  incident edges/faces. Deleting a face leaves its wire boundaries. Cleanup is
  explicit, never implicit while drawing. Material front/back slots swap when
  a face reverses; extrusion copies assignments to generated faces.

Native `.jfx` writes 17-digit double text with a version header, scopes, IDs,
transforms, materials, dimensions, units and saved camera. Load validates a
temporary document before one-step replacement. Save publishes a closed,
exclusive sibling `.ezdesign-tmp` atomically; an existing sibling blocks saving
instead of being overwritten. No automatic crash-recovery UI/fsync journal is
claimed. OBJ/PLY exports flatten instances and omit materials and editable
topology; **save the native project too**. PLY readback is covered independently
by Core/tinyply, not by EZDesign's own reader.

## Renderer, dependencies and current limits

The viewport reuses Core's editing raster through private
`modeling3d_preview.h::render_editing_triangles`, including its z-buffer, clipping
and coverage sampling. Existing production render defaults are unchanged.
Perspective/orthographic input and raster projections agree. Immutable triangle
snapshots run on the existing scheduler and publish only for the active document,
scope and uncancelled gesture. During motion, completed samples may briefly lag
the camera/preview while the newest sample is queued; idle restores the exact
full-size frame. No raw threads or GUI pointers cross workers.
The existing host uploads textures; completed frames survive rendering latency.

Editable edges use a separate overlay (not triangle wireframe). Normal edge
visibility currently tests the segment midpoint; partially occluded long edges
can therefore disappear or show in full. Preview edges, axes, dimensions and snap
markers are X-ray overlays. Headless ImGui tests draw derived triangles without
a GPU; actual windows use the shared depth-tested raster, not painter ordering.

Verified dependency inventory (no vendored sources modified):

| Library | Verified contract / actual build use |
|---|---|
| earcut.hpp | `include/mapbox/earcut.hpp`, ISC `LICENSE`; README says JS 3.2.4 port. Header-only inside `jfx_ezdesign`; vendor `earcut_hpp` target is not added, avoiding default benchmark/test downloads. Its private STL allocations remain upstream-owned. |
| Clipper2 | `CPP/Clipper2Lib/include/clipper2`, version header/CMake 2.0.1, Boost 1.0 license. `jfx_ezdesign_clipper` statically builds engine/offset/rectclip only, without Z, utilities, tests, downloads or source-tree `configure_file`. |
| GLM | Existing math dependency, header version 1.0.0; `dvec3`, `dmat4`, quaternion camera. No second math package. |
| ImGuizmo | Existing desktop bridge, `src/ImGuizmo.h` v1.92.5 WIP, MIT `LICENSE`; same ImGui context and process-static cleanup. No additional vendor target. |
| Manifold | Inspected `include/manifold/manifold.h` (`MeshGL64`, status/Boolean APIs), CMake 3.5.1 and Apache-2.0 license. **Not built or linked by EZDesign.** Its default C bindings/tests/downloads and optional parallel backend are not enabled. General solid tools remain unavailable pending a validated adapter/provenance pipeline. |
| OpenMesh | Inspected BSD license and `PolyConnectivity::add_face` complex vertex/edge rejection. Single boundary-loop connectivity is unsuitable for the canonical loose-edge/holed-face store. Not added. |

`jfx_ezdesign` itself uses Tilly containers/allocators and no raw threads. Vendor
earcut/Clipper internal STL storage stays behind these adapters. OCCT, CGAL,
Assimp, TinyGLTF, libdxfrw and lib3mf are not newly integrated by EZDesign. The
existing Design renderer's CGAL dependency is unaffected. Directory names/case
aliases elsewhere in `third_party` are not evidence of enabled libraries.

Unavailable or deliberately restricted in this increment:

- General Manifold union/subtraction/intersection, Follow Me sweeps, arbitrary
  Push/Pull target-plane snapping/contact, analytic curve retention and constraints.
- Subobject copies, multi/toggle/box selection, selective grouping,
  general coplanar-face merging, cross-context snapping and component overrides.
- Texture/UV editing, material names/removal, section planes,
  persistent construction guides and mesh import/conversion in EZDesign.
- glTF/STEP/DXF/3MF exchange and native `.skp` (no SketchUp C SDK integration).
- Per-face incremental triangulation/BVH refits and shared instance GPU buffers.
  One mesh rebuild reuses each definition's triangulation, but world-space
  instance triangles are currently expanded and the BVH rebuilt per revision.
  Vertex/edge screen tests are linear in active-scope geometry. Large topology
  validation/triangulation/previews still run synchronously; only raster jobs
  run in the scheduler. The benchmark below exposes this scaling limit.

## Tests and measured performance

```sh
cmake --build build --target test_ezdesign test_ezdesign_tools test_ezdesign_ui \
    test_ezdesign_exchange test_ezdesign_room jfx_ezdesign_benchmark
ctest --test-dir build -R '^ezdesign_' --output-on-failure
./build/tests/perf/jfx_ezdesign_benchmark
```

Run the same targets with `JFX_ASAN=ON JFX_UBSAN=ON` before merging. Tests cover
closure/splitting/holes, signed extrusion, door and window cuts, offset collapse,
invalid topology/cyclic references, components/Make Unique, dimensions, history,
far-origin precision, native file dispatch and independent PLY readback. Real
headless ImGui mouse/key tests cover preview/commit agreement, exact input,
materials/dimensions, component placement, gizmo interaction and focus isolation.

Verification on Linux: all five EZDesign checks pass with GCC Debug and with
Clang ASan/UBSan. Seven additional desktop/raster regressions pass under the
sanitizers (12 total), with no sanitizer findings or new-code compiler warnings.
GCC SDL/OpenGL offscreen startup opens the room for 12 frames. The existing
sanitizer build has window support disabled; its native-project two-frame
headless smoke passes instead. The full repository suite and native macOS/Windows
runtime checks were not run for this change.

Linux, Intel i7-11700K, GCC Debug without sanitizers, 1280×800 query camera,
single measurement run (not a statistically controlled benchmark):

| Scene | Load | Mesh | BVH | Pick/query | Snap/query | Definition preview + mesh |
|---|---:|---:|---:|---:|---:|---:|
| 4 windowed walls / 128 triangles | 0.465 ms | 0.759 ms | 0.184 ms | 0.0030 ms | 0.0042 ms | 1.664 ms |
| 200 repeated walls / 6400 triangles | 5.890 ms | 8.005 ms | 13.460 ms | 0.0012 ms | 0.0020 ms | 12.219 ms |

Pick averages 2000 deterministic rays; snap averages 500 root-context queries
(instances are not editable endpoint candidates in that context). This measures
the core, not GPU frame rate. Clang Debug ASan/UBSan on the same scenes measured
7.636/61.983 ms preview+mesh and 0.625/48.556 ms BVH rebuild. Rebuilding expanded
pick data, synchronous candidate validation and per-instance expansion are the
next concrete optimization targets; these are not constant-latency or Tracy
instrumented measurements.
