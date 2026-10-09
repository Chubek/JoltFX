# JoltFX desktop frontend

The Dear ImGui desktop editor has one tab per interface: **NLE**, **Layer
Effects**, **Color Calibration**, **Color Grading**, **Node Compositing**,
**Audio Mixing**, **DAW**, **2D Animation**, **3D Modeling & Animation**, **Plugins**, **Console** and **Statistics**. Transport, clip selection, preview,
undo/redo and Export are shared across tabs. View controls tab visibility; on
narrow layouts the shared preview is collapsible above the active interface.
The shared workspace preview supports 0.25x–8x zoom through the Zoom control or
Ctrl/Cmd plus mouse wheel; the zoom value is independent of render resolution.

**Color Grading** has Resolve-inspired Lift/Gamma/Gain and Shadows/Midtones/
Highlights wheels, master dials and rotary scalar controls generated from kernel
metadata. Drag right/up on a dial, hold Shift for fine control, double-click to
reset, or use arrow keys and numeric entry. Wheel pucks adjust color balance;
their master dials adjust the RGB channels together. Each live gesture and Reset
parameters operation creates one undo step. Tab/selection changes finish the
current gesture. Color controls also support LUT paths, bypass, reorder and
keyframes. See [color/editor usage](../../docs/editor.md).

**Layer Effects** is the per-clip composite stack for transforms, keys, blends
and adjustments. Operators are grouped by category in the add menu, each layer
shows its clip order, blend mode, opacity, parameters and keyframe counts, and
reorder/remove act within the layer stack.

**Color Calibration** is the technical normalization pipeline that runs before
creative grading: white balance, black/white levels, gamma, log/legal range,
color space and LUT stages. Stages are numbered in pipeline order with their
clip order, integral options (channel, color space, tone/log curves, clamp and
preserve switches) render as labeled combos, white-balance temperature uses a
Kelvin slider, and **Bypass all calibration** toggles the whole pipeline in one
undo step. Rendering always follows the shared clip order across layers,
calibration and grading.

The **Timeline** is an interactive NLE: drag clip bodies to move between tracks,
drag edges to trim, scrub the ruler, zoom/pan, Fit or Snap. Controls expose split,
duplicate, source slip, delete/ripple delete, gap insertion, track ordering,
mute/solo, names and undo/redo. **Sequence setup / frame export** sets raster and
rational FPS or writes a PPM frame. See [the NLE guide](../../docs/nle.md).

Timeline shortcuts: Space play/pause; S split; Delete delete; Shift+Delete ripple
delete; Ctrl/Cmd+Z undo; Ctrl/Cmd+Shift+Z redo. Text entry suppresses shortcuts.

**Node Compositing** provides a searchable typed library, draggable nodes and
output-to-input wiring. Middle-drag pans; Zoom/Fit adjusts the view; right-click
an input to disconnect. Generated inspectors edit labels, parameters and paths,
with duplicate/reset/delete, output/interior preview, composition raster and PPM
export. Node positions and edits persist in `.jfx` and participate in shared
undo/redo. Node-tab shortcuts: Delete, Ctrl/Cmd+D, Ctrl/Cmd+Z and
Ctrl/Cmd+Shift+Z. See [the composition guide](../../docs/composition.md).

```bash
cmake --preset default
cmake --build build --target jfx_desktop
./build/frontends/desktop/jfx_desktop
```

## Options

The **3D Modeling & Animation** tab has its own scene outliner, mesh/transform
inspector, frame slider, playback, undo/redo and shaded viewport. Add a cube,
sphere or plane; select it in the outliner; enter numeric values and press Enter
to commit. **Key** stores a transform channel at the current frame. Select a
channel/frame to change interpolation or remove a key. Mesh editing exposes
vertices, subdivision and principal-axis alignment; rigid-body controls set mass
and bake motion into editable keys. **More primitives** adds cylinders, cones,
tori, capsules, pyramids, disks, NURBS patches, metaballs, tubes, hemispheres,
wedges, tetrahedra, octahedra and icosahedra. Select a shape and click **Add
primitive**; the new object is selected automatically. Generator inspectors
edit control points/weights or ball centers/radii; cloners arrange instances in
lines, rings or grids. **Joltscript animation** assigns a source or `.jolt` file
to a transform channel, with time/frame/index/keyed-value inputs.
The viewport uses smooth shading and four-sample antialiasing at its display size.
Drag to orbit, Shift/middle/right-drag to pan, wheel to zoom and Alt-drag to roll.
Drag the GLM quaternion gimbal rings to rotate individual world axes. Escape
cancels; release commits one undo step. Numeric target/distance/FOV and axis-view
buttons are also available. PLY import/export and PNG/video output use explicit paths. File > Open/
Save handles embedded `.jfx` scenes. See [3D usage](../../docs/modeling3d.md).

