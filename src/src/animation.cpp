#include "jfx/jfx_animation.h"
#include "tilly/memory.h"
#include "tilly/memory.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

struct Bone { tilly::string name; uint32_t parent; jfx_animation_transform_t local; };
struct Key { jfx_animation_key_t k; };
struct jfx_animation_scene { tilly::vector<Bone> bones; tilly::vector<Key> keys; };
struct jfx_animation_tab {
    uint32_t width = 0, height = 0;
    double time = 0;
    bool playing = false;
    jfx_animation_scene_t *scene = nullptr;
};

static bool finite_transform(const jfx_animation_transform_t &t) {
    return std::isfinite(t.translation.x) && std::isfinite(t.translation.y) &&
        std::isfinite(t.rotation) && std::isfinite(t.scale.x) && std::isfinite(t.scale.y);
}
static bool valid_curve(jfx_animation_curve_t c) {
    return c >= JFX_ANIMATION_CURVE_STEP && c <= JFX_ANIMATION_CURVE_SMOOTH;
}
static float sample(const tilly::vector<Key> &keys, uint32_t bone, uint8_t prop, double t, float base) {
    const Key *left = nullptr, *right = nullptr;
    for (const auto &entry : keys) {
        const auto &k = entry.k;
        if (k.bone != bone || k.property != prop) continue;
        if (k.time <= t && (!left || k.time > left->k.time)) left = &entry;
        if (k.time >= t && (!right || k.time < right->k.time)) right = &entry;
    }
    if (!left && !right) return base;
    if (!left) return right->k.value;
    if (!right || left == right) return left->k.value;
    double span = right->k.time - left->k.time;
    double u = std::clamp(span > 0 ? (t - left->k.time) / span : 0, 0.0, 1.0);
    if (left->k.curve == JFX_ANIMATION_CURVE_STEP) u = 0;
    else if (left->k.curve == JFX_ANIMATION_CURVE_SMOOTH) u = u * u * (3 - 2 * u);
    return static_cast<float>(left->k.value + (right->k.value - left->k.value) * u);
}
static void evaluate_internal(const jfx_animation_scene *s, double t,
                              tilly::vector<jfx_animation_transform_t> &out) {
    out.resize(s->bones.size());
    for (size_t i = 0; i < s->bones.size(); ++i) {
        const auto &b = s->bones[i];
        auto tr = b.local;
        tr.translation.x = sample(s->keys, static_cast<uint32_t>(i), 0, t, tr.translation.x);
        tr.translation.y = sample(s->keys, static_cast<uint32_t>(i), 1, t, tr.translation.y);
        tr.rotation = sample(s->keys, static_cast<uint32_t>(i), 2, t, tr.rotation);
        tr.scale.x = sample(s->keys, static_cast<uint32_t>(i), 3, t, tr.scale.x);
        tr.scale.y = sample(s->keys, static_cast<uint32_t>(i), 4, t, tr.scale.y);
        if (b.parent == UINT32_MAX) out[i] = tr;
        else {
            const auto p = out[b.parent];
            float c = std::cos(p.rotation), q = std::sin(p.rotation);
            out[i].translation = {p.translation.x + c * tr.translation.x * p.scale.x - q * tr.translation.y * p.scale.y,
                p.translation.y + q * tr.translation.x * p.scale.x + c * tr.translation.y * p.scale.y};
            out[i].rotation = p.rotation + tr.rotation;
            out[i].scale = {p.scale.x * tr.scale.x, p.scale.y * tr.scale.y};
        }
    }
}
static void put32(tilly::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (i * 8)));
}
static void putf(tilly::vector<uint8_t> &b, float v) { uint32_t x; std::memcpy(&x, &v, 4); put32(b, x); }
static void putd(tilly::vector<uint8_t> &b, double v) {
    uint64_t x; std::memcpy(&x, &v, 8);
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(x >> (i * 8)));
}
static uint32_t get32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
        static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}
static float getf(const uint8_t *p) { uint32_t x = get32(p); float v; std::memcpy(&v, &x, 4); return v; }
static double getd(const uint8_t *p) {
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x |= static_cast<uint64_t>(p[i]) << (i * 8);
    double v; std::memcpy(&v, &x, 8); return v;
}

