#include <cassert>
#include <cstring>

#include "jfx/desktop_frontend.h"

int main() {
    jfx_desktop_frontend_t *frontend = nullptr;
    jfx_desktop_frontend_config_t config{};
    config.width = 1280;
    config.height = 720;
    config.backend_name = "webgpu";
    assert(jfx_desktop_frontend_create(&config, &frontend) == JFX_SUCCESS);
    assert(frontend != nullptr);
    assert(jfx_desktop_frontend_open_project(frontend, "demo.jolt") == JFX_SUCCESS);
    assert(std::strcmp(jfx_desktop_frontend_project_path(frontend), "demo.jolt") == 0);
    assert(jfx_desktop_frontend_resize(frontend, 800, 600) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_play(frontend) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_seek(frontend, 1.25) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_draw(frontend) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_pause(frontend) == JFX_SUCCESS);
    jfx_desktop_frontend_destroy(frontend);
    return 0;
}
