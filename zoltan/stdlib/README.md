# Zoltan creative snippets and example sketches

Sketches are single `(defkernel name [x y t ...] body)` programs. `x`/`y`
are normalized coordinates, `t` is seconds, optional `mx`/`my` are the
normalized pointer. C and Zoltan emit identical bytecode for both legacy f32
programs and typed programs with loops, functions, closures and standard-library
imports. Preview accepts floating inputs and numeric scalar/RGBA output.

Files in this directory are all valid sketches (`zoltan verify` passes).
Snippets from `zoltan stdlib list` are paste-ready body fragments.
The six importable modules (math, graphics, geometry, time, collections, strings)
live in [`joltscript/stdlib`](../../joltscript/stdlib/), with the complete API in
the [standard-library reference](../../joltscript/docs/stdlib.md).

| File | Description |
|---|---|
| `sketch_basic.jolt` | Animated gradient drift (scaffold output) |
| `sketch_shapes.jolt` | Disc + animated stripes via `select`/`floor` |
| `sketch_pointer.jolt` | Pointer-reactive mix with `mx`/`my` |

Exports: `zoltan export FILE --as obj` writes a minimal ELF64 with a `.jolt`
section (readable with `lief.parse(path)`, no LIEF build required);
`--as html` writes a standalone Canvas page; `--as wasm` writes WAT for
`wat2wasm`. HTML/WAT support version 1 and reject typed version 2 before writing;
object export supports both. Full Emscripten app bundling and Android packaging need a
configured emsdk / SDK and are documented as follow-ups, not claimed here.
