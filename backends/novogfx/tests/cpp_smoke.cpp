// C++ ownership/DSL checks. Optional hardware path uses TEST_ONLY exclusively.
#include "novogfx.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main() {
    novogfx::Dsl dsl;
    (void)dsl;
    novogfx_mode_t empty{};
    assert(novogfx::Mode::from_raw(empty).name.empty());
    novogfx::Batch::CrtcStage stage;
    {
        std::string name(128, 'x');
        empty.name = name.c_str();
        stage.mode = empty;
        stage.capture_mode_name();
    }
    auto moved = std::move(stage);
    assert(moved.mode_name == std::string(128, 'x'));
    assert(moved.mode->name == nullptr);

    const char *path = std::getenv("NOVOGFX_TEST_DEVICE");
    if (path) {
        novogfx::Device device(path);
        device.enable_atomic();
        novogfx_resources_t *raw = nullptr;
        novogfx::check(novogfx_device_get_resources(device.get(), &raw), "resources");
        auto release = [](novogfx_resources_t *p) { novogfx_resources_destroy(p); };
        std::unique_ptr<novogfx_resources_t, decltype(release)> resources(raw, release);
        for (unsigned long i = 0; i < raw->num_connectors && raw->num_crtcs; ++i) {
            auto *connector = raw->connectors[i];
            novogfx_connector_status_t status;
            novogfx::check(novogfx_connector_status(connector, &status), "status");
            const auto *mode = novogfx_connector_mode(connector, 0);
            if (status != NOVOGFX_CONNECTOR_CONNECTED || !mode) continue;
            novogfx_dumb_buffer_desc_t desc{mode->hdisplay, mode->vdisplay, 32, 0};
            novogfx_dumb_buffer_t *buffer = nullptr;
            novogfx_framebuffer_t *fb = nullptr;
            novogfx::check(novogfx_dumb_buffer_create(device.get(), &desc, &buffer, &fb), "buffer");
            auto destroy = [](novogfx_dumb_buffer_t *p) { novogfx_dumb_buffer_destroy(p); };
            std::unique_ptr<novogfx_dumb_buffer_t, decltype(destroy)> owner(buffer, destroy);
            novogfx::Batch batch;
            {
                auto temporary = *mode;
                std::string name = std::string(mode->name) + std::string(96, 'x');
                temporary.name = name.c_str();
                batch.couple(raw->crtcs[0], fb, &temporary, connector);
            } // The original mode name is freed before request construction.
            auto copy = batch;
            try {
                copy.commit(device, NOVOGFX_COMMIT_TEST_ONLY | NOVOGFX_COMMIT_ALLOW_MODESET);
            } catch (const std::runtime_error &error) {
                // Driver validation can fail without DRM master. Stage assembly
                // and name conversion must have succeeded before that call.
                assert(std::string(error.what()).find("atomic commit:") == 0);
                std::cout << "TEST_ONLY driver result: " << error.what() << '\n';
            }
            break;
        }
    }
    std::cout << "C++ ownership/DSL checks passed\n";
}
