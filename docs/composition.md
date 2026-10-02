# Node-based composition

The composition tool builds a typed, directed acyclic graph in the shared editor.
Sources, transforms, keys, blends, adjustments, Color Calibration and Color
Grading nodes use the same evaluator on desktop, CLI/terminal, web, Android, iOS,
and the After Effects/Premiere/Resolve bridges.

`joltfx nodes` lists the complete library. `joltfx nodes KIND` describes an
operator's ports, numeric parameters and resource fields. Native inspectors use
`jfx_node_kind_*`; browser/mobile/host widgets can use `jfx_node_catalog` JSON.
Both come from the same descriptors, including the kernel-backed color operators.

## Start a composition

```sh
CLI=build/frontends/cli/joltfx
$CLI compose new composition.jfx --size 320 180
$CLI compose edit composition.jfx edited.jfx <<'EOF'
node.add 0 0 0 0 color
node.param 1 0 0 0.2 g
node.param 1 0 0 0.1 b
node.connect 1 0 0 0
node.add 0 0 0 0 grade_primary
node.connect 0 2 0 0
node.param 2 0 0 -1 exposure
node.position 2 0 0 480 80
node.label 2 0 0 0 Final graded output
node.output 2 0 0 0
save
EOF
$CLI compose info edited.jfx
$CLI compose render edited.jfx -o composition.ppm --time 0
build/frontends/desktop/jfx_desktop --project edited.jfx
```

A new composition starts with a white Solid source. The Color node supplies its
optional color input; the grade then consumes the Solid's image. The shared
fixture [`tests/fixtures/composition.jfx`](../tests/fixtures/composition.jfx)
demonstrates two sources and a straight-alpha merge.

`compose render --node N` previews an interior node without changing the project
output. `--size W H` overrides the export raster; `--time S` selects animation or
video-source time. Export writes binary PPM RGB, discarding alpha. Render failures
occur before the destination is opened. RGBA8 preview APIs preserve alpha.

## Frontend controls

### Desktop

Open **Node Compositing** from View, or **New composition** from File. Search the
typed operator library and add a node. Drag a node body to move it; drag from an
output port to an input port to connect. Right-click an input to disconnect.
Middle-drag pans; Zoom and Fit adjust the canvas. Blue ports are images, orange
ports colors, and green ports scalar values. The yellow border marks the output.

The inspector offers compatible source ports, labels, numeric/integer controls,
and resource paths committed with Enter. Duplicate, Reset and Delete act on the
selection. **Preview output** selects the document output; **Preview selected**
shows an interior node while preserving that output. **Composition raster /
export** changes dimensions or exports a PPM at the current time. The shared
project controls save/load `.jfx` files.

With the node window focused: Delete removes the selection, Ctrl/Cmd+D duplicates,
Ctrl/Cmd+Z undoes, and Ctrl/Cmd+Shift+Z redoes. Text entry suppresses shortcuts.

### Web

`mountJoltEditor(bridge)` includes `CompositionCanvas`, a metadata-driven library
and inspector. Drag nodes or output-to-input wires; right-click inputs to
disconnect. Drag the background, middle-drag or Alt-drag to pan; wheel zooms around
the cursor. Fit, duplicate/reset/delete, output/interior previews, raster setup,
undo/redo, project open/save and PPM download are available in the editor.

The bridge exposes `nodeKinds()`, `graphState()` and `renderGraphNode()`. Graph
state is queried independently of the active sequence document. Resource import
uses the module's virtual filesystem; reimport media/LUT bytes in a fresh module.
Web graph playback loops over ten seconds; the seconds field can preview any
supported time. Shortcuts follow the active document kind, so Delete removes a
node when the graph is active and a clip when the sequence is active.

### Android

The activity's **Node Compositing** section includes `CompositionGraphView`.
Drag nodes and typed wires, pan the background, pinch to zoom and use Fit. Select
a node to show generated parameter/path controls and compatible connection
selectors; choose **Disconnected** to remove a wire. Controls expose composition
setup, duplication/reset/removal, output/interior preview, time seeking, history,
project paths and PPM export. JNI delegates to the portable mobile editor.

### iOS

Add `JFXCompositionViewController.h/.m` and `JFXMobilePlayerBridge.h/.m` to the
host application (UIKit, iOS 14+, ARC). The NLE controller's **Node Composition**
button presents it, or embed it directly:

```objc
JFXCompositionViewController *nodes =
    [[JFXCompositionViewController alloc] initWithPlayer:bridge];
[self presentViewController:nodes animated:YES completion:nil];
```

Its touch canvas supports node/wire dragging, background pan, pinch zoom and Fit.
Native descriptors populate add/connection menus, numeric controls and resource
fields. The controller exposes undo/redo, rename/duplicate/reset/delete,
output/interior preview, explicit preview seconds, raster setup, project paths
and PPM export. Done returns to the NLE's sequence preview. The bridge's
`graphState`, `nodeKinds`, `previewGraphNode:seconds:` and composition export
methods are also available to custom native UIs.

### CLI and terminal

Use `compose new/edit/info/render`. `compose info` prints graph-state JSON.
The terminal accepts the commands below plus `composition` (state), `nodes`
(library), `show` (project text), `undo`, `redo`, `graph`, `sequence`, `save`, and
`quit` (exit without saving). EOF saves, and command errors exit nonzero before
writing the destination project. CRLF and a final line without a newline work.

