/* Mixer widgets share the sequence's audio settings, history and output path. */
void finish_audio(jfx_desktop_frontend_t *f) {
    if (!f->audio_edit) return;
    f->audio_edit=false;
    editor_result(f,jfx_editor_commit_edit(f->editor));
}
void audio_value(jfx_desktop_frontend_t *f,const char *op,uint32_t track,uint32_t clip,double value) {
    if (!f->audio_edit) {
        finish_grading(f);
        auto r=jfx_editor_begin_edit(f->editor,JFX_PROJECT_KIND_SEQUENCE);
        if (r!=JFX_SUCCESS) { editor_result(f,r); return; }
        f->audio_edit=true;
    }
    editor_result(f,jfx_editor_command(f->editor,op,track,clip,0,value,""));
}
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
    if (f->audio_edit && !ImGui::IsAnyItemActive()) finish_audio(f);
}
