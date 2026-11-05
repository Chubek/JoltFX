#include "preferences.h"
#include "tilly/allocator.h"
#include "tilly/memory.hpp"
#include "yyjson.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace jfx_preferences {
namespace {
constexpr size_t max_bytes = 64 * 1024;
void message(char *out, size_t cap, const char *text) {
    if (out && cap) std::snprintf(out, cap, "%s", text);
}
bool range(float n, float lo, float hi) { return std::isfinite(n) && n >= lo && n <= hi; }
bool valid(const Values &v) {
    return range(v.viewport_background[0], 0, 1) && range(v.viewport_background[1], 0, 1) && range(v.viewport_background[2], 0, 1) &&
        range(v.orbit_speed, .05f, 5) && range(v.pan_speed, .1f, 5) && range(v.zoom_speed, .05f, 2) &&
        range(v.ui_scale, .75f, 2) && v.preview_max_dimension >= 320 && v.preview_max_dimension <= 1280 && v.theme >= 0 && v.theme <= 2;
}
void *allocate(void *, size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes, alignof(max_align_t));
}
void *reallocate(void *, void *p, size_t, size_t bytes) {
    return tilly_realloc((tilly_allocator_t *)tilly_default_allocator(), p, bytes);
}
void release(void *, void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(), p); }
const yyjson_alc allocator = {allocate, reallocate, release, nullptr};
struct ReadOwner { yyjson_doc *doc; ~ReadOwner() { yyjson_doc_free(doc); } };
struct WriteOwner { yyjson_mut_doc *doc; ~WriteOwner() { yyjson_mut_doc_free(doc); } };
struct TextOwner { char *text; ~TextOwner() { release(nullptr, text); } };
struct FileOwner { FILE *file; ~FileOwner() { if (file) std::fclose(file); } };
struct Temporary {
    tilly::string path;
    FILE *file = nullptr;
    ~Temporary() { if (file) std::fclose(file); if (!path.empty()) std::remove(path.c_str()); }
};
}

Values defaults() {
    return {{0, 0, 0}, .6f, 1, .3f, 1, 1280, 0, false, true, true, false};
}
bool resolve_paths(Paths &out) {
    out = {};
    try {
        std::filesystem::path directory;
        if (const char *override_path = std::getenv("JOLTFX_HOME_DIR"); override_path && *override_path)
            directory = override_path;
        if (directory.empty()) {
#ifdef _WIN32
            if (const char *appdata = std::getenv("APPDATA"); appdata && *appdata) directory = std::filesystem::path(appdata) / "JoltFX";
#elif defined(__APPLE__)
            if (const char *home = std::getenv("HOME"); home && *home) directory = std::filesystem::path(home) / "Library" / "Application Support" / "JoltFX";
#else
            if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg == '/') directory = std::filesystem::path(xdg) / "JoltFX";
            if (directory.empty())
                if (const char *home = std::getenv("HOME"); home && *home) directory = std::filesystem::path(home) / ".config" / "JoltFX";
#endif
        }
        if (directory.empty()) return false;
        auto config = (directory / "Config.json").string(), ini = (directory / "imgui.ini").string();
        if (config.size() >= sizeof(out.config) || ini.size() >= sizeof(out.ini)) return false;
        std::memcpy(out.config, config.c_str(), config.size() + 1);
        std::memcpy(out.ini, ini.c_str(), ini.size() + 1);
        return true;
    } catch (...) { return false; }
}

LoadResult load(const char *path, Values &out, char *error, size_t capacity) {
    message(error, capacity, "");
    if (!path || !*path) { message(error, capacity, "No user configuration directory is available."); return LoadResult::IOError; }
    try {
        errno = 0; FileOwner file{std::fopen(path, "rb")};
        if (!file.file) {
            if (errno == ENOENT) return LoadResult::Missing;
            message(error, capacity, "Cannot read Config.json; defaults are in use."); return LoadResult::IOError;
        }
        tilly::vector<char> bytes(max_bytes + 1);
        size_t count = std::fread(bytes.data(), 1, bytes.size(), file.file);
        if (std::ferror(file.file)) { message(error, capacity, "Cannot read Config.json; defaults are in use."); return LoadResult::IOError; }
        auto invalid = [&] { message(error, capacity, "Invalid or unsupported Config.json; defaults are in use. Save Preferences to replace it."); return LoadResult::Invalid; };
        if (!count || count > max_bytes) return invalid();
        ReadOwner owner{yyjson_read_opts(bytes.data(), count, 0, &allocator, nullptr)};
        auto *root = yyjson_doc_get_root(owner.doc);
        if (!yyjson_is_obj(root)) return invalid();
        const char *keys[] = {"schema_version", "viewport_background", "orbit_speed", "pan_speed", "zoom_speed", "ui_scale",
            "preview_max_dimension", "theme", "invert_zoom", "show_gimbal", "loop_playback", "timeline_snap"};
        unsigned seen = 0; size_t index, size; yyjson_val *key, *value;
        yyjson_obj_foreach(root, index, size, key, value) {
            for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) if (yyjson_equals_str(key, keys[i])) {
                if (seen & (1u << i)) return invalid();
                seen |= 1u << i;
            }
        }
        Values next = defaults();
        auto real = [&](const char *name, float &number) {
            auto *v = yyjson_obj_get(root, name);
            if (!v) return true;
            if (!yyjson_is_num(v) || !std::isfinite(yyjson_get_num(v)) || std::abs(yyjson_get_num(v)) > 10000) return false;
            number = float(yyjson_get_num(v)); return true;
        };
        auto integer = [&](const char *name, int &number) {
            auto *v = yyjson_obj_get(root, name);
            if (!v) return true;
            if (!yyjson_is_int(v) || yyjson_get_sint(v) < 0 || yyjson_get_sint(v) > 10000) return false;
            number = int(yyjson_get_sint(v)); return true;
        };
        auto boolean = [&](const char *name, bool &flag) {
            auto *v = yyjson_obj_get(root, name);
            if (!v) return true;
            if (!yyjson_is_bool(v)) return false;
            flag = yyjson_get_bool(v); return true;
        };
        int version = 1;
        if (!integer("schema_version", version) || version != 1 || !real("orbit_speed", next.orbit_speed) ||
            !real("pan_speed", next.pan_speed) || !real("zoom_speed", next.zoom_speed) || !real("ui_scale", next.ui_scale) ||
            !integer("preview_max_dimension", next.preview_max_dimension) || !integer("theme", next.theme) ||
            !boolean("invert_zoom", next.invert_zoom) || !boolean("show_gimbal", next.show_gimbal) ||
            !boolean("loop_playback", next.loop_playback) || !boolean("timeline_snap", next.timeline_snap)) return invalid();
        if (auto *color = yyjson_obj_get(root, "viewport_background")) {
            if (!yyjson_is_arr(color) || yyjson_arr_size(color) != 3) return invalid();
            for (size_t i = 0; i < 3; ++i) {
                auto *channel = yyjson_arr_get(color, i);
                double n = yyjson_get_num(channel);
                if (!yyjson_is_num(channel) || !std::isfinite(n) || n < 0 || n > 1) return invalid();
                next.viewport_background[i] = float(n);
            }
        }
        if (!valid(next)) return invalid();
        out = next; return LoadResult::Loaded;
    } catch (...) { message(error, capacity, "Cannot allocate preferences storage; defaults are in use."); return LoadResult::IOError; }
}

