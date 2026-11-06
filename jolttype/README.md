# JoltType 0.3

JoltType is JoltFX's C++17 page-design language and document library. Its first
format is a small, LaTeX-inspired declarative language for multi-page layouts.
The **Page Design** desktop tab uses this same compiler for source editing,
visual frame edits, committed revisions, PDF preview and publishing.

## Language

Save UTF-8 source with the `.jtype` extension:

```tex
\jolttype{1}
% A4 portrait. Positions are measured from the upper-left corner.
\page{210mm}{297mm}
\text{title}{20mm}{20mm}{170mm}{28pt}{#182840}{Page Design}
\rect{accent}{20mm}{38mm}{170mm}{3pt}{#e46b36}
\text{body}{20mm}{50mm}{170mm}{12pt}{#303030}{Welcome to JoltType.
Text wraps inside this frame.}

% Each page command starts another page.
\page{8.5in}{11in}
\text{second}{1in}{1in}{6.5in}{18pt}{#202020}{A second page}
```

| Command | Arguments |
|---|---|
| `\jolttype{1}` | Required version header, exactly once at the start |
| `\page{width}{height}` | Start a page; following elements belong to this page |
| `\text{id}{x}{y}{width}{size}{color}{text}` | Text frame, height calculated from wrapped lines |
| `\rect{id}{x}{y}{width}{height}{color}` | Filled rectangle |
| `\image{id}{x}{y}{width}{height}{asset-id}` | PNG/JPEG from the inventory |
| `\textfile{id}{x}{y}{width}{size}{color}{asset-id}` | UTF-8 text/code inventory asset |

Elements paint in source order. IDs are unique across the document, start with
an ASCII letter, and contain 1–63 letters, digits, underscores or hyphens.
Colors are `#RRGGBB`. Lengths accept decimal/scientific numbers and `pt`, `mm`,
`cm`, `in`; a number without a suffix is in points (72 points per inch).
Numbers are locale-independent. Coordinates are nonnegative, page sizes are
10–14400pt, and type sizes are 4–240pt. Frames must fit wholly on their page.

`%` starts a comment **outside arguments**. Inside a text argument it is literal.
Escape backslashes and braces with `\\`, `\{`, `\}`; `\%` is also accepted.
Nested markup, unknown commands and other escapes produce diagnostics with
1-based line/byte-column positions. Literal newlines create line breaks; CRLF
and CR normalize to LF, and tabs become one space.

### Typesetting profile

Version 0.3 uses a deterministic **fixed-em** profile: one cell per Unicode
scalar, a 0.6-em advance, and 1.2-em line height. Wrapping prefers spaces and
breaks oversized words at scalar boundaries. It does not flow text across pages;
overflow is an error. The desktop uses ImGui's available font glyphs at these
advances; SVG uses a monospace font with explicit line widths. Font appearance
depends on the viewer. Final PDF uses **libharu by default**, embeds a system
monospace TrueType font and positions individual scalars at the same advances.
The existing PoDoFo font resolver supplies the font bytes to libharu; libharu
owns PDF drawing and serialization. **PDF (PoDoFo alternative)** retains the
previous exporter. Complex-script shaping,
bidirectional text, rich inline styles, automatic pagination and TeX macros/math
are not part of this version. Missing glyphs depend on the selected system font.

Inputs are limited to 64 KiB, 128 pages, 1024 elements and 4096 lines per frame.
Malformed UTF-8, NUL/control characters, duplicate IDs, non-finite dimensions and
overflow are rejected. Compilation never executes commands or loads files/fonts.

## Page Design workflow

1. Select **Page Design** in the desktop workspace tabs.
2. Use **Hatch into JoltType** to edit the visible source, or select and drag a
   frame on the white page. **Restore hatch** restores the source at hatch entry,
   even after intervening commits; restored changes still require Commit.
   **IDE Mode** enables direct editing and adds Compile and Package tools.
   **Current page code** shows the selected committed page's markup. A released
   drag changes the draft source and preview. Escape or a tab change cancels an
   in-progress drag. The inspector provides an editable **Frame text** field.
3. Press **Commit** to validate and publish one revision. Source edits update
   the page on commit; visual editing is disabled while source needs compiling.
   Invalid source or overflowing text leaves the published revision intact and
   displays a diagnostic. **Revert draft** discards pending changes.
4. **File → Preferences → Page Design → Auto-commit interval (seconds)** controls
   automatic publication. Default: **60**; **0** disables it; maximum: **3600**.
   The timer starts with the first dirty edit, also runs off-tab, defers during
   mouse gestures/file jobs, and retries a failed compile after another interval.
   **Save Preferences** persists the setting independently of documents.
