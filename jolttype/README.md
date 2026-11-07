# JoltType 0.5

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
| `\pagelet{name}{body}` | Reusable JoltType element definition |
| `\kliche{name}{base}{configuration}` | Configuration-only Pagelet variant |
| `\place{id}{pagelet-or-kliche}{x}{y}{values}` | Create a Pagelet entity |
| `\inline{id}{x}{y}{body}` | Define and place an anonymous Pagelet |
| `\use{asset-id}` | Import an inventory Pagelet/Kliche/source library |

Elements paint in source order. IDs are unique across the document, start with
an ASCII letter, and contain 1–63 letters, digits, underscores or hyphens.
Colors are `#RRGGBB`. Lengths accept decimal/scientific numbers and `pt`, `mm`,
`cm`, `in`; a number without a suffix is in points (72 points per inch).
Numbers are locale-independent. Coordinates are nonnegative, page sizes are
10–14400pt, and type sizes are 4–240pt. Frames must fit wholly on their page.

`%` starts a comment **outside arguments**. Inside a text argument it is literal.
Escape backslashes and braces with `\\`, `\{`, `\}`; `\%` is also accepted.
Nested markup is accepted in Pagelet/Kliche/instance blocks; text arguments
still require escaped braces. Unknown commands and other escapes produce diagnostics with
1-based line/byte-column positions. Literal newlines create line breaks; CRLF
and CR normalize to LF, and tabs become one space.

### Pagelets, components and Kliches

Pagelets are design modules written in JoltType, such as titles, tables and
compound layouts. Their ECS-style **entities** have stable document-local IDs,
independent data **components**, and presentation **configuration**. The layout
system resolves these values into ordinary drawing elements; all publishing
targets use the same resulting geometry. A named definition is a reusable
schema, not a shared mutable instance.

```tex
\jolttype{1}
\pagelet{Table}{
  \component{header}{Item                 Value}
  \component{rows}{First                100
Second               200}
  \config{ink}{#182840}
  \config{separator}{24}
  \text{heading}{6}{4}{288}{12}{@ink}{@header}
  \rect{first}{0}{24}{300}{1}{@ink}
  \rect{second}{0}{@separator}{300}{1}{@ink}
  \text{body}{6}{38}{288}{12}{@ink}{@rows}
}
% Move the second rule down to make a double-line header separator.
\kliche{DoubleRuleTable}{Table}{\config{separator}{28}}
\page{600}{800}
\place{prices}{DoubleRuleTable}{30}{50}{
  \component{header}{Product              Price}
  \component{rows}{Print                25
Poster               40}
}
\inline{title}{30}{20}{
  \component{content}{Price list}
  \text{label}{0}{0}{300}{20}{#182840}{@content}
}
```

Declare `\component{name}{default}` and `\config{name}{default}` at the start
of a Pagelet body. A whole argument `{@name}` substitutes a declared value;
values are escaped as data and cannot inject markup. No expressions, loops or
partial-string interpolation are implied. Bodies compose `text`, `rect`,
`image`, `textfile` and nested `place` elements in local coordinates. In an
inline body, declare its own values before its drawing commands.

`place` supplies optional component/config assignments of the declared category.
Omitted values come from the definition. **Kliche definitions accept only
`config` assignments**; assigning a data component, inventing a parameter or
changing its category is an error. A Kliche inherits its base's data schema and
defaults, but cannot capture instance contents. Kliches may derive from earlier
Kliches. Export a committed document as frozen `.jtype` or `.jtbc` when contents
must be fixed along with the presentation. Frozen source contains ordinary
elements; JTBC contains laid-out text and embedded images, without editable
Pagelet schemas. Image references in frozen source still need the inventory.

Definitions precede use, with unique names in a separate definition namespace.
Instance and primitive IDs share a document namespace. Generated drawing IDs
are `instance-local` (nested IDs are similarly prefixed), still limited to 63
characters. Nested drawings belong to their outer placed entity for visual
group moves; edit nested composition in source. Moving an entity or editing a
component patches only that instance's argument, preserving other source/comments.
Changing a definition rebuilds all of its instances on the next preview/commit.
Even unused definitions are validated with their defaults.

Libraries start with `\jolttype{1}` and contain only definitions and `use`
imports. They load from the shared inventory, never from arbitrary filesystem
paths. Repeated imports are idempotent; cycles fail. Limits are 128 definitions,
64 component/config slots per Pagelet, 16 expansion/import levels, 64 markup
nesting levels, 1 MiB cumulative library/expansion input and 1024 final drawing
elements. A Pagelet must produce at least one element and cannot create pages.

### Typesetting profile

Version 0.5 uses a deterministic **fixed-em** profile: one cell per Unicode
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
    Its page preview takes one third of the workspace width; **Hide page preview**
    / **Show page preview** changes visibility, independently of **Auto-update
    preview**. Live compilation runs after a 200 ms typing pause, including while
    hidden. **Update preview** refreshes manually when auto-update is off. Invalid
    drafts retain the last valid page with a diagnostic. Preview updates never
    publish history, save files or replace the committed revision.
   **Current page code** shows the selected committed page's markup. A released
   drag changes the draft source and preview. Escape or a tab change cancels an
   in-progress drag. The inspector provides an editable **Frame text** field.
3. Press **Commit** to validate and publish one revision. Source edits update
    the page on commit; IDE live updates can compile the preview sooner. Visual
    editing is disabled while source needs compiling.
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