extern "C" {
jfx_animation_scene_t *jfx_animation_scene_create(void) { return tilly::create<jfx_animation_scene>(); }
void jfx_animation_scene_destroy(jfx_animation_scene_t *s) { tilly::destroy(s); }
jfx_result_t jfx_animation_scene_add_bone(jfx_animation_scene_t *s,
    const jfx_animation_bone_desc_t *d, uint32_t *out) {
    if (!s || !d || d->size < sizeof(*d) || !out ||
        (d->parent != UINT32_MAX && d->parent >= s->bones.size()) ||
        s->bones.size() >= UINT32_MAX || !finite_transform(d->transform)) return JFX_ERROR_INVALID_ARGUMENT;
    try {
        Bone b;
        b.name = d->name ? d->name : "bone";
        b.parent = d->parent;
        b.local = d->transform;
        if (b.local.scale.x == 0) b.local.scale.x = 1;
        if (b.local.scale.y == 0) b.local.scale.y = 1;
        s->bones.push_back(std::move(b));
        *out = static_cast<uint32_t>(s->bones.size() - 1);
        return JFX_SUCCESS;
    } catch (...) { return JFX_ERROR_OUT_OF_MEMORY; }
}
jfx_result_t jfx_animation_scene_add_key(jfx_animation_scene_t *s, const jfx_animation_key_t *k) {
    if (!s || !k || k->size < sizeof(*k) || k->bone >= s->bones.size() || k->property > 4 ||
        !valid_curve(k->curve) || !std::isfinite(k->time) || k->time < 0 || !std::isfinite(k->value) ||
        s->keys.size() >= UINT32_MAX) return JFX_ERROR_INVALID_ARGUMENT;
    try { s->keys.push_back({*k}); return JFX_SUCCESS; }
    catch (...) { return JFX_ERROR_OUT_OF_MEMORY; }
}
jfx_result_t jfx_animation_scene_info(const jfx_animation_scene_t *s, jfx_animation_scene_info_t *out) {
    if (!s || !out || out->size < sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    out->size = sizeof(*out);
    out->bone_count = static_cast<uint32_t>(s->bones.size());
    out->key_count = static_cast<uint32_t>(s->keys.size());
    out->duration = 0;
    for (const auto &k : s->keys) out->duration = std::max(out->duration, k.k.time);
    return JFX_SUCCESS;
}
jfx_result_t jfx_animation_scene_evaluate(const jfx_animation_scene_t *s, double t, jfx_animation_pose_t *out) {
    if (!s || !out || out->size < sizeof(*out) || !out->world_transforms ||
        out->bone_count < s->bones.size() || !std::isfinite(t) || t < 0) return JFX_ERROR_INVALID_ARGUMENT;
    try {
        tilly::vector<jfx_animation_transform_t> pose;
        evaluate_internal(s, t, pose);
        if (!pose.empty()) std::memcpy(out->world_transforms, pose.data(), pose.size() * sizeof(pose[0]));
        out->bone_count = static_cast<uint32_t>(pose.size());
        return JFX_SUCCESS;
    } catch (...) { return JFX_ERROR_OUT_OF_MEMORY; }
}
jfx_result_t jfx_animation_compile(const jfx_animation_scene_t *s, uint8_t **out, size_t *n) {
    if (!s || !out || !n) return JFX_ERROR_INVALID_ARGUMENT;
    try {
        tilly::vector<uint8_t> b = {'J', 'F', 'A', '1'};
        put32(b, JFX_ANIMATION_BYTECODE_VERSION);
        put32(b, static_cast<uint32_t>(s->bones.size()));
        put32(b, static_cast<uint32_t>(s->keys.size()));
        for (const auto &x : s->bones) {
            put32(b, x.parent);
            for (float v : {x.local.translation.x, x.local.translation.y, x.local.rotation, x.local.scale.x, x.local.scale.y}) putf(b, v);
        }
        for (const auto &x : s->keys) {
            put32(b, x.k.bone);
            b.push_back(x.k.property); b.push_back(static_cast<uint8_t>(x.k.curve));
            b.push_back(0); b.push_back(0); putd(b, x.k.time); putf(b, x.k.value);
        }
        auto *bytes = static_cast<uint8_t *>(tilly_mem_alloc(b.size()));
        if (!bytes) return JFX_ERROR_OUT_OF_MEMORY;
        std::memcpy(bytes, b.data(), b.size());
        *out = bytes; *n = b.size();
        return JFX_SUCCESS;
    } catch (...) { return JFX_ERROR_OUT_OF_MEMORY; }
}
jfx_result_t jfx_animation_validate(const uint8_t *b, size_t n) {
    if (!b || n < 16 || std::memcmp(b, "JFA1", 4) || get32(b + 4) != JFX_ANIMATION_BYTECODE_VERSION)
        return JFX_ERROR_VERSION_MISMATCH;
    uint32_t nb = get32(b + 8), nk = get32(b + 12);
    uint64_t need = 16ull + 24ull * nb + 20ull * nk;
    if (need != n) return JFX_ERROR_INVALID_ARGUMENT;
    size_t p = 16;
    for (uint32_t i = 0; i < nb; ++i, p += 24) {
        uint32_t parent = get32(b + p);
        if (parent != UINT32_MAX && parent >= i) return JFX_ERROR_INVALID_ARGUMENT;
        for (size_t field = 4; field < 24; field += 4)
            if (!std::isfinite(getf(b + p + field))) return JFX_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < nk; ++i, p += 20) {
        if (get32(b + p) >= nb || b[p + 4] > 4 || !valid_curve(static_cast<jfx_animation_curve_t>(b[p + 5])) ||
            b[p + 6] || b[p + 7] || !std::isfinite(getd(b + p + 8)) || getd(b + p + 8) < 0 ||
            !std::isfinite(getf(b + p + 16))) return JFX_ERROR_INVALID_ARGUMENT;
    }
    return JFX_SUCCESS;
}
jfx_result_t jfx_animation_play(const uint8_t *b, size_t n, double t, jfx_animation_pose_t *out) {
    if (!b || !out) return JFX_ERROR_INVALID_ARGUMENT;
    auto result = jfx_animation_validate(b, n);
    if (result != JFX_SUCCESS) return result;
    tilly::unique_ptr<jfx_animation_scene> s(jfx_animation_scene_create());
    if (!s) return JFX_ERROR_OUT_OF_MEMORY;
    size_t p = 16;
    uint32_t nb = get32(b + 8), nk = get32(b + 12);
    for (uint32_t i = 0; i < nb; ++i, p += 24) {
        jfx_animation_bone_desc_t d{sizeof(d), nullptr, get32(b + p),
            {{getf(b + p + 4), getf(b + p + 8)}, getf(b + p + 12), {getf(b + p + 16), getf(b + p + 20)}}};
        uint32_t x;
        result = jfx_animation_scene_add_bone(s.get(), &d, &x);
        if (result != JFX_SUCCESS) return result;
    }
    for (uint32_t i = 0; i < nk; ++i, p += 20) {
        jfx_animation_key_t k{sizeof(k), get32(b + p), b[p + 4], getd(b + p + 8), getf(b + p + 16),
            static_cast<jfx_animation_curve_t>(b[p + 5])};
        result = jfx_animation_scene_add_key(s.get(), &k);
        if (result != JFX_SUCCESS) return result;
    }
    return jfx_animation_scene_evaluate(s.get(), t, out);
}
void jfx_animation_bytes_destroy(uint8_t *b) { tilly_mem_free(b); }
jfx_animation_tab_t *jfx_animation_tab_create(uint32_t w, uint32_t h) {
    if (!w || !h) return nullptr;
    auto *tab = tilly::create<jfx_animation_tab>();
    if (!tab) return nullptr;
    tab->width = w; tab->height = h; tab->scene = jfx_animation_scene_create();
    if (!tab->scene) { tilly::destroy(tab); return nullptr; }
    return tab;
}
void jfx_animation_tab_destroy(jfx_animation_tab_t *t) {
    if (t) { jfx_animation_scene_destroy(t->scene); tilly::destroy(t); }
}
jfx_animation_scene_t *jfx_animation_tab_scene(jfx_animation_tab_t *t) { return t ? t->scene : nullptr; }
jfx_result_t jfx_animation_tab_set_time(jfx_animation_tab_t *t, double x) {
    if (!t || !std::isfinite(x) || x < 0) return JFX_ERROR_INVALID_ARGUMENT;
    t->time = x; return JFX_SUCCESS;
}
double jfx_animation_tab_time(const jfx_animation_tab_t *t) { return t ? t->time : 0; }
jfx_result_t jfx_animation_tab_tick(jfx_animation_tab_t *t, double d) {
    if (!t || !std::isfinite(d) || d < 0 || (t->playing && !std::isfinite(t->time + d))) return JFX_ERROR_INVALID_ARGUMENT;
    if (t->playing) t->time += d;
    return JFX_SUCCESS;
}
jfx_result_t jfx_animation_tab_set_playing(jfx_animation_tab_t *t, bool p) {
    if (!t) return JFX_ERROR_INVALID_ARGUMENT;
    t->playing = p; return JFX_SUCCESS;
}
bool jfx_animation_tab_playing(const jfx_animation_tab_t *t) { return t && t->playing; }
jfx_result_t jfx_animation_tab_pose(const jfx_animation_tab_t *t, jfx_animation_pose_t *out) {
    return t ? jfx_animation_scene_evaluate(t->scene, t->time, out) : JFX_ERROR_INVALID_ARGUMENT;
}
} // extern "C"
