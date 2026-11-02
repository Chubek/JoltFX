#ifndef JFX_DESKTOP_GAME_AUTHORING_H
#define JFX_DESKTOP_GAME_AUTHORING_H

/* Private, allocation-free authoring helpers. Ingagi source is the canonical
 * Cliche/room resource; the picker is only a lexical aid, never a compiler. */
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace game_authoring {
inline constexpr const char *primitives[] = {"cube", "sphere", "plane", "cylinder", "cone", "torus",
    "capsule", "pyramid", "disk", "tube", "hemisphere", "wedge", "tetrahedron", "octahedron",
    "icosahedron", "nurbs", "metaball"};
struct Draft { char name[48]; int primitive; float transform[9], color[3]; };
inline void defaults(Draft &d) {
    d = {}; std::strcpy(d.name, "Player");
    d.transform[6] = d.transform[7] = d.transform[8] = 1;
    d.color[0] = .3f; d.color[1] = .65f; d.color[2] = .95f;
}
inline bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool word(char c) { return alpha(c) || (c >= '0' && c <= '9'); }
inline bool identifier(const char *s) {
    if (!s || !alpha(*s)) return false;
    while (*++s) if (!word(*s)) return false;
    return true;
}
struct Token {
    const char *data; size_t size;
    bool is(const char *s) const { return std::strlen(s) == size && !std::memcmp(data, s, size); }
};
struct Lexer {
    const char *p;
    Token next() {
        for (;;) {
            while (*p && static_cast<unsigned char>(*p) <= 32) ++p;
            if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') ++p; continue; }
            if (p[0] == '/' && p[1] == '*') {
                p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
                if (*p) p += 2;
                continue;
            }
            break;
        }
        const char *start = p;
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            while (*p && *p != quote) { if (*p == '\\' && p[1]) ++p; ++p; }
            if (*p) ++p;
        } else if (word(*p)) { while (word(*p)) ++p; }
        else if (*p) ++p;
        return {start, size_t(p - start)};
    }
};
struct Entries { char names[64][64]; unsigned count; };
inline void scan(const char *source, Entries &entries) {
    entries = {}; Lexer lexer{source}; int depth = 0;
    for (Token t = lexer.next(); t.size; t = lexer.next()) {
        if (t.is("{")) ++depth;
        else if (t.is("}") && depth) --depth;
        else if (!depth && t.is("fn")) {
            Token name = lexer.next(), open = lexer.next();
            if (!name.size || !alpha(*name.data) || name.size >= sizeof(entries.names[0]) || !open.is("(")) continue;
            Token close = lexer.next();
            if (close.is(")") && entries.count < 64) {
                std::memcpy(entries.names[entries.count], name.data, name.size);
                entries.names[entries.count++][name.size] = 0;
            }
        }
    }
}
// Also recognizes functions with arguments, to reject duplicate factory names.
inline bool declared(const char *source, const char *name) {
    Lexer lexer{source}; int depth = 0;
    for (Token t = lexer.next(); t.size; t = lexer.next()) {
        if (t.is("{")) ++depth;
        else if (t.is("}") && depth) --depth;
        else if (!depth && t.is("fn") && lexer.next().is(name)) return true;
    }
    return false;
}
struct Writer {
    char *data; size_t capacity, used = 0; bool ok = true;
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    void add(const char *format, ...) {
        if (!ok) return;
        va_list args; va_start(args, format);
        int n = std::vsnprintf(data + used, capacity - used, format, args); va_end(args);
        if (n < 0 || size_t(n) >= capacity - used) ok = false;
        else used += size_t(n);
    }
};
inline bool append(char *source, size_t capacity, const char *text) {
    size_t a = std::strlen(source), b = std::strlen(text);
    if (a >= capacity || b >= capacity - a) return false;
    std::memcpy(source + a, text, b + 1); return true;
}
inline bool append_cliche(char *source, size_t capacity, const Draft &d, const char *base = nullptr) {
    if (!identifier(d.name) || d.primitive < 0 || d.primitive >= 17) return false;
    if (base && (!identifier(base) || !declared(source, base))) return false;
    for (float v : d.transform) if (!std::isfinite(v) || std::fabs(v) > 10000) return false;
    for (unsigned i = 6; i < 9; ++i) if (std::fabs(d.transform[i]) < .001f) return false;
    for (float v : d.color) if (!std::isfinite(v) || v < 0 || v > 1) return false;
    char name[64]; std::snprintf(name, sizeof(name), "cliche_%s", d.name);
    if (declared(source, name)) return false;
    char text[2048]{}; Writer w{text, sizeof(text)};
    w.add("\n// Cliche: reusable Ingagi factory. Customize or compose other Cliches here.\nfn %s() {\n", name);
    if (base) w.add("    let object = %s();\n", base);
    else w.add("    let object = engine_scene_add(%d);\n", d.primitive);
    for (unsigned i = 0; i < 9; ++i) {
        if (i < 3) w.add("    engine_scene_set(object, %u, %.9g + engine_spawn_offset(%u));\n", i, double(d.transform[i]), i);
        else w.add("    engine_scene_set(object, %u, %.9g);\n", i, double(d.transform[i]));
    }
    for (unsigned i = 0; i < 3; ++i) w.add("    engine_scene_color(object, %u, %.9g);\n", i, double(d.color[i]));
    w.add("    return object;\n}\n");
    return w.ok && append(source, capacity, text);
}
inline constexpr const char *starter =
    "// GameDev: Ingagi owns Cliches, rooms, ECS components and event logic.\n"
    "// ready runs once per Play; step runs once per preview tick.\n"
    "component Actor { object: i64; speed: f64; }\n"
    "fn cliche_Hero() {\n"
    "    let object = engine_scene_add(0);\n"
    "    engine_scene_color(object, 0, 0.3);\n"
    "    let actor = spawn();\n"
    "    add_component(actor, \"Actor\", Actor { object: object, speed: 3.0 });\n"
    "    return object;\n"
    "}\n"
    "fn room_main() { return cliche_Hero(); }\n"
    "fn ready() {\n"
    "    if engine_scene_count() == 0 { return room_main(); }\n"
    "    return engine_scene_count();\n"
    "}\n"
    "fn step() {\n"
    "    if engine_scene_count() > 0 {\n"
    "        let x = engine_scene_get(0, 0);\n"
    "        let direction = 0.0;\n"
    "        if engine_input(0) { direction = direction - 1.0; }\n"
    "        if engine_input(1) { direction = direction + 1.0; }\n"
    "        if direction != 0.0 { engine_scene_set(0, 0, x + direction * engine_delta() * 3.0); }\n"
    "    }\n"
    "    return engine_scene_count();\n"
    "}\n"
    "fn main() { return ready(); }\n";
}
#endif
