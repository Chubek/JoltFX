/* Private Ingagi workspace. The native ABI owns script state and ECS storage;
 * callbacks borrow frontend state synchronously and end before destruction. */
void ingagi_panel(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("Ingagi / Game Engine");
    ImGui::TextWrapped("Edit game logic, load it into an owned VM, then call an entry. "
        "engine_time() exposes the shared transport clock; engine_scene_* callbacks edit the shared 3D scene. Calls are instruction-budgeted.");
    ImGui::InputTextMultiline("##Ingagi source", f->game_source, sizeof(f->game_source), ImVec2(-1, 260));
    ImGui::InputText("Entry", f->game_entry, sizeof(f->game_entry));
    ImGui::InputText("Source file (.ing)", f->project_input, sizeof(f->project_input));
    if (ImGui::Button("Open source")) {
        const auto result = jfx_desktop_frontend_open_project(f, f->project_input);
        if (result != JFX_SUCCESS) std::snprintf(f->game_error, sizeof(f->game_error), "Open source: %s", jfx_result_to_string(result));
    }
    ImGui::SameLine();
    if (ImGui::Button("Save source")) {
        const auto result = jfx_desktop_frontend_save_project(f, f->project_input);
        if (result != JFX_SUCCESS) std::snprintf(f->game_error, sizeof(f->game_error), "Save source: %s", jfx_result_to_string(result));
    }
    if (!jfx_desktop_frontend_game_available()) {
        ImGui::TextDisabled("Enable JFX_INGAGI in the CMake build to run scripts.");
        return;
    }
    if (ImGui::Button("Load / Reload")) jfx_desktop_frontend_game_load(f);
    ImGui::SameLine();
    if (ImGui::Button("Call entry")) jfx_desktop_frontend_game_call(f, f->game_entry);
    ImGui::SameLine();
    if (ImGui::Button("Run source") && jfx_desktop_frontend_game_load(f) == JFX_SUCCESS)
        jfx_desktop_frontend_game_call(f, f->game_entry);
    ImGui::Separator();
    ImGui::Text("Result: %s", f->game_output[0] ? f->game_output : "(not called)");
    if (f->game_error[0]) ImGui::TextWrapped("%s", f->game_error);
    if (jfx_editor_kind(f->editor) == JFX_PROJECT_KIND_SCENE3D) {
        auto *scene = jfx_editor_scene3d(f->editor);
        f->duration_seconds = double(jfx_scene3d_frames(scene)) / jfx_scene3d_fps(scene);
        ImGui::Separator();
        ImGui::Text("Shared 3D scene: %u objects", jfx_scene3d_object_count(scene));
        int frame = int(std::min(double(jfx_scene3d_frames(scene) - 1), std::floor(f->time_seconds * jfx_scene3d_fps(scene))));
        if (ImGui::SliderInt("Scene frame", &frame, 0, int(jfx_scene3d_frames(scene) - 1)))
            jfx_desktop_frontend_seek(f, double(frame) / jfx_scene3d_fps(scene));
        if (f->scene_navigation && (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (!ImGui::GetIO().MouseDown[0] && !ImGui::GetIO().MouseDown[1] && !ImGui::GetIO().MouseDown[2]))) {
            auto status = jfx_desktop_frontend_edit(f, ImGui::IsKeyPressed(ImGuiKey_Escape) ? "3d.navigation_cancel" : "3d.navigation_end", 0, 0, 0, 0, "");
            if (status == JFX_SUCCESS) f->scene_navigation = false;
        }
        modeling3d_viewport(f, scene);
    }
}