**Elements / Pagelets → Insert Pagelet** offers 32 Pagelets and the DoubleRuleTable
Kliche, plus definitions loaded from the document/inventory. Search the picker by
name, category or description; the selected entry shows a brief description.
Select an entity on the page to edit its components or configuration. **Create
Kliche** stores a reusable, configuration-only library in the inventory; its
base retains definition defaults, never the selected instance's data. **New
Pagelet** in the inventory creates an editable module using the Dependency ID.
Imported Pagelet/Kliche assets offer **Load design library** and a source editor.
An inline Pagelet must be named in source before capturing a reusable Kliche.

### Shipped Pagelet catalog

Version 0.5 adds **30 Pagelets** to the original Title, Table and DoubleRuleTable
catalog. These are actual editable JoltType modules, with independent content
components and presentation configuration suitable for Kliche capture.

| Category | New Pagelets |
|---|---|
| Typography (6) | Subtitle, SectionHeading, Paragraph, PullQuote, DropCap, Byline |
| Navigation (5) | PageHeader, PageFooter, ContentsList, Breadcrumbs, ChapterOpener |
| Editorial (5) | Callout, Sidebar, Caption, FactBox, Footnote |
| Business (6) | ContactCard, AddressBlock, InvoiceHeader, InvoiceTotals, PriceCard, SignatureBlock |
| Data (4) | KeyValueList, ComparisonTable, Checklist, Timeline |
| Layout (4) | Divider, TwoColumnText, ThreeColumnText, FigurePlaceholder |

All defaults fit A4 and US Letter at the insertion position (20pt, 20pt), use
the standard fixed-em profile, and require no external assets. Multi-row and
multi-column modules have explicit editable slots. Page numbers, contents
references, dates and invoice totals are supplied text, not automatic numbering
or calculated values. Checklist boxes are printable shapes; FigurePlaceholder
is a placeholder, not an image importer. Edit module source to change its
structure or accommodate longer copy; these templates do not auto-paginate.

Sources live in `jolttype/pagelets/*.jtype`. CMake embeds those exact files in
the library and installs them under `share/joltfx/jolttype/pagelets` (component
`JoltTypePagelets`); the application does not depend on runtime file paths.
Insertion copies only the required module into the document's inventory, using
its catalog asset ID. Existing document-owned source edits are preserved. Save a
Typelet to retain these inventory dependencies with the document.

Other C++ frontends can include `jolttype/pagelets.hpp` and enumerate
`bundled_pagelets(count)`. Entries supply a name, category, description, inventory
asset ID and source with process lifetime. Copy the source into a Pagelet asset,
then use `\use{asset-id}` and `\place{id}{name}{x}{y}{values}` as usual. The
original three entries share `design-elements`; every new Pagelet has its own
standalone module. Catalog definitions use the same compiler as user modules.

**Add text**, **Add rectangle**, **Add page** and Pagelet insertion update the
draft and preview; commit to publish. Frames append to the final source page. Use
source editing to adjust size, color, page placement or remove elements. The page
selector, zoom control and scrollable canvas navigate a multi-page document.
The canvas fills its assigned width at 1x while retaining the page's aspect ratio;
scroll vertically for long pages. Edit mode keeps the tools at their 420px default
width, and gives the rest of the workspace to the page.

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

`compile`, `export_source` and `export_svg` replace outputs only on success. The structured
document exposes pages, frames, source spans and laid-out lines. SVG export
expects a compiler-produced document. `Session` owns separate draft and committed
source/layouts, failure-atomic commits, source-preserving frame/text edits,
history, loading and committed-source saving. `refresh` compiles only the preview;
`set_component` edits one entity and `move` accepts its entity ID for group moves.
`Document::pagelets` and `entities` retain definition/component metadata alongside
the flattened drawing elements. Its timestamps use a caller-provided
monotonic seconds clock. Session operations are single-owner-thread calls;
independent snapshots may be processed on the scheduler.

Library-owned strings/vectors use Tilly's MemTKX-backed allocator. Libharu, KSON
and zstd use Tilly allocation adapters. Other upstream PDF/compiler libraries
manage their own internal allocations. The desktop uses existing ImGui and
preference code.

```sh
cmake --build build --target test_jolttype test_jolttype_pagelets test_page_design_ui jfx_desktop
ctest --test-dir build -R '^(jolttype|jolttype_pagelets|page_design_ui_tests|preferences_ui_tests)$' --output-on-failure
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
ID, choose a kind, filter by name/path/tag/kind, edit tags and dependency IDs, edit text
or code, remove unused assets, and insert images/text into the document. Changes
require preview refresh or Commit to rebuild the page. Images, fonts, text, code,
Pagelet libraries and Kliche libraries share this inventory. Font assets are
stored/tagged/packaged dependencies; selecting one does not change the current
fixed-em target font. A Typelet saves the inventory; `.jtype` alone
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
`source`, `compiled`, `module`, `other`, `font`, `pagelet`, `kliche`. Build targets: `pdf`, `svg`, `ps`, `dvi`,
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

The canonical C API 1.2 is `jolttype/api.h`, re-exported by SDK 1.3 as
`joltfx/sdk/jolttype.h`. It provides project/source/history/hatch operations,
inventory and manifests, package build/import/export, immutable document snapshots,
draft preview refresh/snapshots, entity/component inspection and edits, frozen
`jtbc`/`jtype` export, custom target callbacks, module compilation/execution and frontend-neutral action
descriptors. A frontend may supply a WASM executor/result callback (for example a
browser host) instead of WAMR. Widgets remain frontend-owned; the desktop adapter
is implemented here. See `JoltFX-SDK/docs/jolttype.md` for target integration.