5. **Files…** opens/saves `.jtype` source and exports PDF, SVG, PostScript or DVI.
   **PDF (libharu)** is the default final output; **PDF (PoDoFo alternative)**
   explicitly selects the previous implementation.
   SVG exports the selected page; the other targets contain all pages.
   Commit or revert before file operations. File I/O uses scheduler-owned
   snapshots and atomic sibling-file replacement. Commit and auto-commit update
   the document in memory; they do **not** save to disk.

**Add text**, **Add rectangle** and **Add page** insert markup into the draft;
commit it to update the preview. Frames append to the final source page. Use
source editing to adjust size, color, page placement or remove elements. The page
selector, zoom control and scrollable canvas navigate a multi-page document.

Each successful commit is one independent undo step (32 retained revisions).
Global Undo/Redo and Ctrl/Cmd-Z, Ctrl/Cmd-Shift-Z or Ctrl-Y route to this document;
focused text fields retain their local text undo. Commit or revert pending drafts
before document undo/redo. Reopening a valid file is undoable. `.jtype` files also
open through the desktop's project API/startup project path.

## C++ library

Link the CMake target `jolttype` and include `jolttype/jolttype.hpp`:

```cpp
jolttype::Document document;
jolttype::Diagnostic error;
if (jolttype::compile(source, document, error)) {
    tilly::string svg;
    if (jolttype::export_svg(document, 0, svg, error))
        jolttype::write_file("page.svg", svg, error);
}
```

`compile` and `export_svg` replace outputs only on success. The structured
document exposes pages, frames, source spans and laid-out lines. SVG export
expects a compiler-produced document. `Session` owns separate draft and committed
source/layouts, failure-atomic commits, source-preserving frame/text edits,
history, loading and committed-source saving. Its timestamps use a caller-provided
monotonic seconds clock. Session operations are single-owner-thread calls;
independent snapshots may be processed on the scheduler.

Library-owned strings/vectors use Tilly's MemTKX-backed allocator. Libharu, KSON
and zstd use Tilly allocation adapters. Other upstream PDF/compiler libraries
manage their own internal allocations. The desktop uses existing ImGui and
preference code.

```sh
cmake --build build --target test_jolttype test_page_design_ui jfx_desktop
ctest --test-dir build -R '^(jolttype|page_design_ui_tests|preferences_ui_tests)$' --output-on-failure
```

## Progress Display and publishing

**Show Progress Display** opens a resizable third pane; its header also collapses.
Successful commits queue immutable PDF snapshots on the engine scheduler.
For this preview, PoDoFo typesets, qpdf parses/rewrites the PDF, and PDFium rasterizes the selected
page. Stale worker results never replace a newer revision. Invalid drafts leave
the previous published output visible. **Hide Progress Display** removes the pane.

Native builds default `JFX_JOLTTYPE_PDF` and `JFX_JOLTTYPE_MODULES` to ON; cross
builds default them OFF. CMake builds vendored PoDoFo, qpdf and Binaryen in isolated
ExternalProjects, alongside vendored **libharu** (with required libpng and zlib).
PoDoFo needs CMake 3.23+, FreeType, Fontconfig, OpenSSL, zlib and
libxml2; Binaryen needs a C++20 compiler (JoltType itself remains C++17).
The existing allocator-aware, instruction-metered WAMR adapter is shared with
extension runtimes. zstd, KSON and the PNG/JPEG decoder come from `third_party`.

PDFium's checkout contains headers/source but lacks Chromium's GN/dependency
tree. Build its shared library separately following its README, then configure:

```sh
cmake -S . -B build -DJFX_JOLTTYPE_PDFIUM_LIBRARY=/path/to/libpdfium.so
```

`JOLTTYPE_PDFIUM_LIBRARY` overrides that path at runtime. Otherwise the platform
loader searches for `libpdfium.so`, `libpdfium.dylib` or `pdfium.dll`. If unavailable,
PDF export still works and the pane reports the missing runtime. Preview is
serialized and bounded to 2048 pixels per dimension; the GUI requests 1000.

