/* SDL2 + OpenGL host window for the desktop frontend.
 *
 * Compiled only when SDL2 is found. Provides a real OS window, a real OpenGL
 * 3.3 core context, a real input queue and a real present, and drives the
 * vendored Dear ImGui SDL2 and OpenGL3 backends so the composed draw data is
 * actually rasterized instead of discarded. The preview texture lives here too,
 * which keeps every GL and SDL symbol out of the frontend. */

#include "host_window.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(JFX_DESKTOP_HAVE_SDL2)

#include <SDL.h>
#include <SDL_opengl.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "tilly/allocator.h"

namespace {

void copy_error(char *out_error, size_t out_error_size, const char *reason) {
    if (out_error && out_error_size) {
        std::snprintf(out_error, out_error_size, "%s", reason);
    }
}

} // namespace

struct jfx_desktop_window {
    SDL_Window *sdl_window;
    SDL_GLContext gl_context;
    uint32_t width;
    uint32_t height;
    double last_frame_seconds;
    bool sdl_initialized;
    bool imgui_platform_ready;
    bool imgui_renderer_ready;
    GLuint preview_texture;
    uint32_t preview_width;
    uint32_t preview_height;
};

bool jfx_desktop_window_available(void) { return true; }

const char *jfx_desktop_window_backend_name(void) { return "sdl2"; }

jfx_desktop_window_t *jfx_desktop_window_create(const jfx_desktop_window_config_t *config,
    char *out_error, size_t out_error_size) {
    jfx_desktop_window_t *window = static_cast<jfx_desktop_window_t *>(        tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), sizeof(jfx_desktop_window_t),
            alignof(jfx_desktop_window_t)));
    if (!window) {
        copy_error(out_error, out_error_size, "out of memory");
        return nullptr;
    }
    std::memset(window, 0, sizeof(*window));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        char reason[256];
        std::snprintf(reason, sizeof(reason), "SDL_Init failed: %s", SDL_GetError());
        std::fprintf(stderr, "%s\n", reason);
        jfx_desktop_window_destroy(window);
        copy_error(out_error, out_error_size, reason);
        return nullptr;
    }
    window->sdl_initialized = true;

    /* Request 3.3 core, the floor for the ImGui OpenGL3 backend's GLSL 150
     * shaders, then step down through 3.2/3.0 compatibility for drivers that
     * refuse a core profile. */
    static const struct { int major; int minor; int profile; } kGlRequests[] = {
        { 3, 3, SDL_GL_CONTEXT_PROFILE_CORE },
        { 3, 2, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY },
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY },
    };
    for (const auto &request : kGlRequests) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, request.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, request.minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, request.profile);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        window->sdl_window = SDL_CreateWindow(config->title ? config->title : "JoltFX",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)config->width,
            (int)config->height,
            SDL_WINDOW_OPENGL | (config->resizable ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_SHOWN) |
                SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window->sdl_window) {
            continue;
        }
        window->gl_context = SDL_GL_CreateContext(window->sdl_window);
        if (window->gl_context) {
            break;
        }
        SDL_DestroyWindow(window->sdl_window);
        window->sdl_window = nullptr;
    }
    if (!window->sdl_window || !window->gl_context) {
        char reason[256];
        std::snprintf(reason, sizeof(reason), "no usable OpenGL context: %s", SDL_GetError());
        std::fprintf(stderr, "%s\n", reason);
        jfx_desktop_window_destroy(window);
        copy_error(out_error, out_error_size, reason);
        return nullptr;
    }

    SDL_GL_MakeCurrent(window->sdl_window, window->gl_context);
    SDL_GL_SetSwapInterval(1);
    window->last_frame_seconds = SDL_GetTicks() / 1000.0;

    if (!ImGui_ImplSDL2_InitForOpenGL(window->sdl_window, window->gl_context)) {
        jfx_desktop_window_destroy(window);
        copy_error(out_error, out_error_size, "ImGui_ImplSDL2_InitForOpenGL failed");
        return nullptr;
    }
    window->imgui_platform_ready = true;
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
        jfx_desktop_window_destroy(window);
        copy_error(out_error, out_error_size, "ImGui_ImplOpenGL3_Init failed");
        return nullptr;
    }
    window->imgui_renderer_ready = true;

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window->sdl_window, &drawable_width, &drawable_height);
    window->width = drawable_width > 0 ? (uint32_t)drawable_width : config->width;
    window->height = drawable_height > 0 ? (uint32_t)drawable_height : config->height;
    return window;
}