bool save(const char *path, const Values &v, char *error, size_t capacity) {
    message(error, capacity, "");
    if (!path || !*path || !valid(v)) { message(error, capacity, "Invalid preferences or configuration path."); return false; }
    try {
        WriteOwner owner{yyjson_mut_doc_new(&allocator)};
        if (!owner.doc) throw std::bad_alloc();
        auto *root = yyjson_mut_obj(owner.doc); if (!root) throw std::bad_alloc();
        yyjson_mut_doc_set_root(owner.doc, root);
        auto *color = yyjson_mut_arr_with_float(owner.doc, v.viewport_background, 3);
        if (!color || !yyjson_mut_obj_add_val(owner.doc, root, "viewport_background", color) ||
            !yyjson_mut_obj_add_int(owner.doc, root, "schema_version", 1) ||
            !yyjson_mut_obj_add_real(owner.doc, root, "orbit_speed", double(v.orbit_speed)) ||
            !yyjson_mut_obj_add_real(owner.doc, root, "pan_speed", double(v.pan_speed)) ||
            !yyjson_mut_obj_add_real(owner.doc, root, "zoom_speed", double(v.zoom_speed)) ||
            !yyjson_mut_obj_add_real(owner.doc, root, "ui_scale", double(v.ui_scale)) ||
            !yyjson_mut_obj_add_int(owner.doc, root, "preview_max_dimension", v.preview_max_dimension) ||
            !yyjson_mut_obj_add_int(owner.doc, root, "theme", v.theme) ||
            !yyjson_mut_obj_add_bool(owner.doc, root, "invert_zoom", v.invert_zoom) ||
            !yyjson_mut_obj_add_bool(owner.doc, root, "show_gimbal", v.show_gimbal) ||
            !yyjson_mut_obj_add_bool(owner.doc, root, "loop_playback", v.loop_playback) ||
            !yyjson_mut_obj_add_bool(owner.doc, root, "timeline_snap", v.timeline_snap)) throw std::bad_alloc();
        size_t count = 0;
        TextOwner json{yyjson_mut_write_opts(owner.doc, YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END, &allocator, &count, nullptr)};
        if (!json.text) throw std::bad_alloc();
        auto parent = std::filesystem::path(path).parent_path(); std::error_code ec;
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        if (ec) { message(error, capacity, "Cannot create the preferences directory."); return false; }
        Temporary temp;
        for (unsigned slot = 0; slot < 1000; ++slot) {
            char suffix[40]; std::snprintf(suffix, sizeof(suffix), ".jfx-part-%u", slot);
            temp.path = tilly::string(path) + suffix;
            errno = 0; temp.file = std::fopen(temp.path.c_str(), "wbx");
            if (temp.file) break;
            temp.path.clear(); if (errno != EEXIST) break;
        }
        if (!temp.file) { message(error, capacity, "Cannot create a temporary preferences file."); return false; }
        bool ok = std::fwrite(json.text, 1, count, temp.file) == count && !std::ferror(temp.file);
        if (std::fclose(temp.file) != 0) ok = false;
        temp.file = nullptr;
        if (ok) {
#ifdef _WIN32
            ok = MoveFileExA(temp.path.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            ok = std::rename(temp.path.c_str(), path) == 0;
#endif
        }
        if (!ok) { message(error, capacity, "Cannot save Config.json; the previous file was kept."); return false; }
        temp.path.clear(); return true;
    } catch (...) { message(error, capacity, "Cannot allocate preferences storage; the previous file was kept."); return false; }
}
}
