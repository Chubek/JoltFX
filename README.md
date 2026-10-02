# JoltFX

An effects engine for real-time motion graphics, with a scripting language for
kernels, a pluggable backend layer, and frontends for the desktop, the command
line, the browser and mobile.

**0.5.0-beta.1** is the current public-beta development release.

## Overview

JoltFX is built in layers, each of which can be used on its own:

- **Tilly / TillyZ** — the runtime foundation: allocators, a logger, a dynamic
  module registry, and the bootstrap compiler TillyZ.
- **JoltScript** — the kernel language. The Glue layer compiles `.jolt` source
  to JBC1 bytecode; the Execution layer runs it on the CPU as a bounded stack
  program.
- **Core** — the engine: owns backend selection, the frame loop, the scheduler,
  the event bus, buffers, textures, kernels and the native plugin host.
- **Backends** — Vulkan, Metal, D3D12 and WebGPU, behind one operations table.
  Vulkan dispatches JBC1 to the device as generated GLSL compute shaders; the
  other three currently run the shared validated CPU pipeline.
- **Kernels** — the bundled effect catalog (12 colour effects), compiled once at
  build time and addressable by name from any frontend.
- **Zoltan** — the Joltscript compiler. It emits bytecode byte-identical to the
  C compiler's.
- **Frontends** — a desktop editor, a CLI, a web player, a mobile player, and
  host-application plugin bridges.
- **Extension runtimes** — sandboxed Lua and mruby hosts with enforced memory
  and instruction budgets.

## Quick start

```bash
# Configure and build (needs SDL2 for the desktop window; it degrades to a
# headless-only build without it)
cmake --preset default
cmake --build build -j

# Run the test suite
ctest --test-dir build --output-on-failure

# Or by label
ctest --test-dir build -L conformance --output-on-failure
```

## Using it

### Command line

```bash
CLI=build/frontends/cli/joltfx

$CLI effects                       # list the bundled kernels
$CLI render --effect gamma --param 1.8 -o frame.ppm
$CLI render --backend vulkan ...   # dispatch on the device, not the CPU
$CLI export --effect invert --start 0 --end 47 -o frames/
$CLI capabilities                  # what this build actually implements
$CLI info build/glow.jbc           # inspect compiled bytecode
```

### Desktop

```bash
build/frontends/desktop/jfx_desktop
build/frontends/desktop/jfx_desktop --effect contrast --param 1.6 --project k.jolt
build/frontends/desktop/jfx_desktop --headless-smoke   # no display server needed
```

The desktop frontend opens a real window, runs a frame loop, and previews the
selected effect by rendering it through the engine's backend. The properties
panel reports the resolved device and whether the last frame ran on a GPU.
`--frames N` or `--duration SEC` bound the run.

### Non-linear editing and color

Open `.jfx` sequences in the desktop Timeline, or use the shared terminal editor:

```sh
build/frontends/cli/joltfx nle new sequence.jfx --size 320 180 --fps 30000 1001
build/frontends/cli/joltfx nle edit sequence.jfx edited.jfx
build/frontends/desktop/jfx_desktop --project edited.jfx
```

The shared NLE supports split, trim, move, duplicate, source slip, ripple edits,
track controls and undo/redo. **Color Calibration** and **Color Grading** act on
the selected clip through kernel-backed operators. Both subsystems are exposed
in desktop, CLI/terminal, web, Android, embeddable iOS controllers and the
AE/Premiere/Resolve bridge APIs. See [NLE usage](docs/nle.md) and
[color usage](docs/editor.md) for frontend controls, persistence and export.

### Node-based composition

**Node Compositing** supplies typed visual graphs, drag-to-connect ports,
metadata-driven inspectors, persistent layouts and shared undo/redo across all
frontends. Sources, keys, transforms, blends and color operators can be combined
and previewed or exported using the same evaluator.

```sh
build/frontends/cli/joltfx compose new composition.jfx --size 320 180
build/frontends/cli/joltfx compose edit composition.jfx edited.jfx
build/frontends/cli/joltfx compose render edited.jfx -o composition.ppm
```