void jfx_desktop_window_destroy(jfx_desktop_window_t *window) {
    if (!window) {
        return;
    }
    /* Delete the preview texture while the GL context is still current, then
     * shut the backends down exactly once each. */
    if (window->preview_texture) {
        glDeleteTextures(1, &window->preview_texture);
        window->preview_texture = 0;
    }
    if (window->imgui_renderer_ready) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (window->imgui_platform_ready) {
        ImGui_ImplSDL2_Shutdown();
    }
    if (window->gl_context) {
        SDL_GL_DeleteContext(window->gl_context);
    }
    if (window->sdl_window) {
        SDL_DestroyWindow(window->sdl_window);
    }
    if (window->sdl_initialized) {
        SDL_Quit();
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), window);
}

bool jfx_desktop_window_begin_frame(jfx_desktop_window_t *window) {
    if (!window) {
        return false;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            int drawable_width = 0;
            int drawable_height = 0;
            SDL_GL_GetDrawableSize(window->sdl_window, &drawable_width, &drawable_height);
            if (drawable_width > 0) {
                window->width = (uint32_t)drawable_width;
            }
            if (drawable_height > 0) {
                window->height = (uint32_t)drawable_height;
            }
        }
    }
    const double now = SDL_GetTicks() / 1000.0;
    double delta = now - window->last_frame_seconds;
    // A stalled frame (debugger break, compositor stall) must not be reported to
    // the UI as a multi-second frame.
    if (!(delta > 0.0)) {
        delta = 1.0 / 60.0;
    } else if (delta > 0.25) {
        delta = 0.25;
    }
    window->last_frame_seconds = now;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)window->width, (float)window->height);
    io.DeltaTime = (float)delta;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    return true;
}

void jfx_desktop_window_end_frame(jfx_desktop_window_t *window) {
    if (!window) {
        return;
    }
    ImGui::Render();
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window->sdl_window, &drawable_width, &drawable_height);
    glViewport(0, 0, drawable_width, drawable_height);
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window->sdl_window);
}

uint32_t jfx_desktop_window_drawable_width(const jfx_desktop_window_t *window) {
    return window ? window->width : 0u;
}

uint32_t jfx_desktop_window_drawable_height(const jfx_desktop_window_t *window) {
    return window ? window->height : 0u;
}

void *jfx_desktop_window_upload_rgba8(jfx_desktop_window_t *window, uint32_t width,
    uint32_t height, const uint8_t *rgba) {
    if (!window || !width || !height || !rgba) {
        return nullptr;
    }
    if (!window->preview_texture) {
        glGenTextures(1, &window->preview_texture);
    }
    if (!window->preview_texture) {
        return nullptr;
    }
    glBindTexture(GL_TEXTURE_2D, window->preview_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (window->preview_width != width || window->preview_height != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, nullptr);
        window->preview_width = width;
        window->preview_height = height;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA,
        GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(window->preview_texture));
}

#else /* !JFX_DESKTOP_HAVE_SDL2 */

namespace {
void copy_error(char *out_error, size_t out_error_size, const char *reason) {
    if (out_error && out_error_size) {
        std::snprintf(out_error, out_error_size, "%s", reason);
    }
}
} // namespace

bool jfx_desktop_window_available(void) { return false; }

const char *jfx_desktop_window_backend_name(void) { return "none"; }

jfx_desktop_window_t *jfx_desktop_window_create(const jfx_desktop_window_config_t *,
    char *out_error, size_t out_error_size) {
    copy_error(out_error, out_error_size,
        "this build has no windowing backend; reconfigure with JFX_DESKTOP_WINDOW=ON and SDL2 "
        "available, or use --headless-smoke");
    return nullptr;
}

void jfx_desktop_window_destroy(jfx_desktop_window_t *) {}

bool jfx_desktop_window_begin_frame(jfx_desktop_window_t *) { return false; }

void jfx_desktop_window_end_frame(jfx_desktop_window_t *) {}

uint32_t jfx_desktop_window_drawable_width(const jfx_desktop_window_t *) { return 0u; }

uint32_t jfx_desktop_window_drawable_height(const jfx_desktop_window_t *) { return 0u; }

void *jfx_desktop_window_upload_rgba8(jfx_desktop_window_t *, uint32_t, uint32_t,
    const uint8_t *) {
    return nullptr;
}

#endif /* JFX_DESKTOP_HAVE_SDL2 */
