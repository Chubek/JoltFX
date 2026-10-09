# Zoltan creative stdlib (0.3.0)

Sketches are single `(defkernel name [x y t ...] body)` programs. `x`/`y`
are normalized coordinates, `t` is seconds, optional `mx`/`my` are the
normalized pointer. Bodies must use only JBC1 scalar ops so `zoltan compile`
and `joltfx compile` stay byte-identical:

`+ - * / min max abs floor pow sqrt < > <= >= = != and or not
 bitwise-and bitwise-or bitwise-xor shl shr select`

Files in this directory are all valid sketches (`zoltan verify` passes).
Snippets from `zoltan stdlib list` are paste-ready body fragments.

| File | Description |
|---|---|
| `sketch_basic.jolt` | Animated gradient drift (scaffold output) |
| `sketch_shapes.jolt` | Disc + animated stripes via `select`/`floor` |
| `sketch_pointer.jolt` | Pointer-reactive mix with `mx`/`my` |

Exports: `zoltan export FILE --as obj` writes a minimal ELF64 with a `.jolt`
section (readable with `lief.parse(path)`, no LIEF build required);
`--as html` writes a standalone Canvas page; `--as wasm` writes WAT for
`wat2wasm`. Full Emscripten app bundling and Android packaging need a
configured emsdk / SDK and are documented as follow-ups, not claimed here.
