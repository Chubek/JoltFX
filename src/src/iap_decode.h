/* Private .iap decoder shared by the pack writer/inspector (iap.cpp) and the
 * scene importer (modeling3d.cpp). Not installed; same-directory include.
 * All bounds/order/CRC rules match jfx_iap.h v1. Decode validates the whole
 * blob including the asset walk and trailing CRC; asset bytes are skipped
 * (counted only) so import never copies up to 64 MiB it does not need.
 * No threads, no exceptions for bounds (bool return); callers map failure
 * to JFX_ERROR_INVALID_ARGUMENT. */
#ifndef JFX_IAP_DECODE_H
#define JFX_IAP_DECODE_H
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "tilly/memory.hpp"

namespace jfx_iap_detail {
using Blob = tilly::vector<uint8_t>;
struct Key {
    uint32_t channel = 0, frame = 0, interp = 0;
    float value = 0;
};
struct Object {
    tilly::string name;
    uint32_t cloner_mode = 0, instances = 1;
    float spacing = 3;
    tilly::vector<Key> keys;
};
struct Pack {
    uint32_t fps = 0, frames = 0, assets = 0;
    tilly::vector<Object> objects;
};
inline uint32_t crc32(const uint8_t *data, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
inline bool get_u8(const uint8_t *d, size_t n, size_t &at, uint8_t &v) {
    if (at + 1 > n) return false;
    v = d[at++];
    return true;
}
inline bool get_u32(const uint8_t *d, size_t n, size_t &at, uint32_t &v) {
    if (at + 4 > n) return false;
    v = uint32_t(d[at]) | (uint32_t(d[at + 1]) << 8) |
        (uint32_t(d[at + 2]) << 16) | (uint32_t(d[at + 3]) << 24);
    at += 4;
    return true;
}
inline bool get_u64(const uint8_t *d, size_t n, size_t &at, uint64_t &v) {
    if (at + 8 > n) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(d[at + i]) << (8 * i);
    at += 8;
    return true;
}
inline bool get_f32(const uint8_t *d, size_t n, size_t &at, float &v) {
    uint32_t u = 0;
    if (!get_u32(d, n, at, u)) return false;
    std::memcpy(&v, &u, 4);
    return true;
}
inline bool skip_bytes(const uint8_t *d, size_t n, size_t &at, size_t count) {
    if (count > n || at + count > n) return false;
    at += count;
    return true;
}
/* Full validation; on success fills `out`. Asset payloads are walked but
 * not stored. `JFX_IAP_MAX_*` bounds are passed by the caller so this
 * header stays independent of the public header. */
inline bool decode(const uint8_t *d, size_t n, Pack &out, uint32_t max_assets,
    uint64_t max_asset_bytes, uint64_t max_total_bytes, uint32_t max_objects = 64,
    uint32_t max_keys = 4096) {
    if (!d || n < 20) return false;
    size_t at = 0;
    if (std::memcmp(d, "IAP1", 4) != 0) return false;
    at = 4;
    uint32_t version = 0, fps = 0, frames = 0, objects = 0;
    if (!get_u32(d, n, at, version) || version != 1) return false;
    if (!get_u32(d, n, at, fps) || fps < 1 || fps > 240) return false;
    if (!get_u32(d, n, at, frames) || frames < 1 || frames > 36000) return false;
    if (!get_u32(d, n, at, objects) || objects > max_objects) return false;
    Pack pack;
    pack.fps = fps;
    pack.frames = frames;
    for (uint32_t o = 0; o < objects; ++o) {
        uint8_t name_len = 0;
        if (!get_u8(d, n, at, name_len) || name_len > 127) return false;
        if (at + name_len > n) return false;
        for (size_t i = 0; i < name_len; ++i) {
            unsigned char c = d[at + i];
            if (c < 32 || c == 0) return false;
        }
        Object object;
        object.name.assign(reinterpret_cast<const char *>(d + at), name_len);
        if (!skip_bytes(d, n, at, name_len)) return false;
        uint32_t mode = 0, instances = 0, keys = 0;
        float spacing = 0;
        if (!get_u32(d, n, at, mode) || mode > 3) return false;
        if (!get_u32(d, n, at, instances) || instances < 1 || instances > 64 ||
            ((mode == 0) != (instances == 1)))
            return false;
        if (!get_f32(d, n, at, spacing) || !(spacing >= 0.01f && spacing <= 1000.0f))
            return false;
        if (!get_u32(d, n, at, keys) || keys > max_keys) return false;
        object.cloner_mode = mode;
        object.instances = instances;
        object.spacing = spacing;
        uint64_t last = 0;
        bool first = true;
        for (uint32_t k = 0; k < keys; ++k) {
            Key key;
            if (!get_u32(d, n, at, key.channel) || key.channel > 8) return false;
            if (!get_u32(d, n, at, key.frame) || key.frame >= frames) return false;
            if (!get_f32(d, n, at, key.value) || !std::isfinite(key.value)) return false;
            if (!get_u32(d, n, at, key.interp) || key.interp > 2) return false;
            uint64_t order = uint64_t(key.channel) * 36000 + key.frame;
            if (!first && order <= last) return false;
            last = order;
            first = false;
            object.keys.push_back(key);
        }
        pack.objects.push_back(object);
    }
    uint32_t assets = 0;
    if (!get_u32(d, n, at, assets) || assets > max_assets) return false;
    uint64_t total = 0;
    for (uint32_t a = 0; a < assets; ++a) {
        uint8_t path_len = 0;
        if (!get_u8(d, n, at, path_len) || !path_len || path_len > 127) return false;
        if (at + path_len > n) return false;
        for (size_t i = 0; i < path_len; ++i)
            if (d[at + i] == 0) return false;
        if (!skip_bytes(d, n, at, path_len)) return false;
        uint64_t size = 0;
        if (!get_u64(d, n, at, size) || size > max_asset_bytes) return false;
        total += size;
        if (total > max_total_bytes) return false;
        if (!skip_bytes(d, n, at, size_t(size))) return false;
    }
    if (at + 4 != n) return false;
    uint32_t le = uint32_t(d[n - 4]) | (uint32_t(d[n - 3]) << 8) |
        (uint32_t(d[n - 2]) << 16) | (uint32_t(d[n - 1]) << 24);
    if (le != crc32(d, n - 4)) return false;
    pack.assets = assets;
    out = pack;
    return true;
}
}  // namespace jfx_iap_detail
#endif
