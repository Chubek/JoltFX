/* Private GameDev workspace. Ingagi is the source of truth for executable
 * Cliches/rooms/events; Core owns meshes, animation and scene history. */
void game_pause(jfx_desktop_frontend_t *f) {
    f->game_running = false;
    f->game_delta = 0;
    std::memset(f->game_input, 0, sizeof(f->game_input));
    std::memset(f->game_pressed, 0, sizeof(f->game_pressed));
    std::memset(f->game_spawn_offset, 0, sizeof(f->game_spawn_offset));
}
void game_stop(jfx_desktop_frontend_t *f) {
    game_pause(f); f->game_started = false;
}
void game_source_changed(jfx_desktop_frontend_t *f) {
    game_stop(f); f->game_source_dirty = true;
}
void game_message(jfx_desktop_frontend_t *f, jfx_result_t result) {
    if (result != JFX_SUCCESS)
        std::snprintf(f->game_error, sizeof(f->game_error), "%s", jfx_result_to_string(result));
    else f->game_error[0] = 0;
}
bool game_begin(jfx_desktop_frontend_t *f) {
    if (!game_authoring::identifier(f->game_ready) || !game_authoring::identifier(f->game_step)) {
        std::snprintf(f->game_error, sizeof(f->game_error), "Choose valid Ready and Step event identifiers in Debug.");
        return false;
    }
    if (jfx_desktop_frontend_game_load(f) != JFX_SUCCESS) return false;
    f->game_clock = 0; f->game_ticks = 0;
    if (jfx_desktop_frontend_game_call(f, f->game_ready) != JFX_SUCCESS) return false;
    f->game_started = true; return true;
}
void game_step(jfx_desktop_frontend_t *f, double delta) {
    if (!f->game_running) game_pause(f);
    f->game_delta = delta; f->game_clock += delta;
    if (jfx_desktop_frontend_game_call(f, f->game_step) != JFX_SUCCESS) game_stop(f);
    else { ++f->game_ticks; f->preview_dirty = true; }
    f->game_delta = 0;
}
void game_tick(jfx_desktop_frontend_t *f) {
    if (!f->game_running || f->workspace != JFX_DESKTOP_WORKSPACE_GAME) return;
    auto &io = ImGui::GetIO();
    // Input belongs to the room preview, never to script text or another window.
    bool input = f->game_mode == 0 && !io.WantTextInput && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
    const ImGuiKey keys[] = {ImGuiKey_A, ImGuiKey_D, ImGuiKey_W, ImGuiKey_S, ImGuiKey_Space, ImGuiKey_Enter};
    const ImGuiKey arrows[] = {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow};
    for (unsigned i = 0; i < 6; ++i) {
        f->game_input[i] = input && (ImGui::IsKeyDown(keys[i]) || (i < 4 && ImGui::IsKeyDown(arrows[i])));
        f->game_pressed[i] = input && (ImGui::IsKeyPressed(keys[i], false) || (i < 4 && ImGui::IsKeyPressed(arrows[i], false)));
    }
    game_step(f, std::isfinite(io.DeltaTime) && io.DeltaTime > 0 ? std::min(double(io.DeltaTime), .1) : 1.0 / 60);
    std::memset(f->game_input, 0, sizeof(f->game_input));
    std::memset(f->game_pressed, 0, sizeof(f->game_pressed));
}
void game_picker(const char *label, char (&selected)[64], const game_authoring::Entries &entries, const char *prefix) {
    bool found = false;
    for (unsigned i = 0; i < entries.count; ++i)
        if (!std::strcmp(selected, entries.names[i]) && !std::strncmp(selected, prefix, std::strlen(prefix))) found = true;
    if (!found) selected[0] = 0;
    found = false;
    for (unsigned i = 0; i < entries.count; ++i)
        if (!std::strncmp(entries.names[i], prefix, std::strlen(prefix))) {
            if (!found && !selected[0]) std::snprintf(selected, sizeof(selected), "%s", entries.names[i]);
            found = true;
        }
    if (ImGui::BeginCombo(label, selected[0] ? selected : "No factory yet")) {
        for (unsigned i = 0; i < entries.count; ++i) {
            auto *name = entries.names[i];
            if (!std::strncmp(name, prefix, std::strlen(prefix)) && ImGui::Selectable(name, !std::strcmp(selected, name)))
                std::snprintf(selected, sizeof(selected), "%s", name);
        }
        ImGui::EndCombo();
    }
}
bool game_run_factory(jfx_desktop_frontend_t *f, const char *entry, bool placement) {
    // The placement offset is only visible during this synchronous invocation.
    auto result = jfx_desktop_frontend_game_load(f);
    if (result == JFX_SUCCESS) {
        if (placement) std::memcpy(f->game_spawn_offset, f->game_spawn_position, sizeof(f->game_spawn_offset));
        result = jfx_desktop_frontend_game_call(f, entry);
    }
    std::memset(f->game_spawn_offset, 0, sizeof(f->game_spawn_offset));
    if (result == JFX_SUCCESS) {
        f->scene_object = std::max(0, int(jfx_scene3d_object_count(jfx_editor_scene3d(f->editor))) - 1);
        f->preview_dirty = true;
    }
    return result == JFX_SUCCESS;
}
void game_objects(jfx_desktop_frontend_t *f, bool animation) {
    auto *scene = jfx_editor_scene3d(f->editor);
    const uint32_t count = jfx_scene3d_object_count(scene);
    ImGui::Text("Room / scene (%u objects)", count);
    if (ImGui::BeginChild("Game object tree", ImVec2(0, 130), ImGuiChildFlags_Borders)) {
        for (uint32_t i = 0; i < count; ++i) {
            jfx_object3d_info_t info{}; info.size = sizeof(info);
            if (jfx_scene3d_object_info(scene, i, &info) != JFX_SUCCESS) continue;
            ImGui::PushID(int(i));
            if (ImGui::Selectable(info.name, f->scene_object == int(i))) f->scene_object = int(i);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (f->scene_object < 0 || uint32_t(f->scene_object) >= count) f->scene_object = 0;
    jfx_object3d_info_t info{}; info.size = sizeof(info);
    uint32_t object = uint32_t(f->scene_object);
    if (jfx_scene3d_object_info(scene, object, &info) != JFX_SUCCESS) {
        ImGui::TextWrapped("Instantiate a Cliche or build a room to begin."); return;
    }
    auto edit = [&](const char *op, uint32_t channel = 0, double value = 0, uint32_t frame = 0, const char *text = "") {
        game_message(f, jfx_desktop_frontend_edit(f, op, object, channel, frame, value, text));
    };
    if (!animation) {
        if (ImGui::InputText("Name", info.name, sizeof(info.name), ImGuiInputTextFlags_EnterReturnsTrue)) edit("3d.name", 0, 0, 0, info.name);
        if (ImGui::Checkbox("Visible", &info.visible)) edit("3d.visible", 0, info.visible ? 1 : 0);
        if (ImGui::Button("Duplicate object")) {
            edit("3d.duplicate"); f->scene_object = int(jfx_scene3d_object_count(scene)) - 1; return;
        }
        ImGui::SameLine(); if (ImGui::Button("Delete object")) { edit("3d.remove"); return; }
    }
    uint32_t frame = uint32_t(std::min(double(jfx_scene3d_frames(scene) - 1), std::floor(f->time_seconds * jfx_scene3d_fps(scene))));
    const char *channels[] = {"Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y", "Rotation Z", "Scale X", "Scale Y", "Scale Z"};
    for (uint32_t i = 0; i < 9; ++i) {
        ImGui::PushID(int(i)); ImGui::SetNextItemWidth(115);
        if (ImGui::InputFloat(channels[i], &info.transform[i], 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) edit("3d.transform", i, double(info.transform[i]));
        if (animation) { ImGui::SameLine(); if (ImGui::SmallButton("Key")) edit("3d.key", i, double(info.transform[i]), frame); }
        ImGui::PopID();
    }
    if (!animation) {
        for (uint32_t i = 0; i < 3; ++i) {
            const char *label[] = {"Tint R", "Tint G", "Tint B"};
            if (ImGui::InputFloat(label[i], &info.color[i], 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) edit("3d.color", i, double(info.color[i]));
        }
        ImGui::TextWrapped("Enter commits. Instances have independent transforms/materials. Object indices shift after deletion; use Ingagi ECS handles for gameplay identity.");
    } else {
        ImGui::Combo("Key channel", &f->scene_channel, "Position X\0Position Y\0Position Z\0Rotation X\0Rotation Y\0Rotation Z\0Scale X\0Scale Y\0Scale Z\0");
        ImGui::Combo("Key interpolation", &f->scene_interpolation, "Hold\0Linear\0Smooth\0");
        if (ImGui::Button("Set interpolation")) edit("3d.interpolation", uint32_t(f->scene_channel), f->scene_interpolation, frame);
        if (ImGui::Button("Remove current key")) edit("3d.key_remove", uint32_t(f->scene_channel), 0, frame);
        ImGui::TextWrapped("Ingagi can author keys with engine_scene_key(object, channel, frame, value) and sample them with engine_scene_sample.");
    }
}
void game_rooms(jfx_desktop_frontend_t *f, const game_authoring::Entries &entries) {
    ImGui::TextUnformatted("Rooms / reusable scene builders");
    ImGui::TextWrapped("GameMaker-style rooms are Ingagi room_* factories. Build appends to the shared scene; it does not clear existing work.");
    game_picker("Room factory", f->game_room, entries, "room_");
    ImGui::BeginDisabled(!jfx_desktop_frontend_game_available() || !f->game_room[0]);
    if (ImGui::Button("Build room")) game_run_factory(f, f->game_room, false);
    ImGui::EndDisabled();
    ImGui::InputText("Room name", f->game_room_name, sizeof(f->game_room_name));
    game_picker("Room Cliche", f->game_cliche, entries, "cliche_");
    if (ImGui::Button("Create room factory")) {
        char name[64]; std::snprintf(name, sizeof(name), "room_%s", f->game_room_name);
        char text[256]; std::snprintf(text, sizeof(text), "\nfn %s() { return %s(); }\n", name, f->game_cliche);
        if (!game_authoring::identifier(f->game_room_name) || !game_authoring::identifier(f->game_cliche) ||
            game_authoring::declared(f->game_source, name) || !game_authoring::append(f->game_source, sizeof(f->game_source), text))
            std::snprintf(f->game_error, sizeof(f->game_error), "Choose a Cliche, a unique identifier, and leave space in the 16 KiB source.");
        else { game_source_changed(f); std::snprintf(f->game_room, sizeof(f->game_room), "%s", name); f->game_error[0] = 0; }
    }
    ImGui::Separator();
    if (ImGui::Button("Clear room...")) ImGui::OpenPopup("Clear room?");
    if (ImGui::BeginPopupModal("Clear room?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Clear the shared 3D scene? Ingagi source is kept.");
        if (ImGui::Button("Clear scene")) {
            game_message(f, jfx_desktop_frontend_edit(f, "3d.new", 0, 0, 0, 0, ""));
            game_stop(f); f->scene_object = 0; jfx_desktop_frontend_seek(f, 0); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel clear")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::TextWrapped("Use Objects for the room tree and inspector. Use the 3D workspace for mesh authoring. This preview is currently 3D; a tilemap/2D room renderer is not implemented.");
}
void game_cliches(jfx_desktop_frontend_t *f, const game_authoring::Entries &entries) {
    ImGui::TextUnformatted("Cliches / reusable Ingagi prefabs");
    game_picker("Cliche factory", f->game_cliche, entries, "cliche_");
    ImGui::InputFloat3("Instance offset XYZ", f->game_spawn_position);
    bool finite = true; for (float v : f->game_spawn_position) finite = finite && std::isfinite(v) && std::fabs(v) <= 10000;
    ImGui::BeginDisabled(!finite || !jfx_desktop_frontend_game_available() || !f->game_cliche[0]);
    if (ImGui::Button("Instantiate Cliche")) game_run_factory(f, f->game_cliche, true);
    ImGui::EndDisabled();
    ImGui::Separator();
    auto &draft = f->game_draft;
    ImGui::InputText("Cliche name", draft.name, sizeof(draft.name));
    ImGui::Combo("Primitive", &draft.primitive, game_authoring::primitives, 17);
    ImGui::InputFloat3("Base position", draft.transform);
    ImGui::InputFloat3("Base rotation", draft.transform + 3);
    ImGui::InputFloat3("Base scale", draft.transform + 6);
    ImGui::ColorEdit3("Base tint", draft.color);
    if (ImGui::Button("Use selected pose / material")) {
        jfx_object3d_info_t info{}; info.size = sizeof(info);
        auto result = jfx_scene3d_object_info(jfx_editor_scene3d(f->editor), uint32_t(f->scene_object), &info);
        game_message(f, result);
        if (result == JFX_SUCCESS) {
            std::memcpy(draft.transform, info.transform, sizeof(draft.transform));
            std::memcpy(draft.color, info.color, sizeof(draft.color));
        }
    }
    if (ImGui::Button("Create Cliche")) {
        if (!game_authoring::append_cliche(f->game_source, sizeof(f->game_source), draft))
            std::snprintf(f->game_error, sizeof(f->game_error), "Use a unique identifier, finite TRS (nonzero scale), and leave space in the 16 KiB source.");
        else {
            game_source_changed(f); f->game_error[0] = 0;
            std::snprintf(f->game_cliche, sizeof(f->game_cliche), "cliche_%s", draft.name);
        }
    }
    ImGui::BeginDisabled(!f->game_cliche[0]);
    if (ImGui::Button("Create variant from Cliche")) {
        if (!game_authoring::append_cliche(f->game_source, sizeof(f->game_source), draft, f->game_cliche))
            std::snprintf(f->game_error, sizeof(f->game_error), "Give the variant a unique valid name and valid TRS/tint; the source must fit in 16 KiB.");
        else {
            game_source_changed(f); f->game_error[0] = 0;
            std::snprintf(f->game_cliche, sizeof(f->game_cliche), "cliche_%s", draft.name);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("A Cliche is a zero-argument cliche_* function in your .ing source. Edit it in Ingagi to compose multiple objects, other Cliches, ECS components or behavior. The builder captures pose/material only, not custom mesh geometry or keys.");
    ImGui::TextWrapped("Variants call the base factory and override the returned root object's TRS/tint; base logic still runs. Each invocation creates independent instances. Source edits affect future instances; automatic linked-instance updates and Unity-style Apply/Revert are not yet implemented.");
}
void game_scripts(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("Ingagi / game logic, ECS, rooms and Cliches");
    if (ImGui::Button("Install starter...")) ImGui::OpenPopup("Replace game source?");
    if (ImGui::BeginPopupModal("Replace game source?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Replace source with a room, Hero Cliche, ECS Actor and input-driven step event?");
        if (ImGui::Button("Replace source")) {
            jfx_desktop_frontend_game_set_source(f, game_authoring::starter);
            f->game_cliche[0] = f->game_room[0] = 0; ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Keep source")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::InputTextMultiline("##Ingagi source", f->game_source, sizeof(f->game_source), ImVec2(-1, 300), ImGuiInputTextFlags_AllowTabInput))
        game_source_changed(f);
    ImGui::Text("%zu / %zu source bytes | %s", std::strlen(f->game_source), sizeof(f->game_source) - 1,
        f->game_source_dirty ? "unloaded changes" : "loaded");
    ImGui::InputText("Entry", f->game_entry, sizeof(f->game_entry));
    ImGui::BeginDisabled(!jfx_desktop_frontend_game_available());
    if (ImGui::Button("Load / Reload")) jfx_desktop_frontend_game_load(f);
    ImGui::SameLine(); if (ImGui::Button("Call entry")) jfx_desktop_frontend_game_call(f, f->game_entry);
    ImGui::SameLine(); if (ImGui::Button("Run source") && jfx_desktop_frontend_game_load(f) == JFX_SUCCESS)
        jfx_desktop_frontend_game_call(f, f->game_entry);
    ImGui::EndDisabled();
    ImGui::TextWrapped("Calls use the Ingagi VM: functions, closures, collections and ECS are available. Source-only embedding is import-free. Shader compilation and native QBE/WASM exports remain separate CLI workflows.");
}
jfx_result_t game_save_room(jfx_desktop_frontend_t *f) {
    const char *path = f->game_scene_path;
    const char *ext = std::strrchr(path, '.');
    if (!ext || std::strcmp(ext, ".jfx")) return JFX_ERROR_INVALID_ARGUMENT;
    auto *text = static_cast<char *>(imgui_alloc(JFX_PROJECT_MAX_BYTES, nullptr));
    if (!text) return JFX_ERROR_OUT_OF_MEMORY;
    size_t n = 0;
    auto result = jfx_editor_save(f->editor, text, JFX_PROJECT_MAX_BYTES, &n);
    if (result == JFX_SUCCESS) {
        char temporary[sizeof(f->game_scene_path) + 32];
        std::snprintf(temporary, sizeof(temporary), "%s.gamedev-tmp", path);
        FILE *file = std::fopen(temporary, "wbx");
        if (!file) result = JFX_ERROR_NOT_FOUND;
        else {
            bool ok = std::fwrite(text, 1, n, file) == n;
            if (std::fclose(file)) ok = false;
            if (ok) ok = std::rename(temporary, path) == 0;
            if (!ok) { std::remove(temporary); result = JFX_ERROR_INVALID_ARGUMENT; }
        }
    }
    imgui_free(text, nullptr); return result;
}
void game_resources(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("Resources / source, scenes, meshes and animation packs");
    ImGui::TextWrapped("Save both resources: .ing contains Cliches/rooms/events; .jfx contains the shared scene geometry, keys and camera. Loading source does not execute it.");
    ImGui::InputText("Game source (.ing)", f->game_source_path, sizeof(f->game_source_path));
    if (ImGui::Button("Open source")) game_message(f, jfx_desktop_frontend_open_project(f, f->game_source_path));
    ImGui::SameLine(); if (ImGui::Button("Save source")) game_message(f, jfx_desktop_frontend_save_project(f, f->game_source_path));
    ImGui::InputText("Room scene (.jfx)", f->game_scene_path, sizeof(f->game_scene_path));
    if (ImGui::Button("Open room scene")) {
        auto result = jfx_desktop_frontend_open_project(f, f->game_scene_path);
        if (result == JFX_SUCCESS) jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_GAME);
        game_message(f, result);
    }
    ImGui::SameLine(); if (ImGui::Button("Save room scene")) {
        game_message(f, game_save_room(f));
    }
    ImGui::Separator(); ImGui::InputText("Mesh (.ply)", f->scene_mesh_path, sizeof(f->scene_mesh_path));
    if (ImGui::Button("Import mesh")) {
        auto result = jfx_desktop_frontend_edit(f, "3d.import_ply", 0, 0, 0, 0, f->scene_mesh_path);
        game_message(f, result);
        if (result == JFX_SUCCESS) f->scene_object = int(jfx_scene3d_object_count(jfx_editor_scene3d(f->editor))) - 1;
    }
    ImGui::SameLine(); if (ImGui::Button("Export mesh")) game_message(f, jfx_desktop_frontend_edit(f,
        "3d.export_ply", uint32_t(f->scene_object), 0, 0, 0, f->scene_mesh_path));
    ImGui::InputText("Animation pack (.iap)", f->iap_path, sizeof(f->iap_path));
    if (ImGui::Button("Export animation pack")) game_message(f, jfx_desktop_frontend_export_iap(f, f->iap_path, nullptr, 0));
    ImGui::SameLine(); if (ImGui::Button("Import animation pack")) game_message(f,
        jfx_desktop_frontend_edit(f, "3d.iap_import", 0, 0, 0, 0, f->iap_path));
    ImGui::TextWrapped("This is an editor preview, not a standalone game exporter. Audio authoring lives in DAW; runtime audio, collision callbacks and packaged-game builds are not yet connected.");
}
void game_viewport(jfx_desktop_frontend_t *f) {
    ImGui::BeginDisabled(f->game_running);
    if (ImGui::Button("Undo game edit")) game_message(f, jfx_desktop_frontend_edit(f, "undo", 0, 0, 0, 0, ""));
    ImGui::SameLine(); if (ImGui::Button("Redo game edit")) game_message(f, jfx_desktop_frontend_edit(f, "redo", 0, 0, 0, 0, ""));
    int mode = f->scene_mode; bool playing = f->playing;
    f->scene_mode = 0;
    f->playing = f->playing || f->game_running; // interactive raster, no authoring handles
    modeling3d_viewport(f, jfx_editor_scene3d(f->editor));
    f->scene_mode = mode; f->playing = playing;
    ImGui::EndDisabled();
}
void ingagi_panel(jfx_desktop_frontend_t *f) {
    // Finish viewport gestures even if a different sub-tab is now selected.
    auto &io = ImGui::GetIO(); bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (f->scene_transforming && (escape || !io.MouseDown[0])) {
        game_message(f, jfx_desktop_frontend_edit(f, escape ? "3d.transform_cancel" : "3d.transform_end", 0, 0, 0, 0, ""));
        f->scene_transforming = false;
    }
    if (f->scene_navigation && (escape || modeling3d_navigation_released(f))) {
        game_message(f, jfx_desktop_frontend_edit(f, escape ? "3d.navigation_cancel" : "3d.navigation_end", 0, 0, 0, 0, ""));
        f->scene_navigation = false;
    }
    ImGui::TextUnformatted("GameDev | Ingagi");
    if (!jfx_desktop_frontend_game_available()) ImGui::TextDisabled("Enable JFX_INGAGI=ON to execute game logic. Source and scene editing remain available.");
    ImGui::BeginDisabled(!jfx_desktop_frontend_game_available() || f->scene_navigation || f->scene_transforming);
    if (ImGui::Button(f->game_running ? "Pause game" : "Play game")) {
        if (f->game_running) game_pause(f);
        else if (f->game_started || game_begin(f)) { jfx_desktop_frontend_pause(f); f->game_running = true; }
    }
    ImGui::SameLine(); if (ImGui::Button("Stop game")) game_stop(f);
    ImGui::SameLine(); ImGui::BeginDisabled(f->game_running);
    if (ImGui::Button("Step game") && (f->game_started || game_begin(f))) game_step(f, 1.0 / 60);
    ImGui::EndDisabled(); ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Text("%s | %.3f s | %llu ticks", f->game_running ? "Running" : f->game_started ? "Paused" : "Stopped", f->game_clock, (unsigned long long)f->game_ticks);
    ImGui::TextWrapped("Play calls ready(), then step() each tick. Stop keeps scene edits; Undo restores individual commands. Save a room snapshot before destructive experiments.");
    if (ImGui::BeginTabBar("GameDev modes", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const char *tabs[] = {"Rooms", "Objects", "Cliches", "Ingagi", "Animation", "Resources", "Debug"};
        int previous = f->game_mode;
        for (int i = 0; i < 7; ++i) if (ImGui::BeginTabItem(tabs[i])) { f->game_mode = i; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        if (previous != f->game_mode && f->scene_transforming) {
            game_message(f, jfx_desktop_frontend_edit(f, "3d.transform_end", 0, 0, 0, 0, "")); f->scene_transforming = false;
        }
        if (previous != f->game_mode && f->scene_navigation) {
            game_message(f, jfx_desktop_frontend_edit(f, "3d.navigation_end", 0, 0, 0, 0, "")); f->scene_navigation = false;
        }
    }
    // Keep error/result above scrolling editors so failures are always visible.
    ImGui::Text("Result: %s", f->game_output[0] ? f->game_output : "(not called)");
    if (f->game_error[0]) ImGui::TextWrapped("%s", f->game_error);
    auto *scene = jfx_editor_scene3d(f->editor);
    f->duration_seconds = double(jfx_scene3d_frames(scene)) / jfx_scene3d_fps(scene);
    game_authoring::Entries entries{}; game_authoring::scan(f->game_source, entries);
    bool preview = f->game_mode == 0 || f->game_mode == 1 || f->game_mode == 2 || f->game_mode == 4;
    bool split = preview && ImGui::GetContentRegionAvail().x >= 760 &&
        ImGui::BeginTable("GameDev layout", 2, ImGuiTableFlags_Resizable);
    if (split) {
        ImGui::TableSetupColumn("Authoring", ImGuiTableColumnFlags_WidthFixed, 360);
        ImGui::TableSetupColumn("Game preview"); ImGui::TableNextColumn();
    }
    if (!split && preview && ImGui::CollapsingHeader("Game preview")) {
        if (ImGui::BeginChild("Compact game preview", ImVec2(0, 250))) game_viewport(f);
        ImGui::EndChild();
    }
    if (ImGui::BeginChild("Game authoring", ImVec2(0, 0))) {
        ImGui::BeginDisabled(f->game_running || f->scene_transforming || f->scene_navigation);
        switch (f->game_mode) {
        case 0: game_rooms(f, entries); break;
        case 1: game_objects(f, false); break;
        case 2: game_cliches(f, entries); break;
        case 3: game_scripts(f); break;
        case 4: {
            int frame = int(std::min(double(jfx_scene3d_frames(scene) - 1), std::floor(f->time_seconds * jfx_scene3d_fps(scene))));
            if (ImGui::SliderInt("Scene frame", &frame, 0, int(jfx_scene3d_frames(scene)) - 1))
                jfx_desktop_frontend_seek(f, double(frame) / jfx_scene3d_fps(scene));
            game_objects(f, true); break;
        }
        case 5: game_resources(f); break;
        default:
            ImGui::InputText("Ready event", f->game_ready, sizeof(f->game_ready));
            ImGui::InputText("Step event", f->game_step, sizeof(f->game_step));
            ImGui::Text("VM: %s | source %s", f->game_loaded ? "loaded" : "not loaded", f->game_source_dirty ? "modified" : "current");
            ImGui::Text("Scene objects: %u / %u", jfx_scene3d_object_count(scene), JFX_3D_MAX_OBJECTS);
            ImGui::TextUnformatted("Limits: 16 KiB source, 100,000 instructions/call, 256 call frames.");
            ImGui::Separator();
            ImGui::TextWrapped("engine_delta(): tick seconds (clamped to 0.1); engine_game_time(): preview elapsed seconds; engine_time(): scene transport clock.");
            ImGui::TextWrapped("engine_input(action) / engine_pressed(action): 0 left (A), 1 right (D), 2 up (W), 3 down (S), 4 jump (Space), 5 accept (Enter). Arrow keys also work. Input is enabled only in Rooms, not while typing.");
            ImGui::TextWrapped("engine_scene_add/remove/count/get/set/key/sample/cloner/cloner_real/procedural/color share Core scene/history. Channels 0..8 are XYZ position, XYZ Euler rotation, XYZ scale. engine_spawn_offset(0..2) supplies Cliche placement.");
            ImGui::TextWrapped("Runtime errors pause execution. Reload failure preserves the previous VM; host scene edits are not rolled back. Script print output is not captured by the scalar embedding ABI.");
            break;
        }
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    if (split) {
        ImGui::TableNextColumn();
        game_viewport(f);
        ImGui::EndTable();
    }
}
