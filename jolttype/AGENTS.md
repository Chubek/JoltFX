# JoltType

The C++17 `jolttype` library is independent of ImGui and GPU backends. All owned
strings/vectors use Tilly storage. Version 0.5 is a bounded, declarative page
language, not a TeX interpreter. See README.md for the exact syntax and limits.

Keep compilation, edits, history and file replacement failure-atomic. Drafts
and committed revisions are distinct; visual edits patch argument source spans
so comments and untouched markup survive. Never export an uncommitted layout.
Tests live in tests/unit/test_jolttype.cpp; desktop integration has real-widget
coverage in tests/unit/frontends/test_page_design_ui.cpp.

Pagelets declare data components separately from presentation configuration.
Kliche definitions accept only declared configuration overrides, never instance
data. The layout system resolves entities to drawing elements while retaining
metadata/source spans for editing; generated spans cannot be patched as primitive
source. Enforce import/expansion budgets and cycles. Session::refresh is a draft
preview operation, independent of Commit/history. Frozen JTBC/source exports use
committed layouts. Fonts/Pagelet/Kliche assets round-trip through the inventory.

Bundled Pagelets live in `pagelets/*.jtype`, with metadata in
`pagelets/catalog.cmake`. Those files generate the embedded C++ catalog and ship
as editable resources; do not duplicate their markup in frontend code. Preserve
document-owned inventory edits when inserting templates. Extend
`test_jolttype_pagelets` for catalog defaults, independent component/config edits,
page fit, publishing and Typelet/JTBC round-trips.

Final PDF publishing defaults to libharu; PoDoFo is the explicit `pdf-podofo`
alternative and the Progress Display generator. Both use qpdf validation;
PDFium rasterizes previews. Preserve committed
snapshot/revision ownership across scheduler jobs. Typelets are bounded zstd
archives with KSON-deserialized manifests and structured compiler steps. Module
compilation uses Binaryen; execution belongs to the shared metered WAMR host on
its serialized owner thread. Keep vendor types out of `api.h`, which is the
canonical C SDK contract. Extend publishing/project/API and real-widget tests.