See [the composition guide](docs/composition.md) for desktop/web/touch controls,
the embeddable iOS controller, host APIs and the shared command reference.

### Audio mixing and video export

Timeline audio supports WAV/FLAC/MP3 and FFmpeg media containers, with clip/track
gain, stereo balance, fades, mute/solo and frame-accurate source timing. Desktop,
browser, Android and iOS playback use the shared mixer. Encoded export supports
MP4/MOV/MKV, progress/cancellation and transactional output across all frontends.

```sh
cmake -S . -B build -DJFX_MEDIA_FFMPEG_BUNDLED=ON
cmake --build build --parallel
build/frontends/cli/joltfx export-video edited.jfx -o final.mp4
build/frontends/cli/joltfx compose export composition.jfx -o animation.mkv --frames 300
```

FFmpeg and miniaudio are vendored submodules. Native builds can use system FFmpeg
development packages; the bundled profile builds on native, WASM, Android and
iOS toolchains. See [audio/export usage and dependency details](docs/media.md).

### Kernels

```lisp
;; kernels/color/brightness.jolt
(defkernel brightness [r g b a amount]
  (rgba
    (min a (max 0 (* r amount)))
    (min a (max 0 (* g amount)))
    (min a (max 0 (* b amount)))
    a))
```

```bash
# Compile with either compiler; the output is byte-identical.
zoltan/target/debug/zoltan compile kernels/color/brightness.jolt -o glow.jbc
build/frontends/cli/joltfx compile kernels/color/brightness.jolt -o glow.jbc
scripts/check-bytecode-parity.sh build   # prove they agree
```

## Build configuration

| Option | Default | Effect |
|---|---|---|
| `JFX_BACKEND_VULKAN` | ON | Vulkan backend |
| `JFX_BACKEND_METAL` | ON | Metal backend (CPU path off-platform) |
| `JFX_BACKEND_D3D12` | ON | D3D12 backend (CPU path off-platform) |
| `JFX_BACKEND_WEBGPU` | ON | WebGPU backend (CPU path off-platform) |
| `JFX_EXT_LUA` / `JFX_EXT_MRUBY` | ON | Extension runtimes |
| `JFX_EXT_QUICKJS` / `JFX_EXT_PYTHON` | OFF | Not implemented; a configure error if enabled |
| `JFX_FRONTEND_CLI` / `_DESKTOP` / `_WEB` / `_MOBILE` | ON | Frontends |
| `JFX_DESKTOP_WINDOW` | ON | Desktop window (needs SDL2 and OpenGL) |
| `JFX_PLUGIN_HOST_BRIDGES` | ON | After Effects / Premiere / DaVinci bridges |
| `JFX_VIDEO_FFMPEG` | ON | FFmpeg media decoding and encoded export when available |
| `JFX_MEDIA_FFMPEG_BUNDLED` | OFF | Build the vendored dependency-free FFmpeg profile |
| `JFX_ASAN` / `JFX_UBSAN` | OFF | Sanitizers |
| `JOLTFX_BUILD_TESTS` / `_EXAMPLES` | ON | Tests, examples |

A backend that is switched off is neither built nor offered by the engine, and
requesting it by name is rejected with an error listing what the build does
provide. At least one backend must be enabled.

Presets: `default`, `release`, `sanitizers`, `minimal`.

## Documentation

- `AGENTS.md` — architecture and contribution rules
- `docs/` — guides and references
- `docs/nle.md`, `docs/editor.md`, `docs/composition.md` — NLE, color and composition workflows
- `docs/media.md` — audio mixing, encoded export and media dependencies
- `PROGRESS.md` — what is implemented, what is verified, and what is not
- `CHANGELOG.md` — release history

## Status

Backends, the desktop editor, and the extension runtimes all have honest
limitations. `PROGRESS.md` records what is verified on which platform and what
remains, so the gap between the documentation and the code is explicit rather
than implied.

## License

See [LICENSE](LICENSE).
