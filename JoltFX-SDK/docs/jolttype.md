# JoltType target and frontend SDK

Include `<joltfx/sdk/jolttype.h>` and link `JoltFXSDK::jolttype` for headers.
Implementations come from `JoltFX::jolttype` in the full engine installation.
The standalone SDK does not build PDF, Binaryen or WAMR runtimes.

Create a project with `jt_project_create`, stage source/assets, then commit.
Export rejects dirty drafts. `jt_project_document` returns an owned immutable
snapshot: release it with `jt_document_destroy`. Project strings/asset/interface
views are borrowed until successful mutation. Every handle is single-owner;
module calls share the extension runtime's serialized owner thread. Callbacks
must not reenter or destroy their active project. No vendor or ImGui types appear
in the C11/C++17 header.

API 1.2 adds `jt_project_refresh` and `jt_project_preview_document` for draft-only
live preview. Refresh retains the previous preview on errors and never commits.
Use `jt_document_entity_count/entity/component` and `jt_document_element_entity`
to inspect Pagelet identity, independent data components and configuration.
`jt_project_set_component` edits an instance; Commit remains explicit. Kliche
source definitions accept configuration only, never data-component overrides.
`JT_FONT`, `JT_PAGELET` and `JT_KLICHE` append inventory kinds without renumbering
older values. Fonts are packaged resources, not a font-selection/typesetting API.

## New output targets

Built-in target `pdf` uses libharu for final output; `pdf-podofo` explicitly
selects the retained PoDoFo exporter. Both produce multi-page PDF and validate
through qpdf. The desktop's Progress Display continues to use PoDoFo/PDFium.
Libharu embeds a TrueType font and supports BMP Unicode in its UTF-8 profile;
supplementary characters require the alternative exporter.
Targets `jtbc` and `jtype` freeze the committed layout and component contents as
bytecode or ordinary JoltType elements. JTBC embeds images; frozen source retains
image inventory references. Editable schemas remain in the original source/Typelet.

Fill `jt_target_desc` (`size`, ABI major 1, name, extension, userdata, `emit`) and
call `jt_project_register_target`. Built-in names cannot be replaced. The emitter
receives a document snapshot, selected page and caller sink. Enumerate pages,
elements and lines with `jt_document_*`; images expose borrowed RGBA8 pixels.
Emit bytes through `sink.write`, propagate rejection, and return `JT_OK` only on
success. Callback code/userdata must outlive the project. The callback is
synchronous; put jobs on your frontend's scheduler. For file output, collect bytes
first and publish a sibling temporary atomically; sinks may already have received
partial output when an emitter fails.

## Module-backed controls

Enumerate `jt_project_interface_count/interface/interface_parameter` to generate
frontend controls. `jt_project_invoke` resolves the manifest's module and export,
checks argument count and runs its scalar WASM function. `jt_frontend_host` may
provide a custom executor and an `action_result` handler that integrates the
result with the frontend. Omit the executor to use metered WAMR. A browser can
provide its WebAssembly host; this API does not itself create browser widgets.
Use `jt_has_pdf`, `jt_has_pdfium`, `jt_has_modules` to query this runtime's support.

Module source, manifest schema, build flags, budgets, target limitations and
Typelet format are specified in `jolttype/README.md`. PDF/SVG accept Unicode;
PostScript/DVI currently have an ASCII text profile. DVI does not embed images.
