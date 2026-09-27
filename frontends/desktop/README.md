# JoltFX desktop frontend

The desktop frontend opens a real window and runs the editor: a menu bar, a
viewport that previews a live effect, a timeline, a properties panel and a
console.

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
| `--project FILE` | `.jolt` kernel to open at startup |
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

## Tests

- `desktop_frontend` (`tests/unit/frontends/test_desktop_frontend.cpp`) drives
  the library headless: preview pixels come from the backend, effect selection
  and parameter clamping round-trip, playback advances the clock, and every
  argument is validated.
- `desktop_headless` runs the executable with `--headless-smoke`.
- `desktop_window` runs 30 real frames. It is registered only when this build
  has a windowing backend, and it exits 77 (CTest's "skipped") where no display
  server is available rather than reporting a false failure.

## Limitations

- The panels are separately positioned windows, not a dockable workspace, and
  there is no node editor or undo/redo.
- `--project` and File > Open Project record a path; neither loads a project,
  because there is no project format yet.
- The ImGui font atlas is built by the renderer backend on the windowed path
  and by the frontend on the headless path. Do not build it in `create`: the
  OpenGL3 backend flags the atlas as renderer-backed, and a second build trips
  `ImFontAtlas::Build`'s `RendererHasTextures` assertion.
