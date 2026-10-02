# JoltFX desktop frontend

The desktop frontend opens a real window and runs the editor: a menu bar, a
viewport that previews a live effect, a timeline, a properties panel and a
console.

The editor includes separate **Color Calibration** and **Color Grading** windows
for the selected clip, alongside Layer Effects and Node Compositing. Controls
come from kernel metadata and support LUT paths, bypass, reset, reorder and
keyframes. See [color/editor usage](../../docs/editor.md).

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
undo/redo. Node-window shortcuts: Delete, Ctrl/Cmd+D, Ctrl/Cmd+Z and
Ctrl/Cmd+Shift+Z. See [the composition guide](../../docs/composition.md).

```bash
cmake --preset default
cmake --build build --target jfx_desktop
./build/frontends/desktop/jfx_desktop
```

## Options

| Option | Meaning |
|---|---|
| `--backend NAME` | `vulkan`, `metal`, `d3d12`, `webgpu` or `auto` |
| `--width N`, `--height N` | Window size (default 1280x720) |
| `--effect NAME` | Bundled effect to preview (default `brightness`) |
| `--param VALUE` | Effect parameter, clamped to its documented range |
| `--project FILE` | `.jfx` sequence/graph or `.jolt` kernel to open at startup |
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
- the properties panel edits the live effect and its parameter, and reports the
  resolved device, whether a GPU is available, and whether the last frame ran
  on one;
- the console is a sink on the Tilly logger plus engine event subscriptions, so
  it shows what the engine actually did;
- the statistics panel (View > Statistics) reports engine and UI frame counts,
  frame times, and live buffer/texture/kernel counts.

Panel layout persists in the platform's per-user config directory
(`$XDG_CONFIG_HOME/joltfx`, `%APPDATA%\JoltFX`,
`~/Library/Application Support/JoltFX`) and never writes into the source tree.

## Building without a window

The window needs SDL2 and OpenGL. Without them the target still builds and
`--headless-smoke` still exercises the UI path, so a host with no display
toolchain is not blocked. CMake finds SDL2 through `find_package(SDL2)` and
falls back to `pkg-config sdl2`; `-DJFX_DESKTOP_WINDOW=OFF` disables the window
explicitly.

## Audio and encoded export

The timeline exposes Audio sources, clip audio enable, linear clip/track gain,
stereo pan and frame-length fades. Playback sends the shared immutable mix to
SDL2's queued float-stereo output. Pausing, seeking and successful edits reset
queued audio and mixer snapshots.

Encoded export controls supply output path, frame start/count, encoder name and
mixed audio, with progress and cancellation. One frame is stepped per event-loop
turn. Native builds discover system FFmpeg; `JFX_MEDIA_FFMPEG_BUNDLED=ON` builds
the vendored MPEG-4/AAC, ProRes/PCM and FFV1/PCM profile. See
[the shared media guide](../../docs/media.md).

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
- `desktop_window` runs 30 real frames. It is registered only when this build
  has a windowing backend, and it exits 77 (CTest's "skipped") where no display
  server is available rather than reporting a false failure.

## Limitations

- Panels are separately positioned windows. Sequence and graph edits share
  bounded undo/redo (32 steps / 32 MiB).
- Project dialogs use typed paths. Frame export writes PPM; encoded export uses
  the shared FFmpeg snapshot jobs.
- The ImGui font atlas is built by the renderer backend on the windowed path
  and by the frontend on the headless path. Do not build it in `create`: the
  OpenGL3 backend flags the atlas as renderer-backed, and a second build trips
  `ImFontAtlas::Build`'s `RendererHasTextures` assertion.
