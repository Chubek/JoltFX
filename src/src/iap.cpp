/* Ingagi Animation Pack writer/inspector. See jfx_iap.h for the layout.
 * Uses Tilly containers for buffers, the engine's atomic-replace ExportFile
 * pattern for output, and a small IEEE CRC32 so truncated/corrupt packs fail
 * fast without a scene load. No threads, no GPU, no allocations on failure
 * paths beyond the bounded pack buffer. */
#include "jfx/jfx_iap.h"
#include "jfx/jfx_modeling3d.h"
#include "tilly/memory.hpp"
#include "iap_decode.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
using Blob = tilly::vector<uint8_t>;
using jfx_iap_detail::crc32;

void put_u8(Blob &b, uint8_t v) { b.push_back(v); }
void put_u32(Blob &b, uint32_t v) {
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
    b.push_back(uint8_t((v >> 16) & 0xFF));
    b.push_back(uint8_t((v >> 24) & 0xFF));
}
void put_u64(Blob &b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(uint8_t((v >> (8 * i)) & 0xFF));
}
void put_f32(Blob &b, float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    put_u32(b, u);
}

/* Locale-independent `key CH FRAME VALUE INTERP` line parser. The scene
 * serializer always writes classic-locale numbers ('.' decimals, optional
 * exponent), but `sscanf`/`strtof` follow the process locale, so a locale
 * with ',' decimals would misparse. This accepts [0-9]+ for the integers
 * and [-+]?digits[.digits][eE][-+]digits for the value. */
bool parse_key_line(const char *line, uint32_t &ch, uint32_t &frame, float &value,
    uint32_t &interp) {
    if (!line || std::strncmp(line, "key ", 4) != 0) return false;
    const char *p = line + 4;
    auto digits = [](const char *&q, uint32_t &v) {
        if (*q < '0' || *q > '9') return false;
        uint64_t acc = 0;
        while (*q >= '0' && *q <= '9') {
            acc = acc * 10 + uint64_t(*q - '0');
            if (acc > 0xFFFFFFFFu) return false;
            ++q;
        }
        v = uint32_t(acc);
        return true;
    };
    uint32_t c = 0, f = 0, it = 0;
    if (!digits(p, c) || *p++ != ' ') return false;
    if (!digits(p, f) || *p++ != ' ') return false;
    const char *start = p;
    if (*p == '-' || *p == '+') ++p;
    bool have_digit = false;
    while (*p >= '0' && *p <= '9') {
        have_digit = true;
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            have_digit = true;
            ++p;
        }
    }
    if (!have_digit) return false;
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '-' || *p == '+') ++p;
        bool have_exp = false;
        while (*p >= '0' && *p <= '9') {
            have_exp = true;
            ++p;
        }
        if (!have_exp) return false;
    }
    if (*p++ != ' ') return false;
    if (!digits(p, it) || *p != '\0') return false;
    /* Convert the already-validated float token without locale input. */
    double whole = 0, frac = 0, scale = 1;
    int sign = 1;
    const char *q = start;
    if (*q == '-') {
        sign = -1;
        ++q;
    } else if (*q == '+') {
        ++q;
    }
    while (*q >= '0' && *q <= '9') whole = whole * 10 + (*q++ - '0');
    if (*q == '.') {
        ++q;
        while (*q >= '0' && *q <= '9') {
            frac = frac * 10 + (*q++ - '0');
            scale *= 10;
        }
    }
    double v = sign * (whole + frac / scale);
    if (*q == 'e' || *q == 'E') {
        ++q;
        int esign = 1;
        if (*q == '-') {
            esign = -1;
            ++q;
        } else if (*q == '+') {
            ++q;
        }
        int exp = 0;
        while (*q >= '0' && *q <= '9') exp = exp * 10 + (*q++ - '0');
        double mag = 1;
        for (int i = 0; i < exp; ++i) mag *= 10;
        v = esign < 0 ? v / mag : v * mag;
    }
    if (!std::isfinite(v) || v > 3.402823466e38 || v < -3.402823466e38) return false;
    ch = c;
    frame = f;
    value = float(v);
    interp = it;
    return true;
}

/* Own only an exclusively created sibling; cleanup also runs on errors. */
struct ExportFile {
    tilly::string temporary;
    FILE *file = nullptr;
    explicit ExportFile(const char *path) {
        for (unsigned slot = 0; slot < 1000; ++slot) {
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), ".iap-part-%u", slot);
            temporary = tilly::string(path) + suffix;
            file = std::fopen(temporary.c_str(), "wbx");
            if (file) return;
            if (errno != EEXIST) break;
        }
        temporary.clear();
    }
    ~ExportFile() {
        if (file) std::fclose(file);
        if (!temporary.empty()) std::remove(temporary.c_str());
    }
    ExportFile(const ExportFile &) = delete;
    ExportFile &operator=(const ExportFile &) = delete;
    bool commit(const char *path) {
        if (!file) return false;
        bool ok = !std::ferror(file);
        if (std::fclose(file) != 0) ok = false;
        file = nullptr;
        if (!ok) return false;
#ifdef _WIN32
        ok = MoveFileExA(temporary.c_str(), path,
                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = std::rename(temporary.c_str(), path) == 0;
#endif
        if (ok) temporary.clear();
        return ok;
    }
};

