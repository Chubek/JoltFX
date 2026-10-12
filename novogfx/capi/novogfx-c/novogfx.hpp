/**
 * @file novogfx.hpp
 * @brief Convenience C++ API over the novogfx C ABI, with a DSLtk-native DSL.
 *
 * Implements a C++-native DSL using `DSLtk.hpp` (see `novogfx/DSLtk.hpp`):
 * RAII handles, `operator|` pipelines for atomic batches, and composable
 * predicates for mode/connector selection.
 *
 * Requires C++20. Header-only; link against `novogfx-c` (cdylib/staticlib).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "novogfx.h"

// Adjust to your checkout layout; DSLtk.hpp ships at novogfx/DSLtk.hpp.
#if __has_include("DSLtk.hpp")
#include "DSLtk.hpp"
#elif __has_include("novogfx/DSLtk.hpp")
#include "novogfx/DSLtk.hpp"
#else
#error "NovoGFX requires the shipped DSLtk.hpp header"
#endif

namespace novogfx {

inline void check(novogfx_status_t st, const char *what) {
    if (st != NOVOGFX_OK) {
        throw std::runtime_error(std::string(what) + ": " + novogfx_status_string(st));
    }
}

/** RAII device. */
class Device {
public:
    explicit Device(const char *path) {
        novogfx_device_t *raw = nullptr;
        check(novogfx_device_open(path, &raw), "novogfx_device_open");
        handle_.reset(raw);
    }
    [[nodiscard]] novogfx_device_t *get() const noexcept { return handle_.get(); }
    [[nodiscard]] unsigned long caps() const noexcept { return novogfx_device_caps(get()); }
    void enable_atomic() const { check(novogfx_device_enable_atomic(get()), "enable_atomic"); }
    [[nodiscard]] int fd() const noexcept { return novogfx_device_fd(get()); }

private:
    struct Deleter { void operator()(novogfx_device_t *p) const noexcept { novogfx_device_destroy(p); } };
    std::unique_ptr<novogfx_device_t, Deleter> handle_;
};

/** Snapshot of a mode. */
struct Mode {
    uint32_t clock_khz = 0;
    uint16_t hdisplay = 0, vdisplay = 0;
    uint32_t flags = 0, type = 0;
    std::string name;
    [[nodiscard]] bool preferred() const noexcept { return (type & (1u << 3)) != 0; }
    static Mode from_raw(const novogfx_mode_t &m) {
        Mode out;
        out.clock_khz = static_cast<uint32_t>(m.clock_khz);
        out.hdisplay = m.hdisplay;
        out.vdisplay = m.vdisplay;
        out.flags = static_cast<uint32_t>(m.flags);
        out.type = static_cast<uint32_t>(m.type);
        out.name = m.name ? m.name : "";
        return out;
    }
};

/** Atomic batch DSL: pipe stages into a request via `operator|`.
 *
 * Example:
 * @code
 *   Device dev("/dev/dri/card0");
 *   dev.enable_atomic();
 *   Batch batch;
 *   batch.couple(crtc, fb, mode, connector);
 *   batch.commit(dev, NOVOGFX_COMMIT_ALLOW_MODESET);
 * @endcode
 */
struct Dsl : dsl::DSL<Dsl, dsl::Pipeline, dsl::Operators> {};

/** Fluent atomic-batch builder (usable with or without DSLtk pipelines). */
class Batch {
public:
    struct CrtcStage {
        novogfx_crtc_t *crtc = nullptr;
        novogfx_framebuffer_t *fb = nullptr;
        std::optional<novogfx_mode_t> mode;
        novogfx_connector_t *connector = nullptr;
        /** Owned mode name, retained when stages are copied or moved. */
        std::string mode_name;

        void capture_mode_name() {
            if (mode && mode->name) {
                mode_name = mode->name;
                mode->name = nullptr;
            }
        }
    };
    struct PlaneStage {
        novogfx_plane_update_t update{};
    };

    Batch &couple(novogfx_crtc_t *crtc, novogfx_framebuffer_t *fb,
                  const novogfx_mode_t *mode = nullptr,
                  novogfx_connector_t *connector = nullptr) {
        CrtcStage s;
        s.crtc = crtc;
        s.fb = fb;
        if (mode) s.mode = *mode;
        s.connector = connector;
        s.capture_mode_name();
        crtcs_.push_back(s);
        return *this;
    }
    Batch &plane(const novogfx_plane_update_t &u) {
        PlaneStage s;
        s.update = u;
        planes_.push_back(s);
        return *this;
    }
    /** Pipe-style chaining: `batch | stage`. */
    friend Batch operator|(Batch b, CrtcStage s) {
        s.capture_mode_name();
        b.crtcs_.push_back(s);
        return b;
    }
    friend Batch operator|(Batch b, PlaneStage s) {
        b.planes_.push_back(s);
        return b;
    }

    void commit(Device &dev, unsigned long flags) const {
        novogfx_atomic_request_t *req = nullptr;
        check(novogfx_atomic_request_create(dev.get(), &req), "atomic_request_create");
        const auto destroy = [](novogfx_atomic_request_t *p) { novogfx_atomic_request_destroy(p); };
        std::unique_ptr<novogfx_atomic_request_t, decltype(destroy)> owner(req, destroy);
        for (auto &c : crtcs_) {
            auto mode = c.mode;
            if (mode) mode->name = c.mode_name.c_str();
            const novogfx_mode_t *mp = mode ? &*mode : nullptr;
            check(novogfx_atomic_request_set_crtc(req, c.crtc, c.fb, mp, c.connector), "set_crtc");
        }
        for (auto &p : planes_) {
            check(novogfx_atomic_request_add_plane(req, &p.update), "add_plane");
        }
        novogfx_status_t st = novogfx_atomic_request_commit(req, flags);
        check(st, "atomic commit");
    }

private:
    std::vector<CrtcStage> crtcs_;
    std::vector<PlaneStage> planes_;
};

} // namespace novogfx
