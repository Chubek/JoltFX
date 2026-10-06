/* Mixer widgets share the sequence's audio settings, history and output path. */
void finish_audio(jfx_desktop_frontend_t *f) {
    if (!f->audio_edit) return;
    f->audio_edit=false;
    editor_result(f,jfx_editor_commit_edit(f->editor));
}
void audio_value(jfx_desktop_frontend_t *f,const char *op,uint32_t track,uint32_t clip,double value,uint32_t index=0) {
    if (f->recording) return;
    if (f->native_session) { auto r=native_sync(f,true); if (r!=JFX_SUCCESS) { editor_result(f,r); return; } close_inspector(f); }
    if (!f->audio_edit) {
        finish_grading(f);
        auto r=jfx_editor_begin_edit(f->editor,JFX_PROJECT_KIND_SEQUENCE);
        if (r!=JFX_SUCCESS) { editor_result(f,r); return; }
        f->audio_edit=true;
    }
    editor_result(f,jfx_editor_command(f->editor,op,track,clip,index,value,""));
}
void vst3_panel(jfx_desktop_frontend_t *f);
void audio_panel(jfx_desktop_frontend_t *f) {
    auto *t=jfx_editor_timeline(f->editor);
    auto command=[&](const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double value=0,const char *text="") {
        auto r=jfx_desktop_frontend_edit(f,op,a,b,c,value,text);
        editor_result(f,r); t=jfx_editor_timeline(f->editor); return r;
    };
    if (f->selected_track>=jfx_timeline_track_count(t)) f->selected_track=0;
    if (f->selected_clip>=jfx_timeline_clip_count(t,f->selected_track)) f->selected_clip=0;
    ImGui::TextUnformatted("Audio Mixing");
    ImGui::TextWrapped("Mix sequence tracks, then refine the selected clip. Mute and solo affect both audio and video.");
    if (ImGui::Button("Add audio track") && command("track.add",0,0,0,0,"Audio")==JFX_SUCCESS) {
        f->selected_track=(uint32_t)jfx_timeline_track_count(t)-1; f->selected_clip=0;
    }
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Audio file",f->media_path,sizeof(f->media_path));
    ImGui::SetNextItemWidth(120); ImGui::InputInt("Duration (frames)",&f->audio_import_frames);
    uint64_t frame=jfx_desktop_frontend_timeline_current_frame(f);
    ImGui::BeginDisabled(!*f->media_path || f->audio_import_frames<=0 || !jfx_timeline_track_count(t) || frame>UINT32_MAX);
    if (ImGui::Button("Add audio at playhead") &&
        command("clip.add",f->selected_track,JFX_CLIP_AUDIO,(uint32_t)frame,f->audio_import_frames,f->media_path)==JFX_SUCCESS)
        f->selected_clip=(uint32_t)jfx_timeline_clip_count(t,f->selected_track)-1;
    ImGui::EndDisabled();
    ImGui::SeparatorText("Track mixer");
    if (!jfx_timeline_track_count(t)) ImGui::TextDisabled("Add an audio track to start mixing.");
    const float unit=ImGui::GetFontSize()/13.0f;
    ImGui::BeginChild("Audio channels",ImVec2(0,300*unit),true,ImGuiWindowFlags_HorizontalScrollbar);
    for (uint32_t track=0;track<jfx_timeline_track_count(t);++track) {
        if (track) ImGui::SameLine();
        ImGui::PushID((int)track);
        ImGui::BeginChild("Audio channel",ImVec2(170*unit,275*unit),true);
        if (ImGui::Selectable(jfx_timeline_track_name(t,track),track==f->selected_track)) {
            finish_audio(f); f->selected_track=track; f->selected_clip=0;
        }
        uint32_t clips=0;
        for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
            auto source=jfx_timeline_clip_source(t,track,clip);
            if (source==JFX_CLIP_AUDIO || source==JFX_CLIP_VIDEO) ++clips;
        }
        ImGui::TextDisabled("%u audio/video clips",clips);
        bool muted=jfx_timeline_track_muted(t,track),solo=jfx_timeline_track_solo(t,track);
        if (ImGui::Checkbox("Mute",&muted)) command("track.mute",track,0,0,muted);
        ImGui::SameLine();
        if (ImGui::Checkbox("Solo",&solo)) command("track.solo",track,0,0,solo);
        float gain=jfx_timeline_track_audio_gain(t,track);
        if (ImGui::VSliderFloat("##Track gain",ImVec2(60*unit,130*unit),&gain,0,16,"%.2fx",ImGuiSliderFlags_Logarithmic))
            audio_value(f,"track.audio.gain",track,0,gain);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Track gain: 1.0 is unity; 0 is silence. Ctrl-click to enter a value.");
        if (gain>0) ImGui::Text("%+.1f dB",20.0*std::log10((double)gain));
        else ImGui::TextUnformatted("Silence");
        if (ImGui::SmallButton("Unity")) command("track.audio.gain",track,0,0,1);
        ImGui::EndChild(); ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SeparatorText("Stereo output");
    for (size_t channel=0;channel<2;++channel) {
        float peak=f->audio_peak[channel];
        char label[64];
        if (peak>0) std::snprintf(label,sizeof(label),"%s  %+.1f dBFS%s",channel?"R":"L",20.0*std::log10((double)peak),peak>1?"  OVER":"");
        else std::snprintf(label,sizeof(label),"%s  silence",channel?"R":"L");
        float level=peak>0 ? clampf((20.0f*std::log10(peak)+60)/60,0,1) : 0;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,peak>1?ImVec4(.95f,.25f,.2f,1):ImVec4(.25f,.75f,.45f,1));
        ImGui::ProgressBar(level,ImVec2(-1,0),label); ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("Playback block peaks, before output clipping. Lower gain if OVER appears.");
    ImGui::SeparatorText("Selected clip");
    auto source=jfx_timeline_clip_source(t,f->selected_track,f->selected_clip);
    jfx_clip_audio_t audio{}; audio.size=sizeof(audio);
    if ((source!=JFX_CLIP_AUDIO && source!=JFX_CLIP_VIDEO) ||
        jfx_timeline_get_clip_audio(t,f->selected_track,f->selected_clip,&audio)!=JFX_SUCCESS) {
        ImGui::TextWrapped("Select an audio or video clip using the shared Selected clip menu.");
    } else {
        ImGui::TextUnformatted(jfx_timeline_clip_name(t,f->selected_track,f->selected_clip));
        ImGui::TextWrapped("%s",jfx_timeline_clip_path(t,f->selected_track,f->selected_clip));
        if (ImGui::Checkbox("Enable clip audio",&audio.enabled)) command("clip.audio.enabled",f->selected_track,f->selected_clip,0,audio.enabled);
        if (ImGui::SliderFloat("Clip gain",&audio.gain,0,16,"%.2fx",ImGuiSliderFlags_Logarithmic))
            audio_value(f,"clip.audio.gain",f->selected_track,f->selected_clip,audio.gain);
        if (ImGui::SliderFloat("Stereo balance",&audio.pan,-1,1,"%.2f"))
            audio_value(f,"clip.audio.pan",f->selected_track,f->selected_clip,audio.pan);
        ImGui::TextDisabled("-1 left / 0 center / +1 right");
        if (ImGui::InputScalar("Fade in (frames)",ImGuiDataType_U64,&audio.fade_in_frames))
            audio_value(f,"clip.audio.fade_in",f->selected_track,f->selected_clip,(double)audio.fade_in_frames);
        if (ImGui::InputScalar("Fade out (frames)",ImGuiDataType_U64,&audio.fade_out_frames))
            audio_value(f,"clip.audio.fade_out",f->selected_track,f->selected_clip,(double)audio.fade_out_frames);
    }
    float master=jfx_timeline_master_audio_gain(t);
    if (ImGui::SliderFloat("Master gain",&master,0,4,"%.2fx",ImGuiSliderFlags_Logarithmic)) audio_value(f,"audio.master.gain",0,0,master);
    if (f->audio_edit && !ImGui::IsAnyItemActive()) finish_audio(f);
    if (ImGui::CollapsingHeader("VST3 track inserts")) vst3_panel(f);
}

