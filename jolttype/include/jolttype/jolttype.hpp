#ifndef JOLTTYPE_HPP
#define JOLTTYPE_HPP

#include "tilly/memory.hpp"
#include <cstdint>
#include <string_view>

#define JOLTTYPE_VERSION_MAJOR 0
#define JOLTTYPE_VERSION_MINOR 5
#define JOLTTYPE_VERSION_PATCH 0

namespace jolttype {
constexpr size_t max_source_bytes = 64 * 1024;
constexpr size_t max_expansion_bytes = 1024 * 1024;
constexpr size_t max_pages = 128, max_elements = 1024;
struct Diagnostic {
    size_t offset = 0, line = 1, column = 1;
    char message[256]{};
};
struct Span { size_t begin = 0, end = 0; };
struct Page { double width = 595.275590551, height = 841.88976378; size_t source_begin=0,source_end=0; };
struct Line { tilly::string text; double width = 0; };
// Components belong to entities. Configurations are the only values captured by
// a Kliche; its data-component schema and defaults belong to the base Pagelet.
struct Component { tilly::string name, value; bool configuration=false; };
struct Pagelet {
    tilly::string id, base, body;
    tilly::vector<Component> components;
};
struct Entity {
    tilly::string id, type;
    size_t page=0;
    double x=0,y=0,width=0,height=0;
    Span x_span,y_span,values_span;
    bool inline_pagelet=false;
    tilly::vector<Component> components;
};
enum class Kind { Text, Rectangle, Image };
struct Element {
    Kind kind = Kind::Text;
    tilly::string id, text, entity;
    size_t page = 0;
    double x = 0, y = 0, width = 0, height = 0, size = 12;
    uint32_t color = 0;
    Span x_span, y_span, text_span;
    tilly::vector<Line> lines;
    tilly::string asset, image;
    unsigned image_width=0,image_height=0;
    tilly::vector<uint8_t> pixels;
};
struct Document {
    tilly::vector<Page> pages;
    tilly::vector<Element> elements;
    tilly::vector<Pagelet> pagelets;
    tilly::vector<Entity> entities;
};

// All lengths are points (1/72 inch), origin at the upper-left. Compilation
// never modifies out on error. No file access, macros or code execution.
class Inventory;
bool compile(std::string_view source, Document &out, Diagnostic &error, const Inventory *inventory=nullptr) noexcept;
bool decode_image(Element &,Diagnostic &) noexcept;
bool export_svg(const Document &document, size_t page, tilly::string &out, Diagnostic &error) noexcept;
// Frozen geometry and data; resource references still use the document inventory.
bool export_source(const Document &,tilly::string &,Diagnostic &) noexcept;
bool write_file(const char *path, std::string_view bytes, Diagnostic &error) noexcept;
const char *example_source() noexcept;

class Session {
public:
    const tilly::string &draft() const { return draft_; }
    const tilly::string &source() const { return source_; }
    const Document &committed() const { return committed_; }
    const Document &preview() const { return preview_; }
    const Diagnostic &error() const { return error_; }
    bool dirty() const { return draft_ != source_ || inventory_ != committed_inventory_; }
    bool visual_ready() const { return synchronized_; }
    bool stage(std::string_view source, double now) noexcept;
    bool refresh() noexcept; // Draft preview only: no publication/history/save.
    bool set_component(std::string_view entity,std::string_view name,std::string_view value,double now) noexcept;
    bool move(std::string_view id, double x, double y, double now) noexcept;
    bool set_text(std::string_view id, std::string_view text, double now) noexcept;
    bool commit(double now) noexcept;
    bool revert() noexcept;
    // Returns true only when a revision was published. Zero disables the timer.
    // Busy defers publication during a gesture/text edit; the deadline persists.
    bool tick(double now, int interval_seconds, bool busy) noexcept;
    bool can_history(bool redo) const;
    bool history(bool redo) noexcept;
    bool load(const char *path, double now) noexcept;
    bool save(const char *path) noexcept; // Saves committed source only.
    bool set_inventory(const Inventory &,Diagnostic &) noexcept;
    const Inventory *inventory() const {return inventory_.get();}
private:
    struct Revision {tilly::string source;std::shared_ptr<const Inventory> inventory;};
    tilly::string draft_, source_;
    Document committed_, preview_;
    tilly::vector<Revision> past_, future_;
    Diagnostic error_;
    bool synchronized_ = true;
    double pending_since_ = 0;
    std::shared_ptr<const Inventory> inventory_,committed_inventory_;
    bool visual(tilly::string source, double now) noexcept;
};
}
#endif
