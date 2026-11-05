void preferences_apply(jfx_desktop_frontend_t *f) {
    ImGui::GetStyle()=ImGuiStyle();
    if (f->preferences.theme==1) ImGui::StyleColorsLight();
    else if (f->preferences.theme==2) ImGui::StyleColorsClassic();
    else ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(f->preferences.ui_scale);
    ImGui::GetStyle().FontScaleMain=f->preferences.ui_scale;
    f->looping=f->preferences.loop_playback;
    f->nle_snap=f->preferences.timeline_snap;
    f->preview_dirty=true; ++f->scene_revision;
}
void preferences_panel(jfx_desktop_frontend_t *f) {
    if (f->preferences_save_job && f->preferences_save_job->done.load(std::memory_order_acquire)) {
        auto *job=f->preferences_save_job;
        std::snprintf(f->preferences_message,sizeof(f->preferences_message),"%s",
            job->success?"Preferences saved to Config.json.":job->message);
        tilly::destroy(job); f->preferences_save_job=nullptr;
    }
    if (!f->show_preferences) return;
    ImGui::SetNextWindowSize(ImVec2(560,670),ImGuiCond_FirstUseEver);
    bool changed=false;
    if (ImGui::Begin("Preferences",&f->show_preferences,ImGuiWindowFlags_NoCollapse)) {
        auto &p=f->preferences;
        ImGui::TextUnformatted("Global settings");
        ImGui::TextWrapped("Changes apply immediately to all workspaces. Save Preferences to keep them for the next launch.");
        ImGui::SeparatorText("3D viewport (Design, Animation and GameDev)");
        changed=ImGui::ColorEdit3("Viewport background",p.viewport_background)||changed;
        changed=ImGui::SliderFloat("Orbit sensitivity (degrees/pixel)",&p.orbit_speed,.05f,5.f,"%.2f",ImGuiSliderFlags_AlwaysClamp)||changed;
        changed=ImGui::SliderFloat("Pan sensitivity",&p.pan_speed,.1f,5.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp)||changed;
        changed=ImGui::SliderFloat("Zoom sensitivity",&p.zoom_speed,.05f,2.f,"%.2f",ImGuiSliderFlags_AlwaysClamp)||changed;
        changed=ImGui::Checkbox("Invert wheel zoom",&p.invert_zoom)||changed;
        changed=ImGui::Checkbox("Show navigation gimbal",&p.show_gimbal)||changed;
        changed=ImGui::SliderInt("Maximum preview dimension",&p.preview_max_dimension,320,1280,"%d px",ImGuiSliderFlags_AlwaysClamp)||changed;
        ImGui::TextWrapped("Navigation uses a reduced-resolution, single-sample raster preview. Rendering previews and exports keep their production renderer and background.");
        ImGui::SeparatorText("Appearance and editing");
        changed=ImGui::Combo("Theme",&p.theme,"Dark\0Light\0Classic\0")||changed;
        changed=ImGui::SliderFloat("UI scale",&p.ui_scale,.75f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp)||changed;
        changed=ImGui::Checkbox("Loop playback by default",&p.loop_playback)||changed;
        changed=ImGui::Checkbox("Timeline snapping by default",&p.timeline_snap)||changed;
        ImGui::Separator();
        ImGui::BeginDisabled(f->preferences_save_job || !f->preferences_paths.config[0]);
        if (ImGui::Button("Save Preferences")) {
            auto *job=tilly::create<PreferencesSaveJob>();
            if (job) {
                job->values=p;
                std::snprintf(job->path,sizeof(job->path),"%s",f->preferences_paths.config);
                if (scheduler_submit(preferences_save_task,job,0)) {
                    f->preferences_save_job=job;
                    std::snprintf(f->preferences_message,sizeof(f->preferences_message),"Saving preferences...");
                } else { tilly::destroy(job); std::snprintf(f->preferences_message,sizeof(f->preferences_message),"Preferences could not be queued. Try again."); }
            } else std::snprintf(f->preferences_message,sizeof(f->preferences_message),"Not enough memory to save preferences.");
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Restore Defaults")) { p=jfx_preferences::defaults(); changed=true; }
        ImGui::SameLine(); if (ImGui::Button("Close")) f->show_preferences=false;
        ImGui::TextWrapped("%s",f->preferences_message);
        ImGui::TextWrapped("Configuration: %s",f->preferences_paths.config[0]?f->preferences_paths.config:"No user configuration directory available");
    }
    ImGui::End();
    if (changed) preferences_apply(f);
}