Final export and Typelet target `pdf` use libharu; target `pdf-podofo` selects
PoDoFo. The C++ API uses `Target::PDF` and `Target::PDFPoDoFo`, respectively;
`jt_project_export` accepts the same target strings as Typelets. Both final
exporters validate/rewrite with qpdf and preserve the previous output on failure.
The libharu adapter uses Tilly allocation callbacks and its embedded TrueType
UTF-8 profile supports BMP characters (U+0000–U+FFFF, excluding surrogates).
Supplementary characters produce a diagnostic directing users to PoDoFo rather
than silently replacing glyphs. Progress Display always uses the PoDoFo path.

PostScript uses Courier and ASCII text, supports opaque images, and rejects alpha.
DVI version 2 uses `cmtt10` (a TeX installation supplies the font), explicit
positioning, and dvips color/papersize specials. It supports ASCII text and rules;
bitmap images are rejected. PDF and SVG support Unicode text and PNG/JPEG images.

## Shared inventory and Typelets

**Dependency inventory** is identical in Edit and IDE. Import files with a unique
ID, choose a kind, filter by name/path/tag, edit tags and dependency IDs, edit text
or code, remove unused assets, and insert images/text into the document. Changes
require Commit to rebuild the page. A Typelet saves the inventory; `.jtype` alone
saves only source. Images are limited to 4096 per dimension and 4 megapixels each;
decoded page images total at most 64 MiB. Inventory limits are 128 assets,
16 MiB per asset and 64 MiB total. Missing dependencies/cycles are errors.

**Typelet tools** (or IDE **Package tools**) edits metadata, adds structured build
steps, exposes the MANIFEST editor, builds dependencies and saves/opens `.typelet`.
The format is one zstd frame containing `JTLET1\0\0`, a little-endian u32 entry
count, then length-prefixed UTF-8 paths and byte payloads. The first entry is
`MANIFEST`; all other entries must match its dependency table exactly. Paths are
relative and never extracted to disk. Unknown, duplicate or trailing entries,
oversized decompression and concatenated frames are rejected atomically.

MANIFEST uses a strict JSON subset deserialized by Klib **KSON**. Schema 1:

```json
{
  "schema": 1, "name": "Newsletter", "version": "1.0.0",
  "author": "Designer", "description": "A reusable layout",
  "kind": "document", "entry": "main",
  "dependencies": [
    {"id":"main", "path":"main.jtype", "kind":"source", "tags":["layout"], "requires":[]}
  ],
  "compile": [{"input":"main", "output":"layout.jtbc", "target":"jtbc"}],
  "interfaces": []
}
```

Kinds: `document`, `module`, `mixed`. Dependency kinds: `image`, `text`, `code`,
`source`, `compiled`, `module`, `other`. Build targets: `pdf`, `svg`, `ps`, `dvi`,
`jtbc`, `wasm`, `pdf-podofo`. Steps execute in declared order, in a candidate inventory, and
publish together only on success. They are compiler operations, not shell commands.
JTBC1 stores validated page geometry, text lines and embedded images. Manifest
limits: 64 KiB, 24 nesting levels, 4096 nodes, 32 build steps, 64 interfaces.

## Code modules and frontend API

**New code module** creates a source asset, WASM build step and example action.
Edit the asset, Commit, then **Build dependencies**. Module source is a bounded
scalar expression language, compiled to real WASM by Binaryen:

```tex
\module{measure}
\export{area}{width,height}{width*height}
\export{diagonal}{width,height}{sqrt(width*width+height*height)}
```

Expressions accept finite f64 literals, parameters, parentheses, `+ - * /`,
`sqrt`, `abs`, `floor`, `ceil`, `min`, `max`. There are at most 64 exports and
16 parameters per export. Modules do not expose general TeX or C++ semantics.
WAMR loads at most 1 MiB of WASM with a 16 MiB memory budget and 100,000-instruction
invocation limit. No WASI or automatic start execution is enabled.

An interface entry such as
`{"id":"measure.area","module":"measure.wasm","export":"area","label":"Area","parameters":["width","height"]}`
creates numeric inputs and a **Run action** button under **Typelet module actions**.
Opening a package does not execute its modules. Native runtime lifecycle/calls
share the extension owner's serialized thread; background build jobs only compile.

The canonical C API is `jolttype/api.h`, re-exported by SDK 1.3 as
`joltfx/sdk/jolttype.h`. It provides project/source/history/hatch operations,
inventory and manifests, package build/import/export, immutable document snapshots,
custom target callbacks, module compilation/execution and frontend-neutral action
descriptors. A frontend may supply a WASM executor/result callback (for example a
browser host) instead of WAMR. Widgets remain frontend-owned; the desktop adapter
is implemented here. See `JoltFX-SDK/docs/jolttype.md` for target integration.
