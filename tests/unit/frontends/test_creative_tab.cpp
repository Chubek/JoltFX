/* Creative-programming tab conformance (headless).
 *
 * Drives the public `jfx_desktop_frontend_creative_*` API with no host
 * window: valid sketches render varying pixels through the Glue VM, invalid
 * sources are rejected with a diagnostic and leave output untouched, bounds
 * are enforced, and all three exporters write magic-prefixed files. */

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"

static const char kSketch[] =
    ";; @kernel sketch_demo\n;; @category generative\n"
    ";; @description Test sketch.\n;; @complexity Low\n"
    ";; @gpu Yes\n;; @since 0.3.0\n"
    "(defkernel sketch_demo [x y t]\n"
    "  (rgba x y (* 0.5 (+ 0.5 (* 0.5 t))) 1.0))";

static void check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "creative tab assertion failed: %s\n", what);
        assert(condition);
    }
}

int main() {
    jfx_desktop_frontend_t *f = nullptr;
    jfx_desktop_frontend_config_t config{};
    config.size = sizeof(config);
    config.backend_name = jfx_test_backend();
    check(jfx_desktop_frontend_create(&config, &f) == JFX_SUCCESS, "create");
    check(f != nullptr, "frontend");

    /* The appended tab/panel identifiers resolve and are visible by default. */
    check(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_CREATIVE) ==
              JFX_SUCCESS,
        "select creative workspace");
    check(jfx_desktop_frontend_workspace(f) == JFX_DESKTOP_WORKSPACE_CREATIVE,
        "creative workspace");
    check(jfx_desktop_frontend_panel_visible(f, JFX_DESKTOP_PANEL_CREATIVE),
        "creative panel visible");
    check(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_COUNT) ==
              JFX_ERROR_INVALID_ARGUMENT,
        "workspace count still rejected");

    /* Argument validation happens before any work. */
    check(jfx_desktop_frontend_creative_set_source(nullptr, kSketch) ==
              JFX_ERROR_INVALID_ARGUMENT,
        "null frontend rejected");
    check(jfx_desktop_frontend_creative_set_source(f, nullptr) ==
              JFX_ERROR_INVALID_ARGUMENT,
        "null source rejected");
    check(jfx_desktop_frontend_creative_set_source(f, "") ==
              JFX_ERROR_INVALID_ARGUMENT,
        "empty source rejected");
    check(jfx_desktop_frontend_creative_render(nullptr, 8, 4, nullptr, 0) ==
              JFX_ERROR_INVALID_ARGUMENT,
        "null render rejected");
    check(jfx_desktop_frontend_creative_export(f, "bogus", "/tmp/jfx-creative-bogus") ==
              JFX_ERROR_INVALID_ARGUMENT,
        "unknown export kind rejected");
    check(jfx_desktop_frontend_creative_export(f, "obj", "") ==
              JFX_ERROR_INVALID_ARGUMENT,
        "empty export path rejected");

    /* A valid sketch round-trips through the source buffer. */
    check(jfx_desktop_frontend_creative_set_source(f, kSketch) == JFX_SUCCESS,
        "set source");
    char back[8192];
    check(jfx_desktop_frontend_creative_source(f, back, sizeof(back)) == JFX_SUCCESS,
        "get source");
    check(std::strcmp(back, kSketch) == 0, "source round trip");
    check(jfx_desktop_frontend_creative_source(f, back, 8) ==
              JFX_ERROR_INVALID_ARGUMENT,
        "small source buffer rejected");

    /* Bounds come before compilation: oversize rasters never compile. */
    std::vector<uint8_t> pixels(16 * 8 * 4u, 0xAB);
    check(jfx_desktop_frontend_creative_render(f, 321, 8, pixels.data(),
              pixels.size()) == JFX_ERROR_INVALID_ARGUMENT,
        "oversize width rejected");
    check(jfx_desktop_frontend_creative_render(f, 16, 181, pixels.data(),
              pixels.size()) == JFX_ERROR_INVALID_ARGUMENT,
        "oversize height rejected");
    check(jfx_desktop_frontend_creative_render(f, 0, 8, pixels.data(),
              pixels.size()) == JFX_ERROR_INVALID_ARGUMENT,
        "zero width rejected");

    /* Render 16x8: x varies along the row, so red must vary; the gradient
     * sketch is never flat. */
    check(jfx_desktop_frontend_creative_render(f, 16, 8, pixels.data(),
              pixels.size()) == JFX_SUCCESS,
        "render");
    check(jfx_desktop_frontend_creative_error(f)[0] == 0, "no error text");
    bool varies = false;
    for (size_t i = 1; i < 16 * 8; ++i) {
        if (pixels[i * 4] != pixels[0]) {
            varies = true;
            break;
        }
    }
    check(varies, "sketch output varies across x");
    /* Alpha is opaque for the 4-output rgba form. */
    for (size_t i = 0; i < 16 * 8; ++i) {
        check(pixels[i * 4 + 3] == 255, "alpha opaque");
    }

    /* An undersized destination is rejected and left untouched. */
    std::vector<uint8_t> tiny(4u, 0xAB);
    check(jfx_desktop_frontend_creative_render(f, 16, 8, tiny.data(),
              tiny.size()) == JFX_ERROR_INVALID_ARGUMENT,
        "small destination rejected");
    check(tiny[0] == 0xAB, "destination untouched");

    /* Invalid sources fail with a diagnostic and preserve prior output. */
    const char *bad = "(defkernel broken [x] (+ x missing))";
    check(jfx_desktop_frontend_creative_set_source(f, bad) == JFX_SUCCESS,
        "set bad source (stored, fails at render)");
    std::vector<uint8_t> before = pixels;
    check(jfx_desktop_frontend_creative_render(f, 16, 8, pixels.data(),
              pixels.size()) != JFX_SUCCESS,
        "bad sketch fails");
    check(jfx_desktop_frontend_creative_error(f)[0] != 0, "diagnostic present");
    check(pixels == before, "failed render preserves output");
    check(jfx_desktop_frontend_creative_set_source(f, kSketch) == JFX_SUCCESS,
        "restore source");

    /* All three exporters write magic-prefixed files. */
    check(jfx_desktop_frontend_creative_export(f, "obj", "/tmp/jfx-creative-test.o") ==
              JFX_SUCCESS,
        "export obj");
    check(jfx_desktop_frontend_creative_export(f, "html",
              "/tmp/jfx-creative-test.html") == JFX_SUCCESS,
        "export html");
    check(jfx_desktop_frontend_creative_export(f, "wasm",
              "/tmp/jfx-creative-test.wat") == JFX_SUCCESS,
        "export wasm");
    {
        FILE *elf = std::fopen("/tmp/jfx-creative-test.o", "rb");
        check(elf != nullptr, "object readable");
        uint8_t magic[4] = {0};
        check(std::fread(magic, 1, 4, elf) == 4, "object has header");
        std::fclose(elf);
        check(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' &&
                  magic[3] == 'F',
            "object is ELF");
    }
    {
        FILE *html = std::fopen("/tmp/jfx-creative-test.html", "rb");
        check(html != nullptr, "html readable");
        char head[16] = {0};
        check(std::fread(head, 1, 15, html) == 15, "html has body");
        std::fclose(html);
        check(!std::strncmp(head, "<!DOCTYPE html>", 14), "html doctype");
    }
    {
        FILE *wat = std::fopen("/tmp/jfx-creative-test.wat", "rb");
        check(wat != nullptr, "wat readable");
        char head[8] = {0};
        check(std::fread(head, 1, 7, wat) == 7, "wat has body");
        std::fclose(wat);
        check(!std::strncmp(head, "(module", 7), "wat module");
    }

    /* The full UI frame composes with the new tab selected (headless). */
    check(jfx_desktop_frontend_draw(f) == JFX_SUCCESS, "draw with creative tab");

    jfx_desktop_frontend_destroy(f);
    return 0;
}
