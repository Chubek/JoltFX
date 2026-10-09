# joltfx CLI

Zero-GUI-dependency command-line frontend over the Core engine. Builds on all
platforms.

```bash
cmake --preset default
cmake --build build --target joltfx_cli
./build/frontends/cli/joltfx --help
```

## Commands

- `joltfx 3d new SCENE.jfx` — default 3D scene with a cube.
- `joltfx 3d edit IN.jfx OUT.jfx` — terminal/piped `3d.*` mesh, generator, cloner,
  quaternion camera, key, Joltscript driver and physics edits.
- `joltfx 3d info SCENE.jfx` — scene/object/key state as JSON.
- `joltfx 3d render SCENE.jfx OUT.png [SECONDS]` — shaded PNG frame.
  Shared `export-video` also exports scene animation. See [3D usage](../../docs/modeling3d.md).
  `3d.script_file OBJECT CHANNEL 0 0 FILE.jolt` embeds a driver from a file;
  `3d.script OBJECT CHANNEL 0 0 SOURCE` accepts inline Joltscript.

- `joltfx compose new OUT.jfx [--size W H]` — new Solid-source composition.
- `joltfx compose edit IN.jfx OUT.jfx` — terminal/piped node edits and history;
  `composition` prints state JSON, `nodes` prints the typed library.
- `joltfx compose info IN.jfx` — graph raster, nodes, layout, values, wires and output.
- `joltfx compose render IN.jfx -o OUT.ppm [--time S] [--node N] [--size W H]`
  — output/interior node preview and PPM export. See [the composition workflow](../../docs/composition.md).

- `joltfx nle new OUT.jfx [--size W H] [--fps NUM DEN]` — empty sequence with
  exact rational FPS.
- `joltfx nle edit IN.jfx OUT.jfx` — terminal or piped frame-accurate edits,
  color commands and undo/redo; `timeline` prints JSON state.
- `joltfx nle info IN.jfx` — raster, timing, tracks and clips as JSON.
- `joltfx nle render IN.jfx -o PREFIX [--start N --end N] [--width W --height H]`
  — PPM frames, start-inclusive/end-exclusive, with optional scaled output.
  See [the NLE workflow and command reference](../../docs/nle.md).

- `joltfx grade list`, `joltfx calibration list` — separate color operator catalogs.
- `joltfx grade apply KIND IN OUT [name=value ...] [--lut FILE]` — kernel-backed
  grading to PPM; `calibration apply` has the same syntax.
- `joltfx edit IN.jfx OUT.jfx [--plugin MODULE ...]` — terminal editor with section-local `grade.*` and
  `calibration.*` commands. See [examples and command reference](../../docs/editor.md).

- `joltfx compile FILE [-o OUTPUT]` — validate a `.jolt` kernel (MVP
  `defkernel` form); with `-o`, write the compiled JBC1 bytecode.
- `joltfx verify FILE` — validate only. Exit 0 when valid, 1 otherwise.
- `joltfx effects` — list the 12 bundled color kernels.
- `joltfx info EFFECT|FILE` — sample output for a bundled effect, kernel
  details for a source file, or the header and instruction count for a
  compiled `.jbc`.
- `joltfx render [--effect NAME] [--param VALUE] [--width W --height H]
  [-o OUTPUT.ppm] [--backend NAME]` — render a bundled effect over an animated
  gradient to binary PPM (P6). Defaults: `brightness`, 64x64, `render.ppm`.
- `joltfx export [--effect NAME] [--param VALUE] [--width W --height H]
  [--start N --end N] [-o DIRECTORY] [--backend NAME]` — render a frame range
  to one PPM per frame (`frame_%06d.ppm`) at 24 fps. Frames are inclusive;
  `--start` defaults to 0 and `--end` to 47.
- `joltfx capabilities` — report which shared frontend-contract operations this
  build provides, and which backend it resolved.
- `joltfx export-video DOC.jfx -o OUT.mp4|OUT.mov|OUT.mkv [--start FRAME --frames COUNT]`
  — shared encoded export with timeline audio, stderr progress and atomic output.
  Aliases: `export DOC.jfx`, `nle export DOC.jfx`, `compose export DOC.jfx`.
  Use `--codec`, `--audio-codec`, `--sample-rate`, `--width`, `--height` and
  `--no-audio` for explicit settings; graph exports require a frame count.
  The terminal editor supports `clip.audio.*` and `track.audio.gain` with history.
  See [media building and commands](../../docs/media.md).