struct Parsed {
    uint32_t fps = 0, frames = 0, objects = 0, assets = 0;
};

jfx_result_t parse(const uint8_t *d, size_t n, Parsed *summary) {
    jfx_iap_detail::Pack pack;
    if (!jfx_iap_detail::decode(d, n, pack, JFX_IAP_MAX_ASSETS, JFX_IAP_MAX_ASSET_BYTES,
            JFX_IAP_MAX_TOTAL_BYTES))
        return JFX_ERROR_INVALID_ARGUMENT;
    if (summary) {
        summary->fps = pack.fps;
        summary->frames = pack.frames;
        summary->objects = uint32_t(pack.objects.size());
        summary->assets = pack.assets;
    }
    return JFX_SUCCESS;
}

Blob read_all(const char *path, jfx_result_t *status) {
    Blob out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (status) *status = JFX_ERROR_NOT_FOUND;
        return out;
    }
    in.seekg(0, std::ios::end);
    auto end = in.tellg();
    if (end < 0 || uint64_t(end) > JFX_IAP_MAX_TOTAL_BYTES + 1024 * 1024) {
        if (status) *status = JFX_ERROR_INVALID_ARGUMENT;
        return out;
    }
    in.seekg(0);
    out.resize(size_t(end));
    if (!out.empty() && end > 0) {
        in.read(reinterpret_cast<char *>(out.data()), end);
        if (!in) {
            out.clear();
            if (status) *status = JFX_ERROR_INVALID_ARGUMENT;
            return out;
        }
    }
    if (status) *status = JFX_SUCCESS;
    return out;
}
}  // namespace

