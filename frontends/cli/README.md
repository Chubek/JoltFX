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
- `joltfx info EFFECT|FILE` — sample output for a bundled effect, or
  kernel/bytecode details for a source file.
- `joltfx render [--effect NAME] [--param VALUE] [--width W --height H]
  [-o OUTPUT.ppm] [--backend NAME]` — render a bundled effect over a
  gradient to binary PPM (P6). Defaults: `brightness`, 64x64, `render.ppm`.
  `--backend` is validated by the engine (`vulkan`/`auto`); anything else is
  an error.
- `joltfx version` / `joltfx help [COMMAND]` — version and help.
- `joltfx run` — legacy entry point: init the engine and tick once.

`render` intentionally targets PPM: the MVP has no FFmpeg/Video-codec
wiring, so there is no MP4/MOV export yet. The pixels are the same validated
CPU path used by `jolt_effects_apply`; the engine init + tick exercises the
frame boundary and backend selection.

## Exit codes

0 on success, 1 on usage or runtime failure (bad file, unknown effect,
invalid backend, failed write). Diagnostics go to stderr; normal output to
stdout.

## Tests

```bash
sh tests/integration/cli/test_cli.sh ./build/frontends/cli/joltfx .
```

Covers help/version, compile/verify (good, missing, and broken kernels),
all 12 effects listing and rendering, info, render error paths, PPM magic,
and the legacy `run` command. Registered in CTest as `cli_integration`.
