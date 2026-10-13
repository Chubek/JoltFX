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

Bodies support bindings, mutation, sequences and lazy conditionals. Typed JBC1
version 2 adds exact primitives, loops, functions/closures, collections and the
six built-in modules: `jolt.math`, `jolt.graphics`, `jolt.geometry`, `jolt.time`,
`jolt.collections`, `jolt.strings`. Imports select the typed CPU runtime.
Preview inputs must be floating-point and outputs numeric (one grayscale value
or four RGBA lanes). Intermediate strings and collections are allowed; exact
aggregate output is available through `jolti` and the tagged-value API.
See the [language](../joltscript/docs/language.md) and
[standard-library](../joltscript/docs/stdlib.md) references.

```lisp
;; @kernel pulse
;; @category creative
;; @description Typed standard-library animation
;; @complexity Low
;; @gpu No
;; @since 0.9.0
(import jolt.math (sin))
(defkernel pulse [x y t]
  (let intensity (+ 0.5 (* 0.5 (sin t))))
  (rgba (* x intensity) (* y intensity) intensity 1.0))
```

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
`sketch_shapes`, `sketch_pointer`); `zoltan stdlib` prints creative body snippets.
The importable language modules live in `joltscript/stdlib/`. The object wrapper is an interchange
format, not native code: it carries the JBC1 payload in an ELF `.jolt`
section and can be inspected with `lief.parse(path)` without building LIEF.
Zoltan HTML export lowers version-1 operations, bindings and lazy conditionals
to JavaScript; WASM export writes WAT (assemble with `wat2wasm` when wabt is
installed). Both reject typed version 2 before replacing the destination;
object export preserves either bytecode version. Full Emscripten app
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
320x180 with cached rasters. Use plain `[x y t mx my]` input names in the desktop
pad; its lightweight binding scanner does not handle annotated parameter lists.
Its syntax-color preview and export helpers are separate from Zoltan's Rust
tooling: use `zoltan export` for validated version-aware HTML/WAT export and
typed object export. Opening a `.jolt` file loads it into the sketch buffer and
selects the tab.

Limits: no kernel registration, no GPU dispatch, no direct resource
management from sketches; for those, write a plugin via the Plugin API.
