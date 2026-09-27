#include "jfx/desktop_frontend.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "imgui.h"

extern "C" {
#include "tilly/allocator.h"
}

struct jfx_desktop_frontend {
    jfx_engine_t *engine;
    ImGuiContext *imgui;
    uint32_t width;
    uint32_t height;
    double time_seconds;
    bool playing;
    char project_path[260];
};

static void *imgui_alloc(size_t bytes, void *) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        alignof(max_align_t));
}

static void imgui_free(void *pointer, void *) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), pointer);
}

extern "C" jfx_result_t jfx_desktop_frontend_create(
    const jfx_desktop_frontend_config_t *config, jfx_desktop_frontend_t **out_frontend) {
    if (!out_frontend) return JFX_ERROR_INVALID_ARGUMENT;
    *out_frontend = nullptr;
    jfx_desktop_frontend_t *frontend = static_cast<jfx_desktop_frontend_t *>(tilly_alloc(
        (tilly_allocator_t *)tilly_default_allocator(), sizeof(*frontend), alignof(jfx_desktop_frontend_t)));
    if (!frontend) return JFX_ERROR_OUT_OF_MEMORY;
    std::memset(frontend, 0, sizeof(*frontend));
    frontend->width = config && config->width ? config->width : 1280u;
    frontend->height = config && config->height ? config->height : 720u;
    jfx_engine_config_t engine_config{};
    engine_config.max_buffers = 4;
    engine_config.max_textures = 64;
    engine_config.max_kernels = 64;
    engine_config.backend_name = config ? config->backend_name : nullptr;
    jfx_result_t result = jfx_engine_init(&engine_config, &frontend->engine);
    if (result != JFX_SUCCESS) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return result;
    }
    ImGui::SetAllocatorFunctions(imgui_alloc, imgui_free, nullptr);
    frontend->imgui = ImGui::CreateContext();
    if (!frontend->imgui) {
        jfx_engine_shutdown(frontend->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    ImGui::SetCurrentContext(frontend->imgui);
    ImGui::StyleColorsDark();
    unsigned char *font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    *out_frontend = frontend;
    return JFX_SUCCESS;
}

extern "C" void jfx_desktop_frontend_destroy(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return;
    ImGui::SetCurrentContext(frontend->imgui);
    ImGui::DestroyContext(frontend->imgui);
    jfx_engine_shutdown(frontend->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
}

extern "C" jfx_result_t jfx_desktop_frontend_open_project(jfx_desktop_frontend_t *frontend,
    const char *path) {
    if (!frontend || !path || !path[0] || std::strlen(path) >= sizeof(frontend->project_path)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    std::strcpy(frontend->project_path, path);
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_resize(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height) {
    if (!frontend || !width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    frontend->width = width;
    frontend->height = height;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_play(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    frontend->playing = true;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_pause(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    frontend->playing = false;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds) {
    if (!frontend || !std::isfinite(time_seconds) || time_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    frontend->time_seconds = time_seconds;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_draw(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    ImGui::SetCurrentContext(frontend->imgui);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(frontend->width), static_cast<float>(frontend->height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("Open Project", "Ctrl+O", false, false);
            ImGui::MenuItem("Export", "Ctrl+E", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            ImGui::MenuItem("Play", "Space", &frontend->playing);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::Begin("Viewport");
    ImGui::Text("Backend: %s", jfx_engine_backend_name(frontend->engine));
    ImGui::Text("Project: %s", frontend->project_path[0] ? frontend->project_path : "Untitled");
    ImGui::Text("Preview surface: %ux%u", frontend->width, frontend->height);
    ImGui::Button("Render current frame");
    ImGui::End();
    ImGui::Begin("Timeline");
    float time = static_cast<float>(frontend->time_seconds);
    if (ImGui::SliderFloat("Time", &time, 0.0f, 60.0f, "%.3f s")) frontend->time_seconds = time;
    ImGui::Checkbox("Playing", &frontend->playing);
    ImGui::End();
    ImGui::Begin("Properties");
    ImGui::TextUnformatted("Select a node to edit effect parameters.");
    ImGui::End();
    ImGui::Begin("Console");
    ImGui::TextUnformatted("Desktop UI frame composed successfully.");
    ImGui::End();
    ImGui::Render();
    return jfx_engine_tick(frontend->engine);
}

extern "C" const char *jfx_desktop_frontend_project_path(const jfx_desktop_frontend_t *frontend) {
    return frontend && frontend->project_path[0] ? frontend->project_path : nullptr;
}
