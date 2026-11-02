#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f) == JFX_SUCCESS); }
static ImGuiWindow *editor() {
    for (auto *w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && std::strstr(w->Name, "/Editor interface_")) return w;
    assert(false); return nullptr;
}
static void click(jfx_desktop_frontend_t *f, const char *label, bool tab = false) {
    auto &io = ImGui::GetIO(); io.AddMousePosEvent(0, 0); draw(f);
    auto *w = editor();
    if (!tab && std::strcmp(label, "Play game") && std::strcmp(label, "Pause game") && std::strcmp(label, "Step game"))
        for (auto *child : ImGui::GetCurrentContext()->Windows)
            if (child->Active && std::strstr(child->Name, "/Game authoring_")) { w = child; break; }
    ImGui::DebugLocateItem(tab ? ImHashStr(label, 0, w->GetID("GameDev modes")) : w->GetID(label)); draw(f);
    ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
    ImRect bounds = w->InnerRect; bounds.Expand(4);
    for (const auto &v : ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x = std::fmin(lo.x, v.pos.x); lo.y = std::fmin(lo.y, v.pos.y);
        hi.x = std::fmax(hi.x, v.pos.x); hi.y = std::fmax(hi.y, v.pos.y);
    }
    if (lo.x > hi.x) std::fprintf(stderr, "Missing game widget: %s\n", label);
    assert(lo.x <= hi.x);
    io.AddMousePosEvent((lo.x + hi.x) / 2, (lo.y + hi.y) / 2); draw(f);
    io.AddMouseButtonEvent(0, true); draw(f); io.AddMouseButtonEvent(0, false); draw(f); draw(f);
}
static void text(jfx_desktop_frontend_t *f, const char *label, const char *value) {
    click(f, label);
    auto &io = ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); draw(f);
    io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); draw(f);
    io.AddInputCharactersUTF8(value); draw(f);
    io.AddKeyEvent(ImGuiKey_Enter, true); draw(f); io.AddKeyEvent(ImGuiKey_Enter, false); draw(f);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    jfx_desktop_frontend_config_t config{}; config.size = sizeof(config);
    config.width = 1440; config.height = 1000; config.backend_name = jfx_test_backend();
    jfx_desktop_frontend_t *f = nullptr;
    assert(jfx_desktop_frontend_create(&config, &f) == JFX_SUCCESS);
    ImGui::GetIO().IniFilename = nullptr;
    assert(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_GAME) == JFX_SUCCESS);
    draw(f); draw(f); draw(f);
    char state[16384];
    assert(jfx_desktop_frontend_scene3d_state(f, state, sizeof(state)) == JFX_SUCCESS);
    click(f, "Cliches", true);
    // The default draft can be added once and persists as normal Ingagi source.
    click(f, "Create Cliche");
    assert(jfx_desktop_frontend_save_project(f, argv[1]) == JFX_SUCCESS);
    FILE *file = std::fopen(argv[1], "rb"); assert(file);
    char source[16384]{}; assert(std::fread(source, 1, sizeof(source) - 1, file) > 0); std::fclose(file);
    if (!std::strstr(source, "fn cliche_Player()")) std::fprintf(stderr, "Cliche error: %s\nSource: %s\n", jfx_desktop_frontend_game_error(f), source);
    assert(std::strstr(source, "fn cliche_Player()"));
    if (jfx_desktop_frontend_game_available()) {
        click(f, "Instantiate Cliche");
        assert(!jfx_desktop_frontend_game_error(f)[0]);
        assert(jfx_desktop_frontend_scene3d_state(f, state, sizeof(state)) == JFX_SUCCESS);
        assert(std::strstr(state, "\"name\":\"cube\""));
        click(f, "Instantiate Cliche");
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "1"));
    }
    click(f, "Rooms", true);
    click(f, "Create room factory");
    assert(!jfx_desktop_frontend_game_error(f)[0]);
    assert(jfx_desktop_frontend_save_project(f, argv[1]) == JFX_SUCCESS);
    if (jfx_desktop_frontend_game_available()) {
        click(f, "Build room");
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "2"));
        assert(jfx_desktop_frontend_game_set_source(f,
            "fn ready() { return 0; } fn step() { return engine_delta(); }") == JFX_SUCCESS);
        click(f, "Play game");
        assert(std::atof(jfx_desktop_frontend_game_output(f)) > 0);
        click(f, "Pause game");
        click(f, "Step game");
        assert(std::atof(jfx_desktop_frontend_game_output(f)) > 0);
        click(f, "Rooms", true);
        assert(jfx_desktop_frontend_game_set_source(f,
            "fn ready() { return 0; } fn step() { "
            "if engine_pressed(0) { return 2; } "
            "if engine_input(0) { return 1; } return 0; }") == JFX_SUCCESS);
        click(f, "Play game");
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_A, true); draw(f);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "2"));
        draw(f); assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "1"));
        io.AddKeyEvent(ImGuiKey_A, false); draw(f);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "0"));
        click(f, "Ingagi", true);
        io.AddKeyEvent(ImGuiKey_A, true); draw(f);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "0")); // no game input in code editor
        io.AddKeyEvent(ImGuiKey_A, false); draw(f);
        click(f, "Pause game");
        // Runtime failure pauses automatically and remains recoverable.
        assert(jfx_desktop_frontend_game_set_source(f,
            "fn ready() { return 0; } fn step() { loop {} }") == JFX_SUCCESS);
        click(f, "Play game");
        assert(std::strstr(jfx_desktop_frontend_game_error(f), "budget"));
    }
    for (const char *tab : {"Rooms", "Objects", "Ingagi", "Animation", "Resources", "Debug"}) click(f, tab, true);
    click(f, "Resources", true);
    char scene_path[1024]; std::snprintf(scene_path, sizeof(scene_path), "%s.jfx", argv[1]);
    text(f, "Room scene (.jfx)", scene_path);
    click(f, "Save room scene");
    assert(!jfx_desktop_frontend_game_error(f)[0]);
    file = std::fopen(scene_path, "rb"); assert(file);
    char scene_header[64]{}; assert(std::fread(scene_header, 1, sizeof(scene_header) - 1, file) > 0); std::fclose(file);
    assert(std::strstr(scene_header, "scene3d 1"));
    click(f, "Open room scene");
    assert(jfx_desktop_frontend_workspace(f) == JFX_DESKTOP_WORKSPACE_GAME);
    assert(!jfx_desktop_frontend_game_error(f)[0]);
    assert(!std::remove(scene_path));
    assert(jfx_desktop_frontend_resize(f, 640, 800) == JFX_SUCCESS);
    draw(f); draw(f);
    assert(jfx_desktop_frontend_resize(f, 1440, 1000) == JFX_SUCCESS);
    draw(f); draw(f);
    assert(jfx_desktop_frontend_open_project(f, argv[1]) == JFX_SUCCESS);
    if (jfx_desktop_frontend_game_available()) {
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "cliche_Player") == JFX_SUCCESS);
    }
    assert(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_NLE) == JFX_SUCCESS); draw(f);
    jfx_desktop_frontend_destroy(f);
    assert(!std::remove(argv[1]));
}