extern "C" jfx_result_t jfx_iap_export(const jfx_scene3d_t *scene,
    const char *const *asset_paths, size_t asset_count, const char *path) {
    if (!scene || !path || !*path) return JFX_ERROR_INVALID_ARGUMENT;
    if (asset_count > JFX_IAP_MAX_ASSETS) return JFX_ERROR_INVALID_ARGUMENT;
    if (asset_count && !asset_paths) return JFX_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < asset_count; ++i)
        if (!asset_paths[i] || !*asset_paths[i] || std::strlen(asset_paths[i]) > 127)
            return JFX_ERROR_INVALID_ARGUMENT;
    try {
        tilly::vector<char> state(1024 * 1024);
        if (jfx_scene3d_state(scene, state.data(), state.size()) != JFX_SUCCESS)
            return JFX_ERROR_INVALID_ARGUMENT;
        /* Snapshot the scene text first; serialization validates bounds. */
        tilly::vector<char> text(8 * 1024 * 1024);
        size_t written = 0;
        if (jfx_scene3d_save(scene, text.data(), text.size(), &written) != JFX_SUCCESS)
            return JFX_ERROR_INVALID_ARGUMENT;
        Blob blob;
        blob.reserve(1024);
        blob.insert(blob.end(), {'I', 'A', 'P', '1'});
        uint32_t fps = jfx_scene3d_fps(scene);
        uint32_t frames = jfx_scene3d_frames(scene);
        uint32_t objects = jfx_scene3d_object_count(scene);
        put_u32(blob, JFX_IAP_VERSION);
        put_u32(blob, fps);
        put_u32(blob, frames);
        put_u32(blob, objects);
        for (uint32_t o = 0; o < objects; ++o) {
            jfx_object3d_info_t info{};
            info.size = sizeof(info);
            jfx_procedural3d_info_t proc{};
            proc.size = sizeof(proc);
            if (jfx_scene3d_object_info(scene, o, &info) != JFX_SUCCESS ||
                jfx_scene3d_procedural_info(scene, o, &proc) != JFX_SUCCESS)
                return JFX_ERROR_BACKEND_FAILURE;
            size_t name_len = std::strlen(info.name);
            if (name_len > 127) return JFX_ERROR_INVALID_ARGUMENT;
            put_u8(blob, uint8_t(name_len));
            blob.insert(blob.end(), info.name, info.name + name_len);
            put_u32(blob, proc.cloner_mode);
            put_u32(blob, proc.instances);
            put_f32(blob, proc.spacing);
            /* Keys are exposed through state JSON; re-parse the saved text
             * for exact (channel, frame, value, interp) ordering by scanning
             * the object's key lines. Simpler and exact: parse state JSON is
             * fragile, so walk the serialization segment for this object. */
            (void)info;
            /* Collect keys via sampling-independent path: use scene text. */
            tilly::vector<uint32_t> channels;
            tilly::vector<uint32_t> key_frames;
            tilly::vector<float> key_values;
            tilly::vector<uint32_t> key_interps;
            /* The scene text is canonical; extract this object's keys by
             * re-scanning lines between its `object` and `end`. */
            tilly::string all(text.data(), written);
            size_t pos = 0, seen = 0;
            bool inside = false;
            while (pos < all.size()) {
                size_t eol = all.find('\n', pos);
                if (eol == tilly::string::npos) eol = all.size();
                tilly::string line = all.substr(pos, eol - pos);
                pos = eol + 1;
                if (line.compare(0, 7, "object ") == 0) {
                    if (seen == o) inside = true;
                    else if (inside) break;
                    if (seen != o) {
                        ++seen;
                        continue;
                    }
                }
                if (inside && line.compare(0, 4, "key ") == 0) {
                    uint32_t ch = 0, fr = 0, it = 0;
                    float v = 0;
                    if (!parse_key_line(line.c_str(), ch, fr, v, it))
                        return JFX_ERROR_BACKEND_FAILURE;
                    channels.push_back(ch);
                    key_frames.push_back(fr);
                    key_values.push_back(v);
                    key_interps.push_back(it);
                }
                if (inside && line == "end") break;
                if (!inside && line.compare(0, 7, "object ") == 0) {
                    /* counted above */
                }
            }
            /* Account for the object header line itself in `seen`. */
            (void)channels;
            put_u32(blob, uint32_t(key_frames.size()));
            for (size_t k = 0; k < key_frames.size(); ++k) {
                put_u32(blob, channels[k]);
                put_u32(blob, key_frames[k]);
                put_f32(blob, key_values[k]);
                put_u32(blob, key_interps[k]);
            }
        }
        put_u32(blob, uint32_t(asset_count));
        uint64_t total = 0;
        for (size_t a = 0; a < asset_count; ++a) {
            const char *ap = asset_paths[a];
            size_t plen = std::strlen(ap);
            std::ifstream in(ap, std::ios::binary);
            if (!in) return JFX_ERROR_NOT_FOUND;
            in.seekg(0, std::ios::end);
            auto end = in.tellg();
            if (end < 0 || uint64_t(end) > JFX_IAP_MAX_ASSET_BYTES)
                return JFX_ERROR_INVALID_ARGUMENT;
            in.seekg(0);
            Blob bytes;
            bytes.resize(size_t(end));
            if (!bytes.empty()) {
                in.read(reinterpret_cast<char *>(bytes.data()), end);
                if (!in) return JFX_ERROR_INVALID_ARGUMENT;
            }
            total += uint64_t(bytes.size());
            if (total > JFX_IAP_MAX_TOTAL_BYTES) return JFX_ERROR_INVALID_ARGUMENT;
            put_u8(blob, uint8_t(plen));
            blob.insert(blob.end(), ap, ap + plen);
            put_u64(blob, uint64_t(bytes.size()));
            blob.insert(blob.end(), bytes.begin(), bytes.end());
        }
        uint32_t crc = crc32(blob.data(), blob.size());
        put_u32(blob, crc);
        ExportFile out(path);
        if (!out.file) return JFX_ERROR_BACKEND_FAILURE;
        bool ok = blob.empty() ||
            std::fwrite(blob.data(), 1, blob.size(), out.file) == blob.size();
        if (!ok || !out.commit(path)) return JFX_ERROR_BACKEND_FAILURE;
        Parsed check;
        if (parse(blob.data(), blob.size(), &check) != JFX_SUCCESS)
            return JFX_ERROR_BACKEND_FAILURE;
        return JFX_SUCCESS;
    } catch (const std::bad_alloc &) {
        return JFX_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
}

extern "C" jfx_result_t jfx_iap_info(const char *path, uint32_t *out_fps,
    uint32_t *out_frames, uint32_t *out_objects, uint32_t *out_assets,
    uint64_t *out_bytes) {
    if (!path || !*path) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_result_t status = JFX_SUCCESS;
    Blob blob = read_all(path, &status);
    if (status != JFX_SUCCESS) return status;
    Parsed p;
    jfx_result_t r = parse(blob.data(), blob.size(), &p);
    if (r != JFX_SUCCESS) return r;
    if (out_fps) *out_fps = p.fps;
    if (out_frames) *out_frames = p.frames;
    if (out_objects) *out_objects = p.objects;
    if (out_assets) *out_assets = p.assets;
    if (out_bytes) *out_bytes = uint64_t(blob.size());
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_iap_validate(const char *path) {
    if (!path || !*path) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_result_t status = JFX_SUCCESS;
    Blob blob = read_all(path, &status);
    if (status != JFX_SUCCESS) return status;
    return parse(blob.data(), blob.size(), nullptr);
}
