#include <cstdio>
#include <cstring>

#include "jfx/desktop_frontend.h"

int main(int argc, char **argv) {
    const char *backend = "auto";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) backend = argv[++i];
        else if (std::strcmp(argv[i], "--headless-smoke") != 0) {
            std::fprintf(stderr, "usage: jfx_desktop [--backend NAME] [--headless-smoke]\n");
            return 1;
        }
    }
    jfx_desktop_frontend_config_t config{1280, 720, backend};
    jfx_desktop_frontend_t *frontend = nullptr;
    if (jfx_desktop_frontend_create(&config, &frontend) != JFX_SUCCESS ||
        jfx_desktop_frontend_draw(frontend) != JFX_SUCCESS) {
        std::fprintf(stderr, "failed to initialize desktop frontend\n");
        jfx_desktop_frontend_destroy(frontend);
        return 1;
    }
    std::printf("desktop frontend ready (backend %s)\n", backend);
    jfx_desktop_frontend_destroy(frontend);
    return 0;
}
