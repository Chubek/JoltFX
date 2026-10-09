# Creative programming with Zoltan sketches

A Zoltan sketch is one `(defkernel name [x y t ...] body)` JBC1 program with
a creative convention on top: `x`/`y` are normalized coordinates, `t` is
seconds, and optional `mx`/`my` are the normalized pointer. Because a sketch
is still a plain kernel, `zoltan compile` and `joltfx compile` emit
byte-identical bytecode (see `scripts/check-bytecode-parity.sh`), and the
desktop tab executes exactly that bytecode.

Optional sketch metadata (all `;; @key value` comments):

| Key | Meaning | Default |
|---|---|---|
| `@canvas` | `WIDTHxHEIGHT`, bounded to 320x180 | `320x180` |
| `@fps` | Preview rate in (0, 120] | `30` |
| `@duration` | Seconds in (0, 600] | `10` |

Bodies may only use the JBC1 scalar operations both compilers implement:

`+ - * / min max abs floor pow sqrt < > <= >= = != and or not
 bitwise-and bitwise-or bitwise-xor shl shr select`

## CLI

```sh
zoltan new my_sketch            # scaffold my_sketch/sketch.jolt
zoltan verify sketch.jolt       # validate only
zoltan compile sketch.jolt -o sketch.jbc
zoltan run sketch.jolt --frames 4 --width 64 --height 36
zoltan stdlib list              # paste-ready body fragments
zoltan stdlib show circle
zoltan export sketch.jolt --as obj -o sketch.o     # minimal ELF64, .jolt section
zoltan export sketch.jolt --as html -o sketch.html # standalone Canvas page
zoltan export sketch.jolt --as wasm -o sketch.wat  # WAT for wat2wasm
```

`zoltan/stdlib/` holds valid example sketches (`sketch_basic`,
`sketch_shapes`, `sketch_pointer`). The object wrapper is an interchange
format, not native code: it carries the JBC1 payload in an ELF `.jolt`
section and can be inspected with `lief.parse(path)` without building LIEF.
HTML export lowers the basic scalar ops to JavaScript; WASM export writes
WAT (assemble with `wat2wasm` when wabt is installed). Full Emscripten app
bundling needs a configured emsdk and Android packaging needs an SDK; both
are documented follow-ups, not claimed here.

## Desktop tab

Open **Creative Programming**: the code pad sits beside the result view, and
each side collapses independently via **Show code** / **Show preview**. The
read-only **Syntax colors** strip is a hand-rolled tokenizer preview (parens,
keywords, numbers, comments); editing stays in the text box. **Run**
validates, **Animate** advances `t`, and the `t`/`mx`/`my` sliders plus
canvas size feed the next render. Preview compiles with the Glue
`jolt_compile` and evaluates per pixel with `jolt_vm_run`, bounded to
320x180 with cached rasters. **Export object (.o) / HTML / WASM (WAT)** write
through the same code as the CLI exporters. Opening a `.jolt` file loads it
into the sketch buffer and selects the tab.

Limits: no kernel registration, no GPU dispatch, no direct resource
management from sketches; for those, write a plugin via the Plugin API.