### Host bridges

All three hosts have `jfx_host_composition_create/destroy`, `load/save`, `edit`,
`state`, `render` and `write_frame`. These sessions share the editor implementation
and command vocabulary with `jfx_host_nle_*`. SDK adapters use `jfx_node_catalog`
and graph-state JSON for their canvas and inspector, and render caller-owned RGBA8
or export PPM. Composition loading validates the document kind transactionally.
The portable APIs work without proprietary SDKs; installed host panels and
registration require the corresponding SDK adapter.

## Command reference

The compact FFI is `jfx_editor_command(editor, op, A, B, C, VALUE, TEXT)`.
Terminal lines have the same six fields. Node and port indices are **zero-based**.

| Operation | A | B | C | Value | Text |
| --- | --- | --- | --- | --- | --- |
| `graph` / `sequence` | — | — | — | — | —; select preview/save mode |
| `graph.new` | width | height | — | — | —; replace with a Solid source |
| `graph.size` | width | height | — | — | —; change composition raster |
| `node.add` | — | — | — | — | kind identifier |
| `node.duplicate` | source node | — | — | — | — |
| `node.label` | node | — | — | — | new label, including spaces |
| `node.position` | node | — | — | graph-space X | graph-space Y as a number |
| `node.param` | node | — | — | scalar value | parameter identifier |
| `node.path` | node | string slot | — | — | path; empty clears it |
| `node.connect` | source node | target node | target input | source output port | — |
| `node.disconnect` | target node | target input | — | — | — |
| `node.output` | node | — | — | — | — |
| `node.reset` | node | — | — | — | —; restore numeric defaults |
| `node.remove` | node | — | — | — | — |

Connections require identical port types. Connecting replaces an existing input
only after validation; self-links and cycles are rejected atomically. Numeric
parameters must be finite, in range, and integral for integer controls. Layout
coordinates must be finite and within `[-1e6, 1e6]`.

Duplication deeply copies resource paths and values, retains incoming wires, and
adds a 32-unit layout offset; outgoing wires remain on the original. Reset keeps
resource paths and connections. Removing a node removes its incident edges and
shifts later indices down, adjusting references. If the output is removed, the
first remaining node becomes the output; an empty graph has no output.

## Persistence and history

Graph documents retain raster, selected output, typed wires, full-precision float
values, resource strings, labels and positions. On-disk node numbers are
**one-based**, port and string indices zero-based:

```text
graph
size 320 180
node color "Color #1"
position 1 -12.25 123.5
param 1 g 0.2
node solid "Final output"
position 2 240 0
link 1 0 -> 2 0
output 2
```

Quoted labels/paths preserve spaces, `#`, quotes, backslashes, tabs and newlines;
`""` preserves an explicitly empty field. Legacy graph labels and string values
may consume the rest of an unquoted line. With no output directive the last node
is used. `graph` plus `output 0` permits an empty document to round-trip.

Graph and sequence commands share a **32-step / 32-MiB** history. Each snapshot
restores its edited document and prior active mode while retaining the other
document. New edits clear redo; failed edits/loads preserve history; successful
loads clear it. Save writes the active document. Borrowed model handles should be
reacquired after successful load/new/undo/redo. Direct model mutations require
`jfx_editor_clear_history`.

`jfx_editor_graph_state` does not switch modes. It includes active mode, raster,
output (`null` for none), history flags, and each node's kind, label, position,
values, strings and input edges. `jfx_editor_render_graph` previews a node, with
`UINT32_MAX` selecting the output, without changing the active mode or output.

## Execution and verification

Editor rasters are bounded to 4096 per axis, graphs to 256 nodes, labels to 63
UTF-8 bytes and resource paths to 511 bytes. The reference evaluator processes
only the nodes reachable from the chosen output, once each per render. Reachable
float-frame scratch is capped at 512 MiB; allocation errors propagate without
overwriting preview bytes. Incomplete image inputs render transparent, allowing
partially built compositions to be previewed. Empty graphs render transparent.
Assigned missing media/LUT resources report errors.

Compositing uses straight RGBA float frames with final RGBA8 quantization. The
graph renderer is synchronous CPU processing; kernel-backed color nodes traverse
the existing Glue/Execution path described in [the color guide](editor.md).
Video sources use optional FFmpeg decoding. Time drives procedural/video sources;
graph parameter keyframes remain unimplemented. Composition video export uses
the shared snapshot exporter, with a required frame count, configurable native
rational FPS (30/1 by default), and no audio stream. See [encoded export](media.md).

Composition API 1.1 and editor API 1.4 preserve existing public structure layouts.
`composition_editor` covers ownership, history, validation, persistence, limits
and export errors. `composition_frontend_conformance` compares state, RGBA and
PPM across desktop/mobile/web and all three host bridges. `composition_ui_tests`
drives actual Dear ImGui wiring/layout/disconnect/undo and invalid-wire gestures.
CLI integration and browser-independent Node tests cover terminal edits, canvas interactions,
generated controls and FFI cleanup. Android/iOS UI and browser WASM builds need
their platform toolchains.