- `joltfx version` / `joltfx help [COMMAND]` — version and help.
- `joltfx run` — legacy entry point: init the engine and tick once.

## Where the pixels come from

`render` and `export` do not have a private render path. They create the
headless frontend (`src/headless_frontend.c`), which implements the shared
frontend contract in `frontends/common/include/jfx_frontend.h`, and call
`jfx_frontend_render_frame` / `jfx_frontend_export_frames` on it. From there
the frame goes through `jfx_engine_execute_bytecode` to the backend the engine
resolved, which is what the GUI frontends use too.

`--backend` therefore selects the real execution path rather than labelling a
CPU render. The summary line reports the resolved device, so it is possible to
see which path actually ran:

```console
$ joltfx render --backend vulkan -o frame.ppm
rendered 64x64 brightness -> frame.ppm (backend vulkan, AMD Radeon RX 580 Series (RADV POLARIS10))
```

A backend this build does not contain is rejected by name, and the error lists
the ones it does provide.

## Plugin SDK

- `joltfx plugins inspect MODULE` — load a module and list metadata/actions.
- `joltfx plugins render MODULE IN.jfx OUT.ppm [FRAME]` — load before parsing,
  render an exact sequence frame (graph time is `FRAME/30`) and write PPM.
- `joltfx edit IN.jfx OUT.jfx --plugin MODULE` — load before editing; repeatable.
- `joltfx export-video IN.jfx -o OUT.mkv --plugin MODULE` — encode a plugin
  document using the same snapshot renderer; repeatable.

In the terminal editor, `plugins` lists loaded IDs, `plugin.load PATH` loads,
`plugin.unload ID` unloads and `plugin.action NAME TRACK CLIP NODE` invokes an
action with zero-based selection. Actions are transactional and produce one undo
step; referenced modules report busy on unload. See [the SDK guide](../../docs/plugins.md).

## Extension scripts

- `joltfx scripts list` — report enabled Lua, mruby, QuickJS, MicroPython and WAMR adapters.
- `joltfx scripts run LANG FILE [FUNCTION [NUMBER]]` — load source and optionally
  call a function; prints its typed result. WAMR accepts binary `.wasm`; compile WAT with `wat2wasm` first.
- `joltfx scripts edit LANG FILE IN.jfx OUT.jfx [FUNCTION]` — load the project,
  execute the script and optional zero-argument function, then save on success.
  Script failures preserve existing output.

```sh
build/frontends/cli/joltfx scripts run lua extif/examples/grade.lua gain 0.75
build/frontends/cli/joltfx scripts edit python extif/examples/grade.py \
  extif/examples/sequence.jfx edited.jfx edit
```

Languages are selected at build time. Scripts receive shared editor/event
capabilities and defaults of 4 MiB runtime memory and 100,000 execution units per
invocation; direct system modules/dynamic loading are excluded. Scripts use
`jfx.command/state`, `JFX.command/state` in mruby or `import jfx` in MicroPython.
See [the binding/ownership/budget guide](../../docs/extensions.md) and matching
examples in `extif/examples`. `ext_conformance_cli` runs all enabled examples and
checks grading pixels, sandbox/budget errors and project-output preservation.

## Not implemented

There is no `.joltpkg` container, packaging, or signature verification. The
legacy effect-only `render`/`export` routes write PPM; document-based video
export uses FFmpeg and reports its availability through `capabilities`.

## Exit codes

0 on success, 1 on usage or runtime failure (bad file, unknown effect,
invalid backend, failed write). Diagnostics go to stderr; normal output to
stdout.

## Tests

`tests/integration/cli/test_cli.sh` drives the built binary against the in-tree
kernels: help and version, compile/verify including missing and malformed
inputs, the full 12-kernel effects list, `info` for effects, source and
compiled bytecode, render error paths, P6 magic, a render of every bundled
effect, backend reporting, the accuracy of the capability report, the export
frame sequence, and the legacy `run`. It is registered as the `cli_integration`
CTest case.
