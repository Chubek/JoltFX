#include "preferences.h"
#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "jfx/jfx_scheduler.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace prefs = jfx_preferences;
static void env(const char *key, const char *value) {
#ifdef _WIN32
    assert(_putenv_s(key, value ? value : "") == 0);
#else
    assert((value ? setenv(key, value, 1) : unsetenv(key)) == 0);
#endif
}
static void write(const char *path, const std::string &text) {
    std::ofstream file(path, std::ios::binary); file << text; assert(bool(file));
}
static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f) == JFX_SUCCESS); }
static void click(jfx_desktop_frontend_t *f, ImGuiWindow *w, const char *label) {
    auto &io = ImGui::GetIO(); io.AddMousePosEvent(0,0); draw(f);
    ImGuiID id = !std::strcmp(w->Name, "##MainMenuBar") ? ImHashStr(label, 0, w->GetID("##MenuBar")) : w->GetID(label);
    ImGui::DebugLocateItem(id); draw(f);
    ImRect rect; rect.Min = ImVec2(1.e6f, 1.e6f); rect.Max = ImVec2(-1.e6f, -1.e6f);
    ImRect bounds = w->InnerRect; bounds.Expand(4);
    for (const auto &v : ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) rect.Add(v.pos);
    if (!(rect.GetWidth() > 0)) std::fprintf(stderr, "Missing preference widget '%s' in %s\n", label, w->Name);
    assert(rect.GetWidth() > 0);
    auto p = rect.GetCenter();
    // MainMenuBar's InnerRect excludes its menu bar, unlike ordinary windows.
    if (!std::strcmp(w->Name,"##MainMenuBar")) p.y=w->Pos.y+w->Size.y*.5f;
    io.AddMousePosEvent(p.x, p.y); draw(f);
    io.AddMouseButtonEvent(0, true); draw(f); io.AddMouseButtonEvent(0, false); draw(f); draw(f);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    bool have_home = std::getenv("HOME") != nullptr;
    std::string original_home = have_home ? std::getenv("HOME") : "";
    std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    env("JOLTFX_HOME_DIR", (root / "override").string().c_str());
    prefs::Paths paths{}; assert(prefs::resolve_paths(paths));
    assert(std::filesystem::path(paths.config) == root / "override" / "Config.json");
#if !defined(_WIN32) && !defined(__APPLE__)
    env("JOLTFX_HOME_DIR", nullptr); env("XDG_CONFIG_HOME", root.string().c_str());
    assert(prefs::resolve_paths(paths));
    assert(std::filesystem::path(paths.config) == root / "JoltFX" / "Config.json");
    env("XDG_CONFIG_HOME", "relative-config"); env("HOME", root.string().c_str());
    assert(prefs::resolve_paths(paths));
    assert(std::filesystem::path(paths.config) == root / ".config" / "JoltFX" / "Config.json");
    env("XDG_CONFIG_HOME", nullptr); assert(prefs::resolve_paths(paths));
    assert(std::filesystem::path(paths.config) == root / ".config" / "JoltFX" / "Config.json");
    env("HOME", nullptr); assert(!prefs::resolve_paths(paths));
#endif
    env("JOLTFX_HOME_DIR", (root / "preferences").string().c_str());
    env("HOME", have_home ? original_home.c_str() : nullptr);
    assert(prefs::resolve_paths(paths));
    std::filesystem::remove(paths.config);
    auto defaults = prefs::defaults(), values = defaults;
    assert(values.viewport_background[0] == 0 && values.viewport_background[1] == 0 && values.viewport_background[2] == 0);
    assert(values.orbit_speed > .4f && values.zoom_speed > .15f);
    char error[256]{};
    assert(prefs::load(paths.config, values, error, sizeof(error)) == prefs::LoadResult::Missing);
    values.viewport_background[0] = .125f; values.viewport_background[1] = .25f; values.viewport_background[2] = .5f;
    values.orbit_speed = 1.25f; values.pan_speed = 2; values.zoom_speed = .45f;
    values.invert_zoom = true; values.show_gimbal = false; values.ui_scale = 1.25f;
    values.theme = 1; values.loop_playback = false; values.timeline_snap = true;
    values.preview_max_dimension = 800;
    assert(prefs::save(paths.config, values, error, sizeof(error)));
    auto loaded = defaults;
    assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Loaded);
    assert(loaded.viewport_background[2] == .5f && loaded.orbit_speed == 1.25f && loaded.pan_speed == 2);
    assert(loaded.zoom_speed == .45f && loaded.invert_zoom && !loaded.show_gimbal);
    assert(loaded.ui_scale == 1.25f && loaded.theme == 1 && !loaded.loop_playback && loaded.timeline_snap);
    assert(loaded.preview_max_dimension == 800);
    auto invalid = values; invalid.ui_scale = NAN;
    assert(!prefs::save(paths.config, invalid, error, sizeof(error)) && error[0]);
    assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Loaded && loaded.ui_scale == 1.25f);
    auto bad_path = root / "not-a-file";
    std::filesystem::create_directories(bad_path);
    assert(!prefs::save(bad_path.string().c_str(), values, error, sizeof(error)) && error[0]);
    assert(std::filesystem::is_directory(bad_path));
    for (const auto &entry : std::filesystem::directory_iterator(root))
        assert(entry.path().filename().string().find(".jfx-part-") == std::string::npos);
    for (const char *text : {"[]", "{", "{\"schema_version\":999}",
            "{\"schema_version\":1,\"orbit_speed\":0}", "{\"zoom_speed\":\"fast\"}",
            "{\"viewport_background\":[0,1,2]}", "{\"viewport_background\":[0,1]}",
            "{\"show_gimbal\":1}", "{\"theme\":9}", "{\"ui_scale\":null}",
            "{\"preview_max_dimension\":320.5}", "{\"orbit_speed\":NaN}",
            "{\"orbit_speed\":1,\"orbit_speed\":2}"}) {
        write(paths.config, text); loaded = defaults;
        assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Invalid && error[0]);
        assert(loaded.orbit_speed == defaults.orbit_speed && loaded.ui_scale == defaults.ui_scale);
    }
    write(paths.config, std::string(70000, ' '));
    assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Invalid);
    write(paths.config, "{\"schema_version\":1,\"orbit_speed\":1.5,\"future_option\":{\"value\":true}}");
    assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Loaded);
    assert(loaded.orbit_speed == 1.5f && loaded.show_gimbal && loaded.viewport_background[0] == 0);

    // Real File menu, separate non-modal Preferences window, live global options,
    // scheduler-owned save and reload on the next frontend creation.
    assert(prefs::save(paths.config, defaults, error, sizeof(error)));
    jfx_desktop_frontend_config_t config{}; config.size = sizeof(config);
    config.width = 1440; config.height = 1000; config.backend_name = jfx_test_backend();
    jfx_desktop_frontend_t *f = nullptr; assert(jfx_desktop_frontend_create(&config, &f) == JFX_SUCCESS);
    ImGui::GetIO().IniFilename = nullptr; draw(f); draw(f);
    click(f, ImGui::FindWindowByName("##MainMenuBar"), "File");
    ImGuiWindow *menu = nullptr;
    for (auto *w : ImGui::GetCurrentContext()->Windows) if (w->Active && (w->Flags & ImGuiWindowFlags_Popup)) menu = w;
    assert(menu); click(f, menu, "Preferences...");
    auto *dialog = ImGui::FindWindowByName("Preferences");
    assert(dialog && dialog->Active && !(dialog->Flags & ImGuiWindowFlags_ChildWindow));
    click(f, dialog, "Show navigation gimbal");
    click(f, dialog, "Loop playback by default");
    click(f, dialog, "Timeline snapping by default");
    click(f, dialog, "Save Preferences");
    scheduler_wait_idle(); draw(f);
    assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Loaded);
    assert(!loaded.show_gimbal && !loaded.loop_playback && loaded.timeline_snap);
    jfx_desktop_frontend_destroy(f);
    loaded.ui_scale = 1.25f; loaded.theme = 1;
    assert(prefs::save(paths.config, loaded, error, sizeof(error)));
    assert(jfx_desktop_frontend_create(&config, &f) == JFX_SUCCESS);
    ImGui::GetIO().IniFilename = nullptr;
    assert(ImGui::GetStyle().FontScaleMain == 1.25f && ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x > .8f);
    assert(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_GAME) == JFX_SUCCESS);
    draw(f); assert(prefs::load(paths.config, loaded, error, sizeof(error)) == prefs::LoadResult::Loaded && !loaded.show_gimbal);
    for (auto *w : ImGui::GetCurrentContext()->Windows) if (w->Active)
        for (const auto &v : w->DrawList->VtxBuffer) assert(v.col != IM_COL32(240,85,75,255));
    jfx_desktop_frontend_destroy(f);
    std::filesystem::remove(paths.config);
}