void vst3_catalog_path(jfx_desktop_frontend_t *f,const std::filesystem::path &path) {
    auto text=path.u8string();
    if (text.size()>=JFX_NODE_PATH_MAX || f->vst3_count>=128) return;
    jfx_vst3_class_t classes[128]; size_t count=0;
    auto r=jfx_vst3_classes(text.c_str(),classes,128,&count);
    if (r!=JFX_SUCCESS) return;
    for (size_t i=0;i<count && f->vst3_count<128;++i) {
        auto &entry=f->vst3_catalog[f->vst3_count++];
        std::snprintf(entry.path,sizeof(entry.path),"%s",text.c_str()); entry.info=classes[i];
    }
}
void vst3_scan(jfx_desktop_frontend_t *f,const std::filesystem::path &root) {
    std::error_code ec;
    if (root.extension()==".vst3" || std::filesystem::is_regular_file(root,ec)) { vst3_catalog_path(f,root); return; }
    size_t visited=0;
    for (std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;
        !ec && it!=end && f->vst3_count<128 && visited<10000;it.increment(ec),++visited) {
        if (it.depth()>8) { it.disable_recursion_pending(); continue; }
        if (it->path().extension()==".vst3") { vst3_catalog_path(f,it->path()); it.disable_recursion_pending(); }
    }
}
void vst3_panel(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("VST3 effects and instruments / selected track insert rack");
    if (!jfx_vst3_available()) { ImGui::TextWrapped("VST3 hosting is disabled. Build with JFX_AUDIO_VST3=ON on a desktop platform."); return; }
    auto *t=jfx_editor_timeline(f->editor);
    auto command=[&](const char *op,uint32_t b,uint32_t c=0,double value=0,const char *text="") {
        auto r=jfx_desktop_frontend_edit(f,op,f->selected_track,b,c,value,text);
        editor_result(f,r); t=jfx_editor_timeline(f->editor); return r;
    };
    if (!jfx_timeline_track_count(t)) { ImGui::TextDisabled("Add an audio track first."); return; }
    if (f->selected_track>=jfx_timeline_track_count(t)) f->selected_track=0;
    if (ImGui::BeginCombo("Insert track",jfx_timeline_track_name(t,f->selected_track))) {
        for (uint32_t track=0;track<jfx_timeline_track_count(t);++track)
            if (ImGui::Selectable(jfx_timeline_track_name(t,track),track==f->selected_track)) { finish_audio(f); f->selected_track=track; f->vst3_insert=0; }
        ImGui::EndCombo();
    }
    ImGui::InputText("VST3 plugin or folder",f->vst3_path,sizeof(f->vst3_path));
    bool scan_path=ImGui::Button("Scan path"); ImGui::SameLine();
    bool scan_installed=ImGui::Button("Scan installed VST3");
    if (scan_path || scan_installed) {
        f->vst3_count=0; f->vst3_selected=0;
        try {
            if (scan_path && *f->vst3_path) vst3_scan(f,std::filesystem::u8path(f->vst3_path));
            else {
#if defined(_WIN32)
                if (const char *common=std::getenv("COMMONPROGRAMFILES")) vst3_scan(f,std::filesystem::u8path(common)/"VST3");
                if (const char *local=std::getenv("LOCALAPPDATA")) vst3_scan(f,std::filesystem::u8path(local)/"Programs/Common/VST3");
#elif defined(__APPLE__)
                vst3_scan(f,"/Library/Audio/Plug-Ins/VST3");
                if (const char *home=std::getenv("HOME")) vst3_scan(f,std::filesystem::u8path(home)/"Library/Audio/Plug-Ins/VST3");
#else
                vst3_scan(f,"/usr/lib/vst3"); vst3_scan(f,"/usr/local/lib/vst3");
                if (const char *home=std::getenv("HOME")) vst3_scan(f,std::filesystem::u8path(home)/".vst3");
#endif
            }
            std::snprintf(f->status,sizeof(f->status),"Found %u VST3 classes%s.",f->vst3_count,f->vst3_count==128?" (catalog limit)":"");
        } catch (...) { std::snprintf(f->status,sizeof(f->status),"Unable to scan VST3 path."); }
    }
    if (f->vst3_count) {
        if (ImGui::BeginCombo("Available plugin",f->vst3_catalog[f->vst3_selected].info.name)) {
            for (uint32_t i=0;i<f->vst3_count;++i) {
                ImGui::PushID((int)i);
                if (ImGui::Selectable(f->vst3_catalog[i].info.name,i==f->vst3_selected)) f->vst3_selected=i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",f->vst3_catalog[i].path);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(jfx_timeline_audio_insert_count(t,f->selected_track)>=JFX_AUDIO_MAX_INSERTS);
        if (ImGui::Button("Add VST3 insert")) {
            auto &entry=f->vst3_catalog[f->vst3_selected];
            jfx_audio_insert_t in{}; in.size=sizeof(in); in.enabled=true;
            std::snprintf(in.cid,sizeof(in.cid),"%s",entry.info.cid); std::snprintf(in.path,sizeof(in.path),"%s",entry.path);
            jfx_vst3_instance_t *probe=nullptr;
            auto r=jfx_vst3_create(&in,48000,4096,&probe); jfx_vst3_destroy(probe);
            if (r==JFX_SUCCESS) {
                char text[JFX_NODE_PATH_MAX+34]; std::snprintf(text,sizeof(text),"%s %s",in.cid,in.path);
                if (command("audio.insert.add",0,0,0,text)==JFX_SUCCESS) f->vst3_insert=(uint32_t)jfx_timeline_audio_insert_count(t,f->selected_track)-1;
            } else std::snprintf(f->status,sizeof(f->status),"Cannot host this VST3: %s. Requires stereo float32 output and stereo/no input.",jfx_result_to_string(r));
        }
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Clip gain/balance/fades -> ordered inserts -> track fader -> master. Up to 8 inserts; 64 parameter overrides per insert.");
    size_t count=jfx_timeline_audio_insert_count(t,f->selected_track);
    if (f->vst3_insert>=count) f->vst3_insert=0;
    for (uint32_t i=0;i<count;++i) {
        jfx_audio_insert_t in{}; in.size=sizeof(in); jfx_timeline_get_audio_insert(t,f->selected_track,i,&in);
        ImGui::PushID((int)i);
        if (ImGui::Checkbox("Enabled",&in.enabled)) command("audio.insert.enabled",i,0,in.enabled);
        ImGui::SameLine();
        char label[256]; const char *name=in.path;
        for (const char *p=in.path;*p;++p) if (*p=='/' || *p=='\\') name=p+1;
        std::snprintf(label,sizeof(label),"%u: %.220s",i+1,name);
        if (ImGui::Selectable(label,i==f->vst3_insert,0,ImVec2(220,0))) { finish_audio(f); f->vst3_insert=i; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i) { command("audio.insert.move",i,i-1); f->vst3_insert=i-1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && i+1<count) { command("audio.insert.move",i,i+1); f->vst3_insert=i+1; }
        ImGui::SameLine();
        bool removed=ImGui::SmallButton("Remove");
        if (removed) command("audio.insert.remove",i);
        ImGui::PopID(); if (removed) break;
    }
    jfx_audio_insert_t in{}; in.size=sizeof(in);
    if (jfx_timeline_get_audio_insert(t,f->selected_track,f->vst3_insert,&in)!=JFX_SUCCESS) return;
    ImGui::TextWrapped("%s",in.path);
    if (f->vst3_inspector && (f->vst3_inspector_track!=f->selected_track || f->vst3_inspector_insert!=f->vst3_insert ||
        std::strcmp(f->vst3_inspector_path,in.path) || std::strcmp(f->vst3_inspector_cid,in.cid))) {
        editor_result(f,native_sync(f,true)); close_inspector(f);
    }
    if (ImGui::Button("Edit VST3 parameters")) {
        editor_result(f,open_inspector(f,in));
    }
    ImGui::SameLine();
    if (ImGui::Button("Open native VST3 editor")) {
        auto r=open_inspector(f,in);
        if (r==JFX_SUCCESS) r=jfx_desktop_window_plugin_open(f->window,f->vst3_inspector,"JoltFX / VST3 editor");
        if (r==JFX_SUCCESS) { f->native_session=true; jfx_vst3_set_edit_callback(f->vst3_inspector,native_callback,f); }
        editor_result(f,r);
    }
    if (!f->vst3_inspector) return;
    if (f->native_session) {
        ImGui::TextWrapped("Native edits save opaque component/controller state and share sequence undo. Close the editor to return to generic controls.");
        if (ImGui::Button("Close native editor")) { editor_result(f,native_sync(f,true)); close_inspector(f); }
        if (!f->vst3_inspector) return;
    } else if (ImGui::Button("Capture plugin state")) {
        f->native_session=true; f->native_dirty=true;
        auto r=native_sync(f,true); editor_result(f,r);
        if (r!=JFX_SUCCESS && f->native_edit) { jfx_editor_cancel_edit(f->editor); f->native_edit=false; }
        f->native_session=f->native_dirty=false;
    }
    ImGui::Text("%s",jfx_vst3_is_instrument(f->vst3_inspector)?"MIDI instrument (place first in rack)":"Audio effect");
    ImGui::Text("Plugin latency: %u samples at 48 kHz (automatically compensated)",jfx_vst3_latency(f->vst3_inspector));
    ImGui::BeginChild("VST3 parameters",ImVec2(0,220),true);
    size_t parameters=std::min(jfx_vst3_parameter_count(f->vst3_inspector),(size_t)4096);
    for (uint32_t p=0;p<parameters;++p) {
        jfx_vst3_parameter_t param{}; param.size=sizeof(param);
        if (jfx_vst3_parameter(f->vst3_inspector,p,&param)!=JFX_SUCCESS) continue;
        double value=param.value;
        ImGui::TextDisabled("Parameter %u",param.id);
        for (uint32_t v=0;v<in.parameter_count;++v) if (in.parameters[v].id==param.id) value=in.parameters[v].value;
        float normalized=(float)value;
        ImGui::PushID((int)p); ImGui::BeginDisabled(param.read_only || f->native_session);
        if (ImGui::SliderFloat(param.name,&normalized,0,1,"%.4f")) {
            if (param.steps>0) normalized=std::round(normalized*(float)param.steps)/(float)param.steps;
            audio_value(f,"audio.insert.param",f->selected_track,f->vst3_insert,normalized,param.id);
            editor_result(f,jfx_vst3_set_parameter(f->vst3_inspector,param.id,normalized));
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
    ImGui::EndChild();
    if (f->audio_edit && !ImGui::IsAnyItemActive()) finish_audio(f);
}

void daw_panel(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("DAW / audio arrangement");
    auto *timeline=jfx_editor_timeline(f->editor);
    float bpm=(float)jfx_timeline_audio_tempo(timeline);
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Tempo (BPM)",&bpm,20,400,"%.1f")) audio_value(f,"audio.tempo",0,0,bpm);
    double beats=f->time_seconds*(double)bpm/60;
    ImGui::SameLine(); ImGui::Text("4/4 | Bar %.0f  Beat %.0f",std::floor(beats/4)+1,std::floor(std::fmod(beats,4))+1);
    if (f->audio_edit && !ImGui::IsAnyItemActive()) finish_audio(f);
    if (ImGui::BeginTabBar("DAW views")) {
        if (ImGui::BeginTabItem("Arrangement")) {
            ImGui::TextWrapped("Arrange audio clips in sequence frames. Drag clips to move; drag edges to trim. Snap, split, duplicate and slip share NLE history.");
            timeline_tracks(f,true); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mixer")) { audio_panel(f); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("MIDI / Instruments")) { midi_panel(f); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Recording")) { recording_panel(f); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Automation")) { automation_panel(f); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("VST3 Inserts")) { vst3_panel(f); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Mixdown")) {
            ImGui::InputText("Stereo WAV output",f->audio_export_path,sizeof(f->audio_export_path));
            ImGui::TextWrapped("48 kHz, stereo 32-bit float WAV. Includes track inserts, latency compensation and master gain; retains unclipped headroom.");
            bool running=f->export_job && jfx_export_state(f->export_job)==JFX_EXPORT_RUNNING;
            ImGui::BeginDisabled(running || !*f->audio_export_path);
            if (ImGui::Button("Bounce stereo WAV")) {
                finish_audio(f); editor_result(f,native_sync(f,true)); jfx_export_destroy(f->export_job); f->export_job=nullptr;
                jfx_export_options_t options{}; options.size=sizeof(options); options.path=f->audio_export_path; options.container="wav"; options.audio=true;
                editor_result(f,jfx_export_begin(f->editor,&options,&f->export_job));
            }
            ImGui::EndDisabled();
            if (running) {
                ImGui::ProgressBar((float)((double)jfx_export_completed_frames(f->export_job)/(double)jfx_export_total_frames(f->export_job)));
                if (ImGui::Button("Cancel mixdown")) jfx_export_cancel(f->export_job);
            }
            if (f->export_job && jfx_export_state(f->export_job)==JFX_EXPORT_COMPLETE) ImGui::TextUnformatted("Mixdown complete.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
