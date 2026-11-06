#include "jolttype/jolttype.hpp"
#include "jolttype/project.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cerrno>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace jolttype {
namespace {
void failure(Diagnostic &e, const char *message) {
    e = {}; std::snprintf(e.message, sizeof(e.message), "%s", message);
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
size_t next_character(std::string_view s, size_t i) {
    ++i; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) ++i;
    return i;
}
bool utf8(std::string_view s, size_t &bad) {
    for (size_t i = 0; i < s.size();) {
        bad = i;
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) { if (c < 32 && c != '\n' && c != '\r' && c != '\t') return false; continue; }
        unsigned n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!n || n > s.size() - i) return false;
        uint32_t code = c & ((1u << (6 - n)) - 1);
        for (unsigned j = 0; j < n; ++j) {
            auto b = static_cast<unsigned char>(s[i++]);
            if ((b & 0xc0) != 0x80) return false;
            code = (code << 6) | (b & 63u);
        }
        if ((n == 2 && code < 0x800) || (n == 3 && code < 0x10000) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
tilly::string number(double value) {
    char text[64]; auto r = std::to_chars(text, text + sizeof(text), value, std::chars_format::general, 12);
    if (r.ec != std::errc()) throw std::bad_alloc();
    return tilly::string(text, r.ptr);
}
tilly::string escaped(std::string_view value) {
    tilly::string out;
    for (char c : value) { if (c == '\\' || c == '{' || c == '}' || c == '%') out += '\\'; out += c; }
    return out;
}
struct Parser {
    std::string_view source;
    size_t pos = 0;
    Diagnostic &error;
    const Inventory *inventory = nullptr;
    size_t image_bytes = 0;
    bool fail(const char *message, size_t at) {
        failure(error, message); error.offset = at;
        for (size_t i = 0; i < at && i < source.size(); ++i) {
            if (source[i] == '\n') { ++error.line; error.column = 1; } else ++error.column;
        }
        return false;
    }
    void skip() {
        while (pos < source.size()) {
            if (space(source[pos])) ++pos;
            else if (source[pos] == '%') { while (pos < source.size() && source[pos] != '\n') ++pos; }
            else break;
        }
    }
    bool command(std::string_view &name) {
        skip(); size_t begin = pos;
        if (pos == source.size() || source[pos++] != '\\') return fail("Expected a JoltType command beginning with \\.", begin);
        begin = pos; while (pos < source.size() && letter(source[pos])) ++pos;
        name = source.substr(begin, pos - begin);
        return !name.empty() || fail("Expected a command name.", begin);
    }
    bool argument(tilly::string &value, Span *span = nullptr) {
        skip(); size_t begin = pos;
        if (pos == source.size() || source[pos++] != '{') return fail("Expected a braced argument.", begin);
        begin = pos; value.clear();
        while (pos < source.size() && source[pos] != '}') {
            char c = source[pos++];
            if (c == '{') return fail("Escape literal braces with \\{ and \\}.", pos - 1);
            if (c == '\\') {
                if (pos == source.size()) return fail("Incomplete escape.", pos - 1);
                c = source[pos++];
                if (c != '\\' && c != '{' && c != '}' && c != '%') return fail("Unsupported escape; use \\\\, \\{, \\} or \\%.", pos - 2);
            }
            value += c;
        }
        if (pos == source.size()) return fail("Unclosed argument.", begin - 1);
        if (span) *span = {begin, pos};
        ++pos; return true;
    }
    bool length(double &out, Span *span = nullptr) {
        tilly::string arg; Span location;
        if (!argument(arg, &location)) return false;
        std::string_view s(arg); while (!s.empty() && space(s.front())) s.remove_prefix(1);
        while (!s.empty() && space(s.back())) s.remove_suffix(1);
        double scale = 1;
        if (s.size() >= 2) {
            auto unit = s.substr(s.size() - 2);
            if (unit == "pt" || unit == "mm" || unit == "cm" || unit == "in") {
                scale = unit == "mm" ? 72 / 25.4 : unit == "cm" ? 72 / 2.54 : unit == "in" ? 72 : 1;
                s.remove_suffix(2);
            }
        }
        double n = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), n, std::chars_format::general);
        if (r.ec != std::errc() || r.ptr != s.data() + s.size() || !std::isfinite(n) || n < 0 || n * scale > 14400)
            return fail("Expected a finite length from 0 to 14400pt (pt, mm, cm or in).", location.begin);
        out = n * scale; if (span) *span = location; return true;
    }
    bool layout(Element &e, const Page &p) {
        if (e.width <= 0 || e.x + e.width > p.width + 1e-6 || e.y >= p.height)
            return fail("Element lies outside the page or has zero width.", e.x_span.begin);
        if (e.kind == Kind::Text) {
            if (e.size < 4 || e.size > 240 || e.width < e.size * .6)
                return fail("Text requires 4..240pt type and at least one character of width.", e.x_span.begin);
            size_t cells = static_cast<size_t>(std::floor(e.width / (e.size * .6)));
            size_t start = 0;
            do {
                size_t end = start, count = 0, last_space = std::string_view::npos;
                while (end < e.text.size() && e.text[end] != '\n' && count < cells) {
                    if (e.text[end] == ' ') last_space = end;
                    end = next_character(e.text, end); ++count;
                }
                size_t next = end;
                if (end < e.text.size() && e.text[end] == '\n') ++next;
                else if (end < e.text.size() && e.text[end] == ' ') { while (next < e.text.size() && e.text[next] == ' ') ++next; }
                else if (end < e.text.size() && last_space != std::string_view::npos && last_space > start) { end = last_space; next = last_space + 1; }
                Line line; line.text.assign(e.text.data() + start, end - start);
                count = 0; for (size_t i = 0; i < line.text.size(); i = next_character(line.text, i)) ++count;
                line.width = static_cast<double>(count) * e.size * .6;
                e.lines.push_back(std::move(line));
                if (e.lines.size() > 4096) return fail("Text frame exceeds 4096 lines.", e.text_span.begin);
                start = next;
            } while (start < e.text.size());
            if (!e.text.empty() && e.text.back() == '\n') {
                if (e.lines.size() == 4096) return fail("Text frame exceeds 4096 lines.", e.text_span.begin);
                e.lines.emplace_back();
            }
            e.height = static_cast<double>(e.lines.size()) * e.size * 1.2;
        }
        if (e.height <= 0 || e.y + e.height > p.height + 1e-6)
            return fail("Element overflows the page; resize or move it to another page.", e.y_span.begin);
        return true;
    }
    bool parse(Document &doc) {
        size_t bad = 0;
        if (source.size() > max_source_bytes) return fail("Source exceeds 64 KiB.", 0);
        if (!utf8(source, bad)) return fail("Invalid UTF-8 or control character.", bad);
        std::string_view name; tilly::string arg;
        if (!command(name)) return false;
        if (name != "jolttype") return fail("Start the document with \\jolttype{1}.", 0);
        if (!argument(arg)) return false;
        if (arg != "1") return fail("Unsupported JoltType format version.", pos);
        while (true) {
            skip(); if (pos == source.size()) break;
            size_t begin = pos;
            if (!command(name)) return false;
            if (name == "page") {
                Page p;
                p.source_begin=begin;p.source_end=source.size();
                if (!length(p.width) || !length(p.height)) return false;
                if (p.width < 10 || p.height < 10 || doc.pages.size() == max_pages)
                    return fail("Page dimensions must be at least 10pt; maximum 128 pages.", begin);
                if(!doc.pages.empty())doc.pages.back().source_end=begin;
                doc.pages.push_back(p); continue;
            }
            if (name != "text" && name != "rect" && name != "image" && name != "textfile") return fail("Unknown JoltType command.", begin);
            if (doc.pages.empty()) return fail("Declare a \\page before adding elements.", begin);
            if (doc.elements.size() == max_elements) return fail("Maximum 1024 elements exceeded.", begin);
            Element e; e.kind = name == "image" ? Kind::Image : name == "rect" ? Kind::Rectangle : Kind::Text; e.page = doc.pages.size() - 1;
            if (!argument(e.id)) return false;
            if (e.id.empty() || e.id.size() > 63 || !letter(e.id[0])) return fail("Element IDs must start with a letter and contain 1..63 letters, digits, _ or -.", begin);
            for (char c : e.id) if (!letter(c) && !digit(c) && c != '_' && c != '-') return fail("Invalid element ID.", begin);
            for (const auto &other : doc.elements) if (e.id == other.id) return fail("Duplicate element ID.", begin);
            if (!length(e.x, &e.x_span) || !length(e.y, &e.y_span) || !length(e.width) ||
                !length(e.kind == Kind::Text ? e.size : e.height) || !argument(arg)) return false;
            if(e.kind==Kind::Image) {
                const auto *asset=inventory?inventory->find(arg):nullptr;
                if(!asset || asset->kind!=AssetKind::Image)return fail("Image dependency not found in the inventory.",begin);
                e.asset=arg;e.image=asset->bytes;
                if(!decode_image(e,error))return false;
                image_bytes+=e.pixels.size();if(image_bytes>max_inventory_bytes)return fail("Decoded image budget exceeds 64 MiB.",begin);
            } else {
                if (arg.size() != 7 || arg[0] != '#') return fail("Colors use #RRGGBB.", begin);
                auto r = std::from_chars(arg.data() + 1, arg.data() + 7, e.color, 16);
                if (r.ec != std::errc() || r.ptr != arg.data() + 7) return fail("Invalid hexadecimal color.", begin);
            }
            if (e.kind == Kind::Text) {
                if (!argument(e.text, &e.text_span)) return false;
                if(name=="textfile") {
                    const auto *asset=inventory?inventory->find(e.text):nullptr;
                    if(!asset || (asset->kind!=AssetKind::Text && asset->kind!=AssetKind::Code) || asset->bytes.size()>max_source_bytes)
                        return fail("Text dependency is missing, has the wrong kind, or exceeds 64 KiB.",begin);
                    e.asset=e.text;e.text=asset->bytes;
                    if(!utf8(e.text,bad))return fail("Text dependency is not valid UTF-8.",begin);
                }
                size_t write = 0;
                for (size_t read = 0; read < e.text.size(); ++read) {
                    char c = e.text[read];
                    if (c == '\r') {
                        if (read + 1 < e.text.size() && e.text[read + 1] == '\n') ++read;
                        c = '\n';
                    }
                    e.text[write++] = c == '\t' ? ' ' : c;
                }
                e.text.resize(write);
            }
            if (!layout(e, doc.pages.back())) return false;
            doc.elements.push_back(std::move(e));
        }
        return !doc.pages.empty() || fail("Document requires at least one page.", pos);
    }
};
tilly::string xml(std::string_view text) {
    tilly::string out;
    for (char c : text) {
        switch (c) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break; case '"': out += "&quot;"; break; default: out += c; }
    }
    return out;
}
template<class T> void remember(tilly::vector<T> &history, const T &source) {
    history.reserve(33); history.push_back(source);
    if (history.size() > 32) history.erase(history.begin());
}
}

