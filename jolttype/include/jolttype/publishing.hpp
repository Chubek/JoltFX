#ifndef JOLTTYPE_PUBLISHING_HPP
#define JOLTTYPE_PUBLISHING_HPP
#include "jolttype.hpp"
namespace jolttype {
// PDF is final output through libharu. The appended alternative preserves IDs.
enum class Target { PDF, SVG, PostScript, DVI, PDFPoDoFo };
using Progress = void (*)(void *, unsigned percent, const char *stage);
struct Bitmap { unsigned width = 0, height = 0; tilly::vector<uint8_t> rgba; };
// PDF/PS/DVI contain all pages; SVG exports the selected page.
bool publish(const Document &, Target, size_t page, tilly::string &bytes,
    Diagnostic &, Progress progress = nullptr, void *user = nullptr) noexcept;
bool pdf_available() noexcept;
bool pdf_preview_available() noexcept;
// PDFium rasterizes an immutable, qpdf-validated PDF. max_dimension: 64..2048.
bool render_pdf(std::string_view pdf, size_t page, unsigned max_dimension,
    Bitmap &, Diagnostic &) noexcept;
bool inspect_pdf(std::string_view pdf, size_t &pages, Diagnostic &) noexcept;
}
#endif