| Option | Meaning |
|---|---|
| `--backend NAME` | `vulkan`, `metal`, `d3d12`, `webgpu` or `auto` |
| `--width N`, `--height N` | Window size (default 1280x720) |
| `--effect NAME` | Bundled effect to preview (default `brightness`) |
| `--param VALUE` | Effect parameter, clamped to its documented range |
| `--project FILE` | `.jfx` sequence/graph or `.jolt` kernel to open at startup |
| `--plugin MODULE` | Load a native SDK module before opening the project; repeatable |
| `--frames N` | Exit after N frames (0 runs until quit) |
| `--duration SEC` | Exit after SEC seconds of wall clock |
| `--headless-smoke` | Compose frames with no window, then exit |
| `-h`, `--help` | Show help |

## How it works

The frontend links no SDL or OpenGL symbol itself. Everything platform-specific
lives in `src/host_window.cpp`:

- **SDL2** for the window and the input queue
- **OpenGL 3.3 core** for the context, stepping down through 3.2 and 3.0
  compatibility for drivers that refuse a core profile
- the vendored **Dear ImGui** SDL2 and OpenGL3 backends, so the composed draw
  data is rasterized and presented rather than discarded

That split is what lets the same UI code run headless, which is the path
`--headless-smoke` and the unit test use.

What the UI shows is real state:

- the viewport image is the selected effect rendered through the Core engine's
  selected backend and uploaded as a texture;
- the timeline advances the engine clock while playing, and loops at the end of
  the range;
- the Layer Effects tab edits the live effect and its parameter, and reports the
  resolved device, whether a GPU is available, and whether the last frame ran
  on one;
- the console is a sink on the Tilly logger plus engine event subscriptions, so
  it shows what the engine actually did;
- the statistics panel (View > Statistics) reports engine and UI frame counts,
  frame times, and live buffer/texture/kernel counts.

ImGui layout persists in the platform's per-user config directory
(`$XDG_CONFIG_HOME/joltfx`, `%APPDATA%\JoltFX`,
`~/Library/Application Support/JoltFX`) and never writes into the source tree.

## Building without a window

The window needs SDL2 and OpenGL. Without them the target still builds and
`--headless-smoke` still exercises the UI path, so a host with no display
toolchain is not blocked. CMake finds SDL2 through `find_package(SDL2)` and
falls back to `pkg-config sdl2`; `-DJFX_DESKTOP_WINDOW=OFF` disables the window
explicitly.

## Audio and encoded export

The **DAW** workspace adds Arrangement, Mixer, MIDI / Instruments, Recording,
Automation, VST3 Inserts and Mixdown views.
Native builds enable VST3 hosting with `JFX_AUDIO_VST3=ON` (default), using pinned
MIT-licensed Steinberg interfaces. Scan installed plugins or a custom path, add
stereo effects/instruments to a track, open native editors or generic controls,
edit MIDI notes and gain/parameter automation, record an input take and bounce
stereo float WAV. Inserts and latency compensation also apply to video exports.
Tempo, racks, opaque plugin state, notes and lanes persist in `.jfx` and share
undo/redo. Native Linux views require SDL's X11 backend; recording uses SDL input.
See [DAW and VST3 guide](../../docs/daw.md) for setup, processing and supported
plugin features.

Open **Audio Mixing** to adjust all track faders, mute and solo side by side.
Add an audio track, enter an audio file path and duration in frames, then add it
at the playhead. Select an audio or video clip in the shared clip menu to adjust
its gain, stereo balance, enable switch and fade lengths. Gain ranges from zero
to 16 times unity; faders show linear gain and decibels. A fader drag creates one
undo step, and mix settings persist in `.jfx` projects. Mute/solo apply to both
audio and video, as in the NLE.

Stereo output meters show peaks of the mixed playback blocks in dBFS. **OVER**
indicates samples above full scale; lower track or clip gain to retain headroom.
Meters clear on pause, seek or edits. The mixer is shared with playback and
encoded export; no separate audio library or mixer document is needed.

The timeline exposes Audio sources, clip audio enable, linear clip/track gain,
stereo pan and frame-length fades. Playback sends the shared immutable mix to
SDL2's queued float-stereo output. Pausing, seeking and successful edits reset
queued audio and mixer snapshots.

The shared **Export...** button opens video export controls from every tab.
Encoded export controls supply output path, frame start/count, encoder name and
mixed audio, with progress and cancellation. One frame is stepped per event-loop
turn. Native builds discover system FFmpeg; `JFX_MEDIA_FFMPEG_BUNDLED=ON` builds
the vendored MPEG-4/AAC, ProRes/PCM and FFV1/PCM profile. See
[the shared media guide](../../docs/media.md).

