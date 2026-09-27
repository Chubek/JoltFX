/* JoltFX desktop frontend entry point.
 *
 * Opens the editor window and runs it until the user quits. `--headless-smoke`
 * composes frames with no window and exits, which is what CI and the unit test
 * use; it is not a substitute for the real window path. */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "jfx/desktop_frontend.h"

namespace {

void print_usage(void) {
    std::printf(
        "jfx_desktop - JoltFX desktop editor\n"
        "\n"
        "Usage: jfx_desktop [options]\n"
        "\n"
        "Options:\n"
        "  --backend NAME     Engine backend: vulkan, metal, d3d12, webgpu, auto\n"
        "  --width N         Window width in pixels (default %u)\n"
        "  --height N        Window height in pixels (default %u)\n"
        "  --effect NAME     Bundled effect to preview (default brightness)\n"
        "  --param VALUE     Effect parameter, clamped to its documented range\n"
        "  --project FILE    .jolt kernel to open at startup\n"
        "  --frames N        Exit after N frames (0, the default, runs until quit)\n"
        "  --duration SEC    Exit after SEC seconds of wall clock\n"
        "  --headless-smoke  Compose N frames with no window, then exit\n"
        "  -h, --help        Show this help\n",
        JFX_DESKTOP_DEFAULT_WIDTH, JFX_DESKTOP_DEFAULT_HEIGHT);
}

bool parse_uint(const char *text, uint32_t *out) {
    if (!text || !text[0]) {
        return false;
    }
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (*end != '\0' || value > 0xffffffffUL) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

} // namespace

int main(int argc, char **argv) {
    jfx_desktop_frontend_config_t config;
    std::memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.width = JFX_DESKTOP_DEFAULT_WIDTH;
    config.height = JFX_DESKTOP_DEFAULT_HEIGHT;
    const char *backend = "auto";
    uint64_t max_frames = 0;
    double max_seconds = 0.0;
    bool headless = false;
    uint32_t headless_frames = 2;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (std::strcmp(arg, "--backend") == 0 && has_value) {
            backend = argv[++i];
        } else if (std::strcmp(arg, "--width") == 0 && has_value) {
            if (!parse_uint(argv[++i], &config.width) || !config.width) {
                std::fprintf(stderr, "error: --width expects a positive integer\n");
                return 1;
            }
        } else if (std::strcmp(arg, "--height") == 0 && has_value) {
            if (!parse_uint(argv[++i], &config.height) || !config.height) {
                std::fprintf(stderr, "error: --height expects a positive integer\n");
                return 1;
            }
        } else if (std::strcmp(arg, "--effect") == 0 && has_value) {
            config.effect_name = argv[++i];
        } else if (std::strcmp(arg, "--param") == 0 && has_value) {
            char *end = nullptr;
            config.effect_parameter = (float)std::strtod(argv[++i], &end);
            if (*end != '\0') {
                std::fprintf(stderr, "error: --param expects a number\n");
                return 1;
            }
        } else if (std::strcmp(arg, "--project") == 0 && has_value) {
            config.project_path = argv[++i];
        } else if (std::strcmp(arg, "--frames") == 0 && has_value) {
            uint32_t frames = 0;
            if (!parse_uint(argv[++i], &frames)) {
                std::fprintf(stderr, "error: --frames expects a non-negative integer\n");
                return 1;
            }
            max_frames = frames;
        } else if (std::strcmp(arg, "--duration") == 0 && has_value) {
            char *end = nullptr;
            max_seconds = std::strtod(argv[++i], &end);
            if (*end != '\0' || max_seconds < 0.0) {
                std::fprintf(stderr, "error: --duration expects a non-negative number\n");
                return 1;
            }
        } else if (std::strcmp(arg, "--headless-smoke") == 0) {
            headless = true;
        } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n\n", arg);
            print_usage();
            return 1;
        }
    }

    if (headless) {
        /* Smoke mode always terminates, so a missing or broken frame loop
         * cannot hang a build. */
        max_frames = headless_frames;
    }
    config.backend_name = backend;

    jfx_desktop_frontend_t *frontend = nullptr;
    const jfx_result_t created = jfx_desktop_frontend_create(&config, &frontend);
    if (created != JFX_SUCCESS || !frontend) {
        std::fprintf(stderr, "error: could not create the desktop frontend (%d)\n", (int)created);
        return 1;
    }

    jfx_result_t status;
    if (headless) {
        status = JFX_SUCCESS;
        for (uint32_t frame = 0; frame < headless_frames; ++frame) {
            status = jfx_desktop_frontend_draw(frontend);
            if (status != JFX_SUCCESS) {
                break;
            }
        }
    } else {
        status = jfx_desktop_frontend_run(frontend, max_frames, max_seconds);
        if (status == JFX_ERROR_NOT_INITIALIZED) {
            /* run() only reports this when it could not obtain a window or an
             * OpenGL context. Exit 77 so a CTest run on a host with no display
             * server skips instead of reporting a false failure. */
            jfx_desktop_frontend_destroy(frontend);
            return 77;
        }
    }

    /* Copy what the summary needs out of the frontend: those strings live in
     * memory the destroy below releases. */
    char effect[64];
    char resolved_backend[32];
    const uint64_t frames = jfx_desktop_frontend_frame_count(frontend);
    const char *effect_name = jfx_desktop_frontend_effect_name(frontend);
    const char *backend_name = jfx_desktop_frontend_backend_name(frontend);
    std::snprintf(effect, sizeof(effect), "%s", effect_name ? effect_name : "none");
    std::snprintf(resolved_backend, sizeof(resolved_backend), "%s",
        backend_name ? backend_name : "none");

    jfx_desktop_frontend_destroy(frontend);

    if (status != JFX_SUCCESS) {
        std::fprintf(stderr, "error: desktop frontend failed (%d)\n", (int)status);
        return 1;
    }
    std::printf("desktop frontend: %llu frames, effect '%s', backend %s\n",
        (unsigned long long)frames, effect, resolved_backend);
    return 0;
}
