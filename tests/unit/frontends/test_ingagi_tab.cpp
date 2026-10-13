#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    jfx_desktop_frontend_t *f = nullptr;
    jfx_desktop_frontend_config_t config{};
    config.size = sizeof(config);
    config.backend_name = jfx_test_backend();
    assert(jfx_desktop_frontend_create(&config, &f) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_GAME) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_panel_visible(f, JFX_DESKTOP_PANEL_GAME));
    assert(jfx_desktop_frontend_draw(f) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_game_set_source(nullptr, "fn main() {}") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_game_set_source(f, nullptr) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_game_set_source(f, "") == JFX_ERROR_INVALID_ARGUMENT);
    const std::string oversized(16384, 'x');
    assert(jfx_desktop_frontend_game_set_source(f, oversized.c_str()) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_game_load(nullptr) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_game_call(f, nullptr) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_game_set_source(f, "fn main() { return 42; }") == JFX_SUCCESS);
    assert(jfx_desktop_frontend_save_project(f, "not-ingagi.jfx") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_save_project(f, argv[1]) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_game_set_source(f, "fn main() { return 99; }") == JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(f, argv[1]) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_workspace(f) == JFX_DESKTOP_WORKSPACE_GAME);
    const std::string temporary = std::string(argv[1]) + ".ingagi-tmp";
    FILE *blocked = std::fopen(temporary.c_str(), "wb");
    assert(blocked); std::fputs("preserve existing file", blocked); assert(!std::fclose(blocked));
    assert(jfx_desktop_frontend_save_project(f, argv[1]) != JFX_SUCCESS);
    blocked = std::fopen(temporary.c_str(), "rb"); assert(blocked);
    char content[64]{}; assert(std::fread(content, 1, sizeof(content), blocked) == 22);
    assert(!std::fclose(blocked)); assert(!std::strcmp(content, "preserve existing file"));
    assert(!std::remove(temporary.c_str()));
    FILE *large = std::fopen(argv[1], "wb"); assert(large);
    assert(std::fwrite(oversized.data(), 1, oversized.size(), large) == oversized.size());
    assert(!std::fclose(large));
    assert(jfx_desktop_frontend_open_project(f, argv[1]) == JFX_ERROR_INVALID_ARGUMENT);
    if (jfx_desktop_frontend_game_available()) {
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "main") == JFX_SUCCESS);
        assert(std::strcmp(jfx_desktop_frontend_game_output(f), "42") == 0);
        assert(jfx_desktop_frontend_game_set_source(f, "fn broken(") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_load(f) != JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_error(f)[0]);
        assert(jfx_desktop_frontend_game_call(f, "main") == JFX_SUCCESS);
        assert(std::strcmp(jfx_desktop_frontend_game_output(f), "42") == 0);
        assert(jfx_desktop_frontend_game_set_source(f, "fn main() { return engine_time(); }") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "main") == JFX_SUCCESS);
        // Host scene edits share the modeling workspace's document and history.
        assert(jfx_desktop_frontend_edit(f, "3d.new", 0, 0, 0, 0, "") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_set_source(f,
            "fn main() { let id = engine_scene_add(0); engine_scene_set(id, 0, 42); "
            "engine_scene_key(id, 1, 0, 10); engine_scene_key(id, 1, 30, 20); "
            "return engine_scene_get(id, 0); } "
            "fn count() { return engine_scene_count(); } "
            "fn sample() { return engine_scene_sample(0, 1, 0.5); } "
            "fn bad_id() { return engine_scene_get(-1, 0); } "
            "fn bad_channel() { return engine_scene_set(0, 9, 1); } "
            "fn bad_type() { return engine_scene_add(true); } "
            "fn remove() { engine_scene_remove(0); return engine_scene_count(); }") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "main") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "42"));
        assert(jfx_desktop_frontend_game_call(f, "count") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "1"));
        assert(jfx_desktop_frontend_game_call(f, "sample") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "15"));
        for (const char *entry : {"bad_id", "bad_channel", "bad_type"}) {
            assert(jfx_desktop_frontend_game_call(f, entry) != JFX_SUCCESS);
            assert(jfx_desktop_frontend_game_error(f)[0]);
        }
        assert(jfx_desktop_frontend_game_call(f, "remove") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "0"));
        assert(jfx_desktop_frontend_edit(f, "undo", 0, 0, 0, 0, "") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "count") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "1"));
        assert(jfx_desktop_frontend_draw(f) == JFX_SUCCESS);
        // 3D-tab Ingagi extensions: cloner, procedural/shader state, material.
        assert(jfx_desktop_frontend_edit(f, "3d.new", 0, 0, 0, 0, "") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_set_source(f,
            "fn drive() { let id = engine_scene_add(0); engine_scene_cloner(id, 2, 6, 3.0); "
            "engine_scene_color(id, 0, 0.5); return engine_scene_procedural(id, 1); } "
            "fn real() { return engine_scene_cloner_real(0); } "
            "fn bad_cloner() { engine_scene_cloner(0, 9, 1, 1.0); return 0; } "
            "fn bad_color() { engine_scene_color(0, 5, 0.5); return 0; }") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "drive") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "6"));
        assert(jfx_desktop_frontend_game_call(f, "real") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "6"));
        for (const char *entry : {"bad_cloner", "bad_color"}) {
            assert(jfx_desktop_frontend_game_call(f, entry) != JFX_SUCCESS);
            assert(jfx_desktop_frontend_game_error(f)[0]);
        }
        // Drive + .iap export share the scene document.
        assert(jfx_desktop_frontend_drive_3d(f, "drive") == JFX_SUCCESS);
        {
            char iap_path[256];
            std::snprintf(iap_path, sizeof(iap_path), "%s.iap", argv[1]);
            assert(jfx_desktop_frontend_export_iap(f, iap_path, nullptr, 0) == JFX_SUCCESS);
            uint32_t fps = 0, frames = 0, objects = 0, assets = 0;
            assert(jfx_desktop_frontend_iap_info(iap_path, &fps, &frames, &objects, &assets) == JFX_SUCCESS);
            assert(objects >= 1 && assets == 0 && fps >= 1);
            assert(!std::remove(iap_path));
            assert(jfx_desktop_frontend_export_iap(nullptr, iap_path, nullptr, 0) != JFX_SUCCESS);
            assert(jfx_desktop_frontend_iap_info(nullptr, nullptr, nullptr, nullptr, nullptr) != JFX_SUCCESS);
        }
        assert(jfx_desktop_frontend_game_set_source(f,
            "component SceneObject { object: i64; } "
            "fn main() { let e = spawn(); "
            "add_component(e, \"SceneObject\", SceneObject { object: engine_scene_add(0) }); "
            "let link = get_component(e, \"SceneObject\"); "
            "engine_scene_set(link.object, 0, 7); despawn(e); "
            "return engine_scene_get(link.object, 0); }") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "main") == JFX_SUCCESS);
        assert(!std::strcmp(jfx_desktop_frontend_game_output(f), "7"));
        assert(jfx_desktop_frontend_game_set_source(f, "fn main() { loop {} }") == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_load(f) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_game_call(f, "main") != JFX_SUCCESS);
        assert(std::strstr(jfx_desktop_frontend_game_error(f), "budget"));
    } else {
        assert(jfx_desktop_frontend_game_load(f) == JFX_ERROR_NOT_IMPLEMENTED);
    }
    jfx_desktop_frontend_destroy(f);
    assert(!std::remove(argv[1]));
    return 0;
}