## Plugins

Open **Plugins** or **Extensions > Plugin manager**, enter a module path and
click **Load module**. Loaded modules expose metadata, editor actions and custom
effects in the shared catalogs. Actions use the selected clip/node, update the
preview and undo in one step. **Extensions** also lists actions directly.

An unload reports busy while graphs, clips, history or export snapshots reference
the module. Remove those operators and use **Clear editor history** to release
history-only references. See [SDK build and usage](../../docs/plugins.md).

```sh
./build/frontends/desktop/jfx_desktop --plugin "$PWD/build/sdk/jfx_example_plugin.so" \
    --project edited.jfx
```

Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z work across workspace tabs; text entry suppresses
the shared shortcuts.

## Tests

- `desktop_frontend` (`tests/unit/frontends/test_desktop_frontend.cpp`) drives
  the library headless: preview pixels come from the backend, effect selection
  and parameter clamping round-trip, playback advances the clock, and every
  argument is validated.
- `desktop_headless` runs the executable with `--headless-smoke`.
- `nle_frontend_conformance` compares shared edit state, color pixels and PPM
  export between desktop/mobile/web; `nle` covers timing and history.
- `composition_frontend_conformance` compares graph state, interior/output RGBA
  and PPM export across desktop/mobile/web and host bridges.
- `composition_ui_tests` simulates Dear ImGui canvas wiring, layout dragging,
  disconnect/undo and invalid-wire gestures against the real shared document.
- `workspace_ui_tests` drives the actual tabs, grading dials/wheels, live pixels,
  one-step undo, audio fader gestures/mute and saved stereo mixes, tab switches
  mid-gesture, plugin actions and narrow/high-DPI layouts.
- `desktop_window` runs 30 real frames. It is registered only when this build
  has a windowing backend, and it exits 77 (CTest's "skipped") where no display
  server is available rather than reporting a false failure.

## Limitations

- Sequence and graph edits share bounded undo/redo (32 steps / 32 MiB).
- Project dialogs use typed paths. Frame export writes PPM; encoded export uses
  the shared FFmpeg snapshot jobs.
- The ImGui font atlas is built by the renderer backend on the windowed path
  and by the frontend on the headless path. Do not build it in `create`: the
  OpenGL3 backend flags the atlas as renderer-backed, and a second build trips
  `ImFontAtlas::Build`'s `RendererHasTextures` assertion.

## Vector animation workspace

The **2D Animation** tab has a dedicated 960 × 540 vector stage, tool shelf and
layer exposure sheet. Draw with Brush (B), Line (L), Rectangle (R) and Ellipse
(O). Stroke/fill colors, fill enable and stroke width apply to new artwork;
**Apply style** updates the selected shape as one undoable edit.
Select (V) picks the topmost shape on the active layer; drag to move it or press
Delete to remove it. Eraser (E) removes a clicked shape. Hand (H) pans, the wheel
zooms, and **Fit stage** resets the view. Escape cancels a drawing/move gesture.

Click a frame cell or scrub the frame slider. Drawings hold until the next key;
**Duplicate key** (F6) copies the held drawing to the current frame, while **Blank
key** (F7) starts an empty cel (or clears an existing key). Editing a held exposure
changes its source key. Add layers and toggle their visibility or lock. Onion
skin shows the neighboring keys in red/blue. Space or **Play drawing** loops the
120-frame sequence at the selected FPS. The drawing clock is independent of NLE
playback and pauses when switching workspaces.

Use **Undo drawing** / **Redo drawing**, Ctrl/Cmd-Z and Ctrl/Cmd-Shift-Z for up to
32 document edits. Each completed stroke or move is one undo step. Artwork
survives workspace switches. **Drawing files...** saves/loads a versioned
`.jfxdraw` document, including layers, cels and vector styles; Ctrl/Cmd-S saves
to its drawing path. Loading is undoable and invalid files preserve the current
drawing. **Export frame SVG** exports visible artwork at the current frame with
transparent background, without selection, grid or onion skins.

Drawing files are separate from `.jfx` sequence/composition projects and JFA1
skeletal bytecode. Use the drawing-specific file controls to preserve artwork
before closing the application. This first vector workflow does not yet provide
Bezier/node editing, pressure sensitivity, tweening, audio sync, or animated
video export. Interactive limits are eight layers, 256 shapes per cel and 2,048
points per stroke; loaded files are additionally limited to one million points.

`animation_drawing` covers exposure/history/persistence and malformed files;
`animation_ui_tests` drives actual ImGui mouse/keyboard input and checks vector
geometry, movement, cancellation, undo/redo, cel clearing and tab retention.