bool compile(std::string_view source, Document &out, Diagnostic &error, const Inventory *inventory) noexcept {
    try {
        error = {}; Document candidate; Parser p{source, 0, error, inventory};
        if (!p.parse(candidate)) return false;
        out = std::move(candidate); return true;
    } catch (...) { failure(error, "Not enough memory to compile JoltType."); return false; }
}
bool export_svg(const Document &doc, size_t page, tilly::string &out, Diagnostic &error) noexcept {
    try {
        if (page >= doc.pages.size()) { failure(error, "Select a valid page to export."); return false; }
        const auto &p = doc.pages[page];
        tilly::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + number(p.width) + "pt\" height=\"" + number(p.height) +
            "pt\" viewBox=\"0 0 " + number(p.width) + " " + number(p.height) + "\">\n<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
        for (const auto &e : doc.elements) {
            if (e.page != page) continue;
            char color[8]; std::snprintf(color, sizeof(color), "#%06x", unsigned(e.color));
            if (e.kind == Kind::Rectangle) {
                svg += "<rect x=\"" + number(e.x) + "\" y=\"" + number(e.y) + "\" width=\"" + number(e.width) + "\" height=\"" + number(e.height) + "\" fill=\"" + color + "\"/>\n";
            } else if(e.kind==Kind::Image) {
                static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                tilly::string encoded;
                for(size_t i=0;i<e.image.size();i+=3){uint32_t n=uint32_t(static_cast<unsigned char>(e.image[i]))<<16;
                    if(i+1<e.image.size())n|=uint32_t(static_cast<unsigned char>(e.image[i+1]))<<8;
                    if(i+2<e.image.size())n|=static_cast<unsigned char>(e.image[i+2]);
                    encoded+=alphabet[(n>>18)&63];encoded+=alphabet[(n>>12)&63];encoded+=i+1<e.image.size()?alphabet[(n>>6)&63]:'=';encoded+=i+2<e.image.size()?alphabet[n&63]:'=';}
                svg+="<image x=\""+number(e.x)+"\" y=\""+number(e.y)+"\" width=\""+number(e.width)+"\" height=\""+number(e.height)+"\" preserveAspectRatio=\"none\" href=\"data:image/"+
                    (e.image.size()>1 && static_cast<unsigned char>(e.image[0])==0x89?"png":"jpeg")+";base64,"+encoded+"\"/>\n";
            } else {
                double y = e.y + e.size;
                for (const auto &line : e.lines) {
                    if (!line.text.empty()) svg += "<text x=\"" + number(e.x) + "\" y=\"" + number(y) + "\" font-family=\"monospace\" font-size=\"" + number(e.size) +
                        "\" fill=\"" + color + "\" xml:space=\"preserve\" textLength=\"" + number(line.width) + "\" lengthAdjust=\"spacingAndGlyphs\">" + xml(line.text) + "</text>\n";
                    y += e.size * 1.2;
                }
            }
        }
        svg += "</svg>\n"; out.swap(svg); error = {}; return true;
    } catch (...) { failure(error, "Not enough memory to export SVG."); return false; }
}
bool write_file(const char *path, std::string_view bytes, Diagnostic &error) noexcept {
    struct Temporary {
        tilly::string path; FILE *file = nullptr;
        ~Temporary() { if (file) std::fclose(file); if (!path.empty()) std::remove(path.c_str()); }
    } temp;
    try {
        if (!path || !*path) { failure(error, "Specify an output path."); return false; }
        for (int slot = 0; slot < 1000; ++slot) {
            temp.path = tilly::string(path) + ".jolttype-part-" + number(slot);
            errno = 0; temp.file = std::fopen(temp.path.c_str(), "wbx");
            if (temp.file) break;
            temp.path.clear(); if (errno != EEXIST) break;
        }
        if (!temp.file) { failure(error, "Cannot create an output file beside the destination."); return false; }
        bool ok = std::fwrite(bytes.data(), 1, bytes.size(), temp.file) == bytes.size() && !std::ferror(temp.file);
        if (std::fclose(temp.file) != 0) ok = false;
        temp.file = nullptr;
        if (ok) {
#ifdef _WIN32
            ok = MoveFileExA(temp.path.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            ok = std::rename(temp.path.c_str(), path) == 0;
#endif
        }
        if (!ok) { failure(error, "Could not publish output; previous file retained."); return false; }
        temp.path.clear(); error = {}; return true;
    } catch (...) { failure(error, "Not enough memory to write output."); return false; }
}
const char *example_source() noexcept {
    return "\\jolttype{1}\n% JoltType: lengths are pt, mm, cm or in.\n\\page{210mm}{297mm}\n"
        "\\text{title}{20mm}{20mm}{170mm}{28pt}{#182840}{Page Design}\n"
        "\\rect{accent}{20mm}{38mm}{170mm}{3pt}{#e46b36}\n"
        "\\text{body}{20mm}{50mm}{170mm}{12pt}{#303030}{Welcome to JoltType. Select and drag a frame, edit its text, then press Commit.\nYour draft stays separate from the published page.}\n";
}
bool Session::stage(std::string_view source, double now) noexcept {
    try {
        if (source.size() > max_source_bytes || !std::isfinite(now)) { failure(error_, "Invalid source size or clock."); return false; }
        tilly::string next(source);
        if (next == draft_) return true;
        if (!dirty()) pending_since_ = now;
        draft_.swap(next); synchronized_ = false; error_ = {}; return true;
    } catch (...) { failure(error_, "Not enough memory for the draft."); return false; }
}
bool Session::visual(tilly::string source, double now) noexcept {
    Document next;
    if (!compile(source, next, error_, inventory_.get())) return false;
    if (!stage(source, now)) return false;
    preview_ = std::move(next); synchronized_ = true; return true;
}
bool Session::move(std::string_view id, double x, double y, double now) noexcept {
    try {
        if (!synchronized_) { failure(error_, "Commit or revert source edits before visual editing."); return false; }
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0) { failure(error_, "Invalid frame position."); return false; }
        for (const auto &e : preview_.elements) if (e.id == id) {
            auto next = draft_;
            next.replace(e.y_span.begin, e.y_span.end - e.y_span.begin, number(y));
            next.replace(e.x_span.begin, e.x_span.end - e.x_span.begin, number(x));
            return visual(std::move(next), now);
        }
        failure(error_, "Select a frame to move."); return false;
    } catch (...) { failure(error_, "Not enough memory to move the frame."); return false; }
}
bool Session::set_text(std::string_view id, std::string_view text, double now) noexcept {
    try {
        if (!synchronized_) { failure(error_, "Commit or revert source edits before visual editing."); return false; }
        if (text.size() > max_source_bytes) { failure(error_, "Text exceeds 64 KiB."); return false; }
        for (const auto &e : preview_.elements) if (e.id == id && e.kind == Kind::Text) {
            if(!e.asset.empty()){failure(error_,"Edit resource-backed text in the inventory.");return false;}
            auto next = draft_; next.replace(e.text_span.begin, e.text_span.end - e.text_span.begin, escaped(text));
            return visual(std::move(next), now);
        }
        failure(error_, "Select a text frame."); return false;
    } catch (...) { failure(error_, "Not enough memory to edit text."); return false; }
}
bool Session::commit(double now) noexcept {
    try {
        if (!std::isfinite(now)) { failure(error_, "Invalid commit clock."); return false; }
        Document next;
        if (!compile(draft_, next, error_, inventory_.get())) return false;
        Document preview = next; tilly::string source = draft_;
        if (dirty() && !source_.empty()) remember(past_, Revision{source_,committed_inventory_});
        if (dirty()) future_.clear();
        committed_ = std::move(next); preview_ = std::move(preview); source_.swap(source);
        committed_inventory_=inventory_;
        synchronized_ = true; pending_since_ = now; error_ = {}; return true;
    } catch (...) { failure(error_, "Not enough memory to commit the draft."); return false; }
}
bool Session::revert() noexcept {
    try {
        auto source = source_; auto doc = committed_;
        draft_.swap(source); preview_ = std::move(doc); inventory_=committed_inventory_; synchronized_ = true; error_ = {}; return true;
    } catch (...) { failure(error_, "Not enough memory to revert the draft."); return false; }
}
bool Session::tick(double now, int interval, bool busy) noexcept {
    if (interval <= 0 || !dirty() || busy || !std::isfinite(now)) return false;
    if (now < pending_since_) pending_since_ = now;
    if (now - pending_since_ < interval) return false;
    pending_since_ = now; return commit(now);
}
bool Session::can_history(bool redo) const { return !dirty() && !(redo ? future_ : past_).empty(); }
bool Session::history(bool redo) noexcept {
    try {
        if (!can_history(redo)) return false;
        auto &from = redo ? future_ : past_; auto &to = redo ? past_ : future_;
        Document next; if (!compile(from.back().source, next, error_, from.back().inventory.get())) return false;
        auto preview = next; auto source = from.back().source; auto draft = source;
        remember(to, Revision{source_,committed_inventory_});
        inventory_=committed_inventory_=from.back().inventory;
        committed_ = std::move(next); preview_ = std::move(preview); source_.swap(source); draft_.swap(draft);
        from.pop_back(); synchronized_ = true; error_ = {}; return true;
    } catch (...) { failure(error_, "Not enough memory to restore history."); return false; }
}
bool Session::load(const char *path, double now) noexcept {
    struct File { FILE *p; ~File() { if (p) std::fclose(p); } } file{path ? std::fopen(path, "rb") : nullptr};
    if (!file.p) { failure(error_, "Cannot open JoltType source."); return false; }
    try {
        tilly::string source(max_source_bytes + 1, '\0');
        size_t n = std::fread(source.data(), 1, source.size(), file.p);
        if (n > max_source_bytes || std::ferror(file.p)) { failure(error_, "Cannot read source, or file exceeds 64 KiB."); return false; }
        source.resize(n); Document next;
        if (!std::isfinite(now)) { failure(error_, "Invalid load clock."); return false; }
        if (!compile(source, next, error_, inventory_.get())) return false;
        auto preview = next; auto draft = source;
        if (!source_.empty() && source != source_) remember(past_, Revision{source_,committed_inventory_});
        source_.swap(source); draft_.swap(draft); committed_ = std::move(next); preview_ = std::move(preview);
        committed_inventory_=inventory_;
        future_.clear(); synchronized_ = true; pending_since_ = now; return true;
    } catch (...) { failure(error_, "Not enough memory to open source."); return false; }
}
bool Session::save(const char *path) noexcept {
    if (source_.empty()) { failure(error_, "Commit a document before saving."); return false; }
    return write_file(path, source_, error_);
}
bool Session::set_inventory(const Inventory &inventory,Diagnostic &error) noexcept {
    try {
        if(!inventory.validate(error))return false;
        if(!inventory_ && inventory.assets().empty())return true;
        if(inventory_ && inventory.assets().size()==inventory_->assets().size()) {
            bool same=true;
            for(size_t i=0;i<inventory.assets().size();++i){const auto &a=inventory.assets()[i],&b=inventory_->assets()[i];
                if(a.id!=b.id || a.path!=b.path || a.kind!=b.kind || a.tags!=b.tags || a.dependencies!=b.dependencies || a.bytes!=b.bytes){same=false;break;}}
            if(same)return true;
        }
        inventory_=std::allocate_shared<Inventory>(tilly::allocator<Inventory>{},inventory);error={};return true;
    }
    catch(...){failure(error,"Not enough memory to snapshot inventory.");return false;}
}
}
