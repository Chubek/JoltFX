# JoltType

The C++17 `jolttype` library is independent of ImGui and GPU backends. All owned
strings/vectors use Tilly storage. Version 0.3 is a bounded, declarative page
language, not a TeX interpreter. See README.md for the exact syntax and limits.

Keep compilation, edits, history and file replacement failure-atomic. Drafts
and committed revisions are distinct; visual edits patch argument source spans
so comments and untouched markup survive. Never export an uncommitted layout.
Tests live in tests/unit/test_jolttype.cpp; desktop integration has real-widget
coverage in tests/unit/frontends/test_page_design_ui.cpp.

Final PDF publishing defaults to libharu; PoDoFo is the explicit `pdf-podofo`
alternative and the Progress Display generator. Both use qpdf validation;
PDFium rasterizes previews. Preserve committed
snapshot/revision ownership across scheduler jobs. Typelets are bounded zstd
archives with KSON-deserialized manifests and structured compiler steps. Module
compilation uses Binaryen; execution belongs to the shared metered WAMR host on
its serialized owner thread. Keep vendor types out of `api.h`, which is the
canonical C SDK contract. Extend publishing/project/API and real-widget tests.
