# joltfx CLI

Zero-GUI-dependency command-line frontend over the Core engine. Builds on all
platforms.

```bash
cmake --preset default
cmake --build build --target joltfx_cli
./build/frontends/cli/joltfx --help
```

## Commands

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

## Not implemented

There is no `.joltpkg` container, packaging, or signature verification, and no
video container output: `render` and `export` write binary PPM. The FFmpeg
wiring described in `frontends/AGENTS.md` does not exist.

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
