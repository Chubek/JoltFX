/* Included in the UI namespace: widgets edit the public models and surface errors. */
void editor_result(jfx_desktop_frontend_t *f, jfx_result_t r) {
    if (r == JFX_SUCCESS) { reset_audio(f); f->editing = true; f->preview_dirty = true; f->status[0]=0; }
    else std::snprintf(f->status, sizeof(f->status), "%s", jfx_result_to_string(r));
}
void encoded_export(jfx_desktop_frontend_t *f) {
    ImGui::InputText("Video output (.mp4/.mov/.mkv)",f->video_export_path,sizeof(f->video_export_path));
    ImGui::InputText("Encoder (empty: default)",f->video_codec,sizeof(f->video_codec));
    ImGui::InputScalar("Export start frame",ImGuiDataType_U64,&f->video_start);
    ImGui::InputScalar("Export frames (0: sequence)",ImGuiDataType_U64,&f->video_frames);
    ImGui::Checkbox("Include mixed audio",&f->video_audio);
    bool running=f->export_job && jfx_export_state(f->export_job)==JFX_EXPORT_RUNNING;
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Export encoded video")) {
        jfx_export_destroy(f->export_job); f->export_job=nullptr;
        jfx_export_options_t options{}; options.size=sizeof(options); options.path=f->video_export_path;
        options.video_codec=*f->video_codec?f->video_codec:nullptr; options.start_frame=f->video_start;
        options.frame_count=f->video_frames?f->video_frames:jfx_editor_kind(f->editor)==JFX_PROJECT_KIND_GRAPH?300:0;
        options.audio=f->video_audio;
        editor_result(f,jfx_export_begin(f->editor,&options,&f->export_job));
    }
    ImGui::EndDisabled();
    if (running) {
        ImGui::ProgressBar((float)((double)jfx_export_completed_frames(f->export_job)/(double)jfx_export_total_frames(f->export_job)));
        if (ImGui::Button("Cancel video export")) jfx_export_cancel(f->export_job);
    }
}
void timeline_tracks(jfx_desktop_frontend_t *f,bool daw=false) {
    auto *t = jfx_editor_timeline(f->editor);
    auto command=[&](const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double v=0,const char *text="") {
        auto r=jfx_desktop_frontend_edit(f,op,a,b,c,v,text); editor_result(f,r);
        t=jfx_editor_timeline(f->editor); return r;
    };
    if (f->selected_track>=jfx_timeline_track_count(t)) f->selected_track=0;
    if (f->selected_clip>=jfx_timeline_clip_count(t,f->selected_track)) f->selected_clip=0;
    uint64_t frame=jfx_desktop_frontend_timeline_current_frame(f);
    char code[64]; jfx_timeline_timecode(t,frame,code,sizeof(code));
    ImGui::Text("%s | %u/%u fps",code,jfx_timeline_fps_num(t),jfx_timeline_fps_den(t));
    ImGui::SameLine();
    if (ImGui::Button("Preview sequence")) {
        command("sequence");
    }
    ImGui::SameLine(); ImGui::Checkbox("Loop", &f->looping);
    ImGui::SameLine();
    ImGui::BeginDisabled(!jfx_editor_can_undo(f->editor));
    if (ImGui::Button("Undo")) { command("undo"); f->nle_drag=0; }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!jfx_editor_can_redo(f->editor));
    if (ImGui::Button("Redo")) { command("redo"); f->nle_drag=0; }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Add track")) {
        if (command("track.add",0,0,0,0,daw?"Audio":"Video")==JFX_SUCCESS) { f->selected_track=(uint32_t)jfx_timeline_track_count(t)-1; f->selected_clip=0; }
    }
    if (f->nle_zoom<=0) { f->nle_zoom=2; f->nle_snap=true; }
    ImGui::SetNextItemWidth(160); ImGui::SliderFloat("Zoom px/frame",&f->nle_zoom,0.1f,20,"%.1f");
    ImGui::SameLine(); ImGui::Checkbox("Snap",&f->nle_snap);
    ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::InputScalar("First frame",ImGuiDataType_U64,&f->nle_first);
    if (f->nle_first>1000000000) f->nle_first=1000000000;
    ImGui::SameLine(); if (ImGui::Button("Fit")) { f->nle_first=0; f->nle_zoom=(ImGui::GetContentRegionAvail().x+400)/((float)jfx_timeline_duration(t)+30); if (f->nle_zoom<0.01f) f->nle_zoom=0.01f; }
    ImGui::SetNextItemWidth(280); ImGui::InputText("Media path", f->media_path, sizeof(f->media_path));
    ImGui::SameLine();
    if (ImGui::BeginCombo("Add clip", "Choose source")) {
        for (int source = 0; source < JFX_CLIP_SOURCE_COUNT; ++source) {
            if (daw && source!=JFX_CLIP_AUDIO) continue;
            if (ImGui::Selectable(jfx_clip_source_name((jfx_clip_source_t)source))) {
                if (frame<=UINT32_MAX && command("clip.add",f->selected_track,(uint32_t)source,(uint32_t)frame,90,f->media_path)==JFX_SUCCESS)
                    f->selected_clip=(uint32_t)jfx_timeline_clip_count(t,f->selected_track)-1;
            }
        }
        ImGui::EndCombo();
    }
    float scale=f->nle_zoom;
    ImGui::BeginChild("NLE lanes",ImVec2(0,daw?240.0f:125.0f),true);
    float available=ImGui::GetContentRegionAvail().x;
    ImVec2 ruler=ImGui::GetCursorScreenPos(); ruler.x+=120;
    ImGui::SetCursorScreenPos(ruler); ImGui::InvisibleButton("Ruler",ImVec2(available>120?available-120:20,20));
    auto *draw=ImGui::GetWindowDrawList();
    uint64_t step=(uint64_t)std::ceil(60/scale); if (!step) step=1;
    for (uint64_t tick=f->nle_first;tick<f->nle_first+(uint64_t)(available/scale);tick+=step) {
        float x=ruler.x+(float)(tick-f->nle_first)*scale;
        char label[32]; std::snprintf(label,sizeof(label),"%llu",(unsigned long long)tick);
        draw->AddText(ImVec2(x,ruler.y),IM_COL32_WHITE,label);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(0)) {
        double at=(double)((ImGui::GetIO().MousePos.x-ruler.x)/scale)+(double)f->nle_first;
        f->time_seconds=std::fmax(0,std::round(at))/jfx_timeline_fps(t); f->playing=false; f->preview_dirty=true;
    }
    for (uint32_t reverse=(uint32_t)jfx_timeline_track_count(t);reverse>0;--reverse) {
        uint32_t track=reverse-1;
        ImGui::PushID((int)track);
        bool muted=jfx_timeline_track_muted(t,track);
        if (ImGui::Checkbox("M",&muted)) command("track.mute",track,0,0,muted);
        ImGui::SameLine();
        bool solo=jfx_timeline_track_solo(t,track);
        if (ImGui::Checkbox("S",&solo)) command("track.solo",track,0,0,solo);
        ImGui::SameLine();
        if (ImGui::Selectable(jfx_timeline_track_name(t,track),f->selected_track==track,0,ImVec2(50,20))) { f->selected_track=track; f->selected_clip=0; }
        ImGui::SameLine();
        ImVec2 origin=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("lane",ImVec2(available>120?available-120:20,28));
        bool lane_hover=ImGui::IsItemHovered();
        if (lane_hover && f->nle_drag) f->nle_drop_track=track;
        if (lane_hover && ImGui::IsMouseDragging(2)) {
            double first=(double)f->nle_first-(double)(ImGui::GetIO().MouseDelta.x/scale);
            f->nle_first=(uint64_t)std::fmax(0,first);
        }
        bool hit=false;
        draw->PushClipRect(origin,ImVec2(origin.x+available-120,origin.y+28),true);
        for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
            float x=origin.x+(float)((double)jfx_timeline_clip_start(t,track,clip)-(double)f->nle_first)*scale;
            float end=x+(float)jfx_timeline_clip_length(t,track,clip)*scale;
            ImVec2 a(x,origin.y),b(end,origin.y+26);
            bool selected=f->selected_track==track && f->selected_clip==clip;
            draw->AddRectFilled(a,b,!jfx_timeline_clip_enabled(t,track,clip)?IM_COL32(65,65,65,255):selected?IM_COL32(80,160,220,255):IM_COL32(65,90,125,255),3);
            draw->AddText(ImVec2(x+3,origin.y+3),IM_COL32_WHITE,jfx_timeline_clip_name(t,track,clip));
            if (lane_hover && ImGui::IsMouseHoveringRect(a,b)) {
                hit=true;
                if (ImGui::IsMouseClicked(0)) {
                    f->selected_track=track; f->selected_clip=clip;
                    f->nle_drag_track=track; f->nle_drop_track=track; f->nle_drag_clip=clip;
                    f->nle_drag_x=ImGui::GetIO().MousePos.x;
                    f->nle_drag_start=jfx_timeline_clip_start(t,track,clip); f->nle_drag_length=jfx_timeline_clip_length(t,track,clip);
                    f->nle_drag=f->nle_drag_x<x+6?2:f->nle_drag_x>end-6?3:1;
                }
            }
            if (f->nle_drag && f->nle_drag_track==track && f->nle_drag_clip==clip) {
                float delta=ImGui::GetIO().MousePos.x-f->nle_drag_x;
                draw->AddRect(ImVec2(x+(f->nle_drag!=3?delta:0),origin.y),ImVec2(end+(f->nle_drag!=2?delta:0),origin.y+26),IM_COL32(255,215,100,255),3.0f,2.0f);
            }
        }
        if (!hit && lane_hover && ImGui::IsMouseClicked(0)) {
            f->selected_track=track;
            f->time_seconds=std::fmax(0,std::round((double)((ImGui::GetIO().MousePos.x-origin.x)/scale)+(double)f->nle_first))/jfx_timeline_fps(t);
            f->playing=false; f->preview_dirty=true;
        }
        float playhead=origin.x+(float)((double)jfx_desktop_frontend_timeline_current_frame(f)-(double)f->nle_first)*scale;
        draw->AddLine(ImVec2(playhead,origin.y),ImVec2(playhead,origin.y+28),IM_COL32(255,200,80,255),2);
        draw->PopClipRect(); ImGui::PopID();
    }
    ImGui::EndChild();
    if (f->nle_drag && ImGui::IsMouseReleased(0)) {
        int64_t delta=(int64_t)std::llround((ImGui::GetIO().MousePos.x-f->nle_drag_x)/scale);
        int64_t edge=(int64_t)f->nle_drag_start+(f->nle_drag==3?(int64_t)f->nle_drag_length:0)+delta;
        if (f->nle_snap) {
            int64_t best=edge; double distance=8/scale;
            auto snap=[&](uint64_t candidate) { double d=std::fabs((double)candidate-(double)edge); if (d<distance) { distance=d; best=(int64_t)candidate; } };
            snap(frame);
            if (daw) {
                double frames_per_beat=jfx_timeline_fps(t)*60/jfx_timeline_audio_tempo(t);
                double beat=std::fmax(0,std::round((double)edge/frames_per_beat)*frames_per_beat);
                snap((uint64_t)std::llround(beat));
            }
            for (uint32_t c=0;c<jfx_timeline_clip_count(t,f->nle_drop_track);++c) {
                if (f->nle_drop_track==f->nle_drag_track && c==f->nle_drag_clip) continue;
                auto start=jfx_timeline_clip_start(t,f->nle_drop_track,c); snap(start); snap(start+jfx_timeline_clip_length(t,f->nle_drop_track,c));
            }
            delta+=best-edge;
        }
        auto start=(int64_t)f->nle_drag_start, length=(int64_t)f->nle_drag_length;
        if (delta || f->nle_drop_track!=f->nle_drag_track) {
            if (f->nle_drag==1) {
                uint32_t destination=f->nle_drop_track;
                if (command("clip.move",f->nle_drag_track,f->nle_drag_clip,destination,(double)(start+delta))==JFX_SUCCESS) {
                    f->selected_track=destination; f->selected_clip=destination==f->nle_drag_track?f->nle_drag_clip:(uint32_t)jfx_timeline_clip_count(t,destination)-1;
                }
            } else if (start+(f->nle_drag==2?delta:0)>=0 && start+(f->nle_drag==2?delta:0)<=UINT32_MAX)
                command("clip.trim",f->nle_drag_track,f->nle_drag_clip,(uint32_t)(start+(f->nle_drag==2?delta:0)),(double)(length+(f->nle_drag==2?-delta:delta)));
        }
        f->nle_drag=0;
    }
    if (f->selected_track<jfx_timeline_track_count(t)) {
        ImGui::SetNextItemWidth(100); ImGui::InputInt("Gap frames",&f->nle_gap_length);
        ImGui::SameLine();
        if (ImGui::SmallButton("Insert gap at playhead") && frame<=UINT32_MAX)
            command("track.insert_gap",f->selected_track,0,(uint32_t)frame,f->nle_gap_length);
        char name[JFX_TIMELINE_NAME_MAX]; std::snprintf(name,sizeof(name),"%s",jfx_timeline_track_name(t,f->selected_track));
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("Track name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)) command("track.name",f->selected_track,0,0,0,name);
        ImGui::SameLine(); if (ImGui::SmallButton("Track up") && f->selected_track+1<jfx_timeline_track_count(t)) { command("track.move",f->selected_track,0,0,f->selected_track+1); ++f->selected_track; }
        ImGui::SameLine(); if (ImGui::SmallButton("Track down") && f->selected_track) { command("track.move",f->selected_track,0,0,f->selected_track-1); --f->selected_track; }
        ImGui::SameLine(); if (ImGui::SmallButton("Remove track")) { command("track.remove",f->selected_track); return; }
        float gain=jfx_timeline_track_audio_gain(t,f->selected_track);
        if (ImGui::SliderFloat("Track audio gain",&gain,0,4)) command("track.audio.gain",f->selected_track,0,0,gain);
    }
    if (f->selected_clip<jfx_timeline_clip_count(t,f->selected_track)) {
        uint64_t start=jfx_timeline_clip_start(t,f->selected_track,f->selected_clip);
        uint64_t length=jfx_timeline_clip_length(t,f->selected_track,f->selected_clip);
        char name[JFX_TIMELINE_NAME_MAX]; std::snprintf(name,sizeof(name),"%s",jfx_timeline_clip_name(t,f->selected_track,f->selected_clip));
        ImGui::SetNextItemWidth(180);
        if (ImGui::InputText("Clip name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)) command("clip.name",f->selected_track,f->selected_clip,0,0,name);
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputScalar("Position",ImGuiDataType_U64,&start)) command("clip.move",f->selected_track,f->selected_clip,f->selected_track,(double)start);
        ImGui::SameLine(); ImGui::SetNextItemWidth(120);
        if (ImGui::InputScalar("Length",ImGuiDataType_U64,&length) && start<=UINT32_MAX)
            command("clip.trim",f->selected_track,f->selected_clip,(uint32_t)start,(double)length);
        ImGui::SameLine();
        ImGui::Text("Source in: %llu",(unsigned long long)jfx_timeline_clip_in_point(t,f->selected_track,f->selected_clip));
        if (ImGui::SmallButton("Split at playhead")) command("clip.split",f->selected_track,f->selected_clip,0,(double)frame);
        ImGui::SameLine(); if (ImGui::SmallButton("Duplicate")) command("clip.duplicate",f->selected_track,f->selected_clip,f->selected_track,(double)(start+length));
        ImGui::SameLine(); if (ImGui::SmallButton("Trim head here") && frame<=UINT32_MAX && frame<start+length) command("clip.trim",f->selected_track,f->selected_clip,(uint32_t)frame,(double)(start+length-frame));
        ImGui::SameLine(); if (ImGui::SmallButton("Trim tail here") && frame>start && start<=UINT32_MAX) command("clip.trim",f->selected_track,f->selected_clip,(uint32_t)start,(double)(frame-start));
        ImGui::SameLine(); if (ImGui::SmallButton("Slip -1")) command("clip.slip",f->selected_track,f->selected_clip,0,-1);
        ImGui::SameLine(); if (ImGui::SmallButton("Slip +1")) command("clip.slip",f->selected_track,f->selected_clip,0,1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete clip")) command("clip.remove",f->selected_track,f->selected_clip);
        ImGui::SameLine(); if (ImGui::SmallButton("Ripple delete")) command("clip.ripple_delete",f->selected_track,f->selected_clip);
        jfx_clip_audio_t audio={}; audio.size=sizeof(audio);
        if (jfx_timeline_get_clip_audio(t,f->selected_track,f->selected_clip,&audio)==JFX_SUCCESS &&
            (jfx_timeline_clip_source(t,f->selected_track,f->selected_clip)==JFX_CLIP_AUDIO || jfx_timeline_clip_source(t,f->selected_track,f->selected_clip)==JFX_CLIP_VIDEO)) {
            if (ImGui::Checkbox("Clip audio",&audio.enabled)) command("clip.audio.enabled",f->selected_track,f->selected_clip,0,audio.enabled?1:0);
            if (ImGui::SliderFloat("Audio gain",&audio.gain,0,4)) command("clip.audio.gain",f->selected_track,f->selected_clip,0,audio.gain);
            if (ImGui::SliderFloat("Stereo pan",&audio.pan,-1,1)) command("clip.audio.pan",f->selected_track,f->selected_clip,0,audio.pan);
            if (ImGui::InputScalar("Fade in frames",ImGuiDataType_U64,&audio.fade_in_frames)) command("clip.audio.fade_in",f->selected_track,f->selected_clip,0,(double)audio.fade_in_frames);
            if (ImGui::InputScalar("Fade out frames",ImGuiDataType_U64,&audio.fade_out_frames)) command("clip.audio.fade_out",f->selected_track,f->selected_clip,0,(double)audio.fade_out_frames);
        }
    }
    if (ImGui::CollapsingHeader("Sequence setup / frame export")) {
        ImGui::SetNextItemWidth(100); ImGui::InputInt("Width",&f->nle_width);
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputInt("Height",&f->nle_height);
        ImGui::SetNextItemWidth(100); ImGui::InputInt("FPS numerator",&f->nle_fps_num);
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputInt("FPS denominator",&f->nle_fps_den);
        if (ImGui::Button("Create empty sequence")) {
            if (f->nle_width>0 && f->nle_height>0 && f->nle_fps_num>0 && f->nle_fps_den>0 &&
                command("sequence.new",(uint32_t)f->nle_width,(uint32_t)f->nle_height,(uint32_t)f->nle_fps_num,f->nle_fps_den)==JFX_SUCCESS) {
                f->time_seconds=0; f->playing=false; f->selected_track=f->selected_clip=0; f->nle_drag=0;
            }
        }
        ImGui::InputText("Export path (PPM)",f->nle_export_path,sizeof(f->nle_export_path));
        if (ImGui::Button("Export selected frame")) editor_result(f,jfx_editor_write_frame(f->editor,frame,
            jfx_timeline_width(t),jfx_timeline_height(t),f->nle_export_path));
    }
    if (ImGui::CollapsingHeader("Encoded video export")) {
        encoded_export(f);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        bool modifier=ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) f->playing=!f->playing;
        if (ImGui::IsKeyPressed(ImGuiKey_S) && !modifier) command("clip.split",f->selected_track,f->selected_clip,0,(double)frame);
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) command(ImGui::GetIO().KeyShift?"clip.ripple_delete":"clip.remove",f->selected_track,f->selected_clip);
    }
}
#include "color_controls.inc"
/* Human-readable options for calibration's integral (enum-like) parameters.
 * Only names with kernel-documented meanings are mapped; anything else falls
 * back to a plain integer slider so the UI cannot mislabel a value. */
static const char *calibration_option_label(const char *name, int value, float maximum) {
    if (!name) return nullptr;
    if (!std::strcmp(name, "channel")) {
        static const char *options[] = {"RGB", "R", "G", "B", "A"};
        return (value >= 0 && value < 5) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "interpolation")) {
        static const char *options[] = {"Nearest", "Linear/Tri"};
        return (value >= 0 && value < 2) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "clamp") || !std::strcmp(name, "preserve_luma") ||
        !std::strcmp(name, "preserve_hues") || !std::strcmp(name, "preserve_shadows") ||
        !std::strcmp(name, "preserve_highlights") || !std::strcmp(name, "black_point_compensation")) {
        static const char *options[] = {"Off", "On"};
        return (value >= 0 && value < 2) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "intent")) {
        static const char *options[] = {"Relative", "Absolute"};
        return (value >= 0 && value < 2) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "adaptation")) {
        static const char *options[] = {"None", "Bradford", "von Kries"};
        return (value >= 0 && value < 3) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "tone_curve")) {
        static const char *options[] = {"Reinhard", "Hable", "ACES"};
        return (value >= 0 && value < 3) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "log_curve")) {
        static const char *options[] = {"Cineon", "S-Log", "C-Log", "V-Log", "ARRI LogC"};
        return (value >= 0 && value < 5) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "color_space")) {
        static const char *options[] = {"sRGB", "Linear"};
        return (value >= 0 && value < 2) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "input_space") || !std::strcmp(name, "output_space")) {
        if (maximum <= 1.5f) {
            static const char *options[] = {"sRGB", "Linear"};
            return (value >= 0 && value < 2) ? options[value] : nullptr;
        }
        static const char *options[] = {"sRGB", "Linear", "Rec.709", "Rec.2020", "Display P3"};
        return (value >= 0 && value < 5) ? options[value] : nullptr;
    }
    if (!std::strcmp(name, "profile")) {
        static const char *options[] = {"sRGB", "Rec.709", "Rec.2020", "Display P3", "DCI-P3"};
        return (value >= 0 && value < 5) ? options[value] : nullptr;
    }
    return nullptr;
}
static bool calibration_combo(const jfx_param_desc_t &desc, float *value) {
    int current = (int)*value;
    int lo = (int)desc.minimum, hi = (int)desc.maximum;
    const char *current_label = calibration_option_label(desc.name, current, desc.maximum);
    char preview[96];
    std::snprintf(preview, sizeof(preview), "%s",
        current_label ? current_label : desc.label);
    if (!ImGui::BeginCombo(desc.label, preview)) return false;
    bool changed = false;
    for (int option = lo; option <= hi; ++option) {
        const char *label = calibration_option_label(desc.name, option, desc.maximum);
        char item[96];
        if (label) std::snprintf(item, sizeof(item), "%s", label);
        else std::snprintf(item, sizeof(item), "%d", option);
        bool selected = (option == current);
        if (ImGui::Selectable(item, selected)) {
            *value = (float)option;
            changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}
void effect_stack(jfx_desktop_frontend_t *f, jfx_color_section_t section = JFX_COLOR_NONE) {
    auto *t=jfx_editor_timeline(f->editor); uint32_t track=f->selected_track, clip=f->selected_clip;
    if (clip>=jfx_timeline_clip_count(t,track)) { ImGui::TextUnformatted("Select a clip in the timeline."); return; }
    ImGui::Text("Clip: %s",jfx_timeline_clip_name(t,track,clip));
    if (section == JFX_COLOR_NONE) {
        ImGui::TextUnformatted("Layer stack: transforms, keys, blends and adjustments.");
        ImGui::TextDisabled("Layers composite in clip order; blend and opacity control how each layer mixes.");
    } else if (section == JFX_COLOR_CALIBRATION) {
        ImGui::TextUnformatted("Calibration pipeline: normalize white, levels and spaces first.");
        ImGui::TextDisabled("Technical correction before creative grading. Suggested order: white balance, black/white levels, gamma, log/legal, color space, LUT.");
    }
    /* Count section-local stages for "Stage N of M" numbering. */
    uint32_t stages = 0;
    for (uint32_t i = 0; i < jfx_timeline_effect_count(t, track, clip); ++i)
        if (jfx_color_section(jfx_timeline_effect_kind_desc(t, track, clip, i)) == section) ++stages;
    if (section == JFX_COLOR_NONE) {
        if (ImGui::BeginCombo("Add layer effect", "Choose operator")) {
            for (size_t i=0;i<jfx_node_kind_count();++i) {
                const auto *k=jfx_node_kind_at(i);
                if (jfx_color_section(k)!=section) continue;
                if (k->input_count==1 && k->inputs[0].type==JFX_PORT_IMAGE) {
                    char item[192]; std::snprintf(item, sizeof(item), "%s / %s", k->category, k->label);
                    if (ImGui::Selectable(item))
                        editor_result(f,jfx_desktop_frontend_layer_effects_add(f,track,clip,k->name));
                }
            }
            ImGui::EndCombo();
        }
    } else {
        const char *combo_label = section == JFX_COLOR_CALIBRATION ? "Add calibration stage" : "Add effect";
        if (ImGui::BeginCombo(combo_label, "Choose operator")) {
            for (size_t i=0;i<jfx_node_kind_count();++i) {
                const auto *k=jfx_node_kind_at(i);
                if (jfx_color_section(k)!=section) continue;
                if (k->input_count==1 && k->inputs[0].type==JFX_PORT_IMAGE && ImGui::Selectable(k->label))
                    editor_result(f,jfx_desktop_frontend_layer_effects_add(f,track,clip,k->name));
            }
            ImGui::EndCombo();
        }
    }
    if (section == JFX_COLOR_CALIBRATION && stages > 0) {
        bool any_enabled = false;
        for (uint32_t i = 0; i < jfx_timeline_effect_count(t, track, clip); ++i) {
            if (jfx_color_section(jfx_timeline_effect_kind_desc(t, track, clip, i)) != section) continue;
            if (jfx_timeline_effect_enabled(t, track, clip, i)) { any_enabled = true; break; }
        }
        if (ImGui::SmallButton(any_enabled ? "Bypass all calibration" : "Enable all calibration")) {
            finish_grading(f);
            finish_audio(f);
            if (jfx_editor_begin_edit(f->editor, JFX_PROJECT_KIND_SEQUENCE) == JFX_SUCCESS) {
                jfx_result_t first_error = JFX_SUCCESS;
                for (uint32_t i = 0; i < jfx_timeline_effect_count(t, track, clip); ++i) {
                    if (jfx_color_section(jfx_timeline_effect_kind_desc(t, track, clip, i)) != section) continue;
                    jfx_result_t r = jfx_editor_command(f->editor, "effect.enabled", track, clip, i,
                        !any_enabled ? 1.0 : 0.0, "");
                    if (r != JFX_SUCCESS && first_error == JFX_SUCCESS) first_error = r;
                }
                if (first_error == JFX_SUCCESS) {
                    editor_result(f, jfx_editor_commit_edit(f->editor));
                } else {
                    jfx_editor_cancel_edit(f->editor);
                    editor_result(f, first_error);
                }
                t = jfx_editor_timeline(f->editor);
            } else {
                editor_result(f, JFX_ERROR_BUSY);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u stage%s", stages, stages == 1 ? "" : "s");
    }
    uint32_t ordinal = 0;
    for (uint32_t e=0;e<jfx_timeline_effect_count(t,track,clip);++e) {
        const auto *k=jfx_timeline_effect_kind_desc(t,track,clip,e);
        if (jfx_color_section(k)!=section) continue;
        ++ordinal;
        ImGui::PushID((int)e);
        if (section == JFX_COLOR_NONE)
            ImGui::Text("Layer %u of %u (clip order %u): %s [%s]", ordinal, stages, e, k->label, k->category);
        else if (section == JFX_COLOR_CALIBRATION)
            ImGui::Text("Stage %u of %u (clip order %u): %s", ordinal, stages, e, k->label);
        else
            ImGui::Text("%s", k->label);
        bool enabled=jfx_timeline_effect_enabled(t,track,clip,e);
        const char *toggle_label = section == JFX_COLOR_CALIBRATION ? (enabled ? "Calibrated" : "Bypassed") : k->label;
        if (ImGui::Checkbox(toggle_label,&enabled)) editor_result(f,jfx_desktop_frontend_layer_effects_set_enabled(f,track,clip,e,enabled));
        float opacity=jfx_timeline_effect_opacity(t,track,clip,e);
        if (ImGui::SliderFloat(section == JFX_COLOR_CALIBRATION ? "Stage mix##Opacity" : "Opacity",&opacity,0,1)) editor_result(f,jfx_desktop_frontend_layer_effects_set_opacity(f,track,clip,e,opacity));
        auto blend=jfx_timeline_effect_blend(t,track,clip,e);
        if (ImGui::BeginCombo("Blend",jfx_blend_mode_name(blend))) {
            for (int b=0;b<JFX_BLEND_COUNT;++b) if (ImGui::Selectable(jfx_blend_mode_name((jfx_blend_mode_t)b)))
                editor_result(f,jfx_desktop_frontend_layer_effects_set_blend(f,track,clip,e,(jfx_blend_mode_t)b));
            ImGui::EndCombo();
        }
        size_t wheel_params=0;
        if (section==JFX_COLOR_GRADING && (!std::strcmp(k->name,"grade_color_wheels") || !std::strcmp(k->name,"lift_gamma_gain"))) {
            const char *tonal[]={"Shadows","Midtones","Highlights"}, *primary[]={"Lift","Gamma","Gain"};
            auto labels=!std::strcmp(k->name,"grade_color_wheels")?tonal:primary;
            if (ImGui::BeginTable("Color wheels",3,ImGuiTableFlags_SizingStretchSame)) {
                for (size_t w=0;w<3;++w) {
                    ImGui::TableNextColumn(); ImGui::PushID((int)w);
                    float rgb[3]; for (size_t c=0;c<3;++c) rgb[c]=jfx_timeline_effect_param(t,track,clip,e,w*3+c);
                    if (grading_wheel(labels[w],k->params+w*3,rgb,ImGui::GetContentRegionAvail().x))
                        for (size_t c=0;c<3;++c) if (rgb[c]!=jfx_timeline_effect_param(t,track,clip,e,w*3+c) && begin_grading(f))
                            editor_result(f,jfx_desktop_frontend_layer_effects_set_param(f,track,clip,e,w*3+c,rgb[c]));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            wheel_params=9;
        }
        bool dials=section==JFX_COLOR_GRADING;
        bool calibration = section == JFX_COLOR_CALIBRATION;
        int columns=(int)(ImGui::GetContentRegionAvail().x/150);
        if (columns<1) columns=1;
        if (columns>4) columns=4;
        bool table=dials && ImGui::BeginTable("Grading dials",columns,ImGuiTableFlags_SizingStretchSame);
        for (size_t p=0;p<k->param_count;++p) {
            if (table) ImGui::TableNextColumn();
            ImGui::PushID((int)p);
            float v=jfx_timeline_effect_param(t,track,clip,e,p);
            bool changed_value=false;
            if (calibration && k->params[p].integral &&
                calibration_option_label(k->params[p].name, (int)v, k->params[p].maximum)) {
                changed_value=calibration_combo(k->params[p],&v);
            } else if (k->params[p].integral) {
                int integer=(int)v;
                changed_value=ImGui::SliderInt(k->params[p].label,&integer,(int)k->params[p].minimum,(int)k->params[p].maximum);
                v=(float)integer;
            } else if (calibration && !std::strcmp(k->params[p].name, "temperature")) {
                int kelvin=(int)std::lround(v);
                changed_value=ImGui::SliderInt("Temperature (K)",&kelvin,(int)k->params[p].minimum,(int)k->params[p].maximum,"%d K");
                v=(float)kelvin;
            } else if (dials && p>=wheel_params) {
                changed_value=grading_dial(k->params[p],&v,ImGui::GetContentRegionAvail().x);
            } else changed_value=ImGui::SliderFloat(k->params[p].label,&v,k->params[p].minimum,k->params[p].maximum);
            if (changed_value && v!=jfx_timeline_effect_param(t,track,clip,e,p) && (!dials || begin_grading(f)))
                editor_result(f,jfx_desktop_frontend_layer_effects_set_param(f,track,clip,e,p,v));
            if (ImGui::SmallButton(calibration ? "Set calibration key" : "Set key")) editor_result(f,jfx_desktop_frontend_layer_effects_add_key(f,track,clip,e,p,jfx_desktop_frontend_timeline_current_frame(f),v));
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove key")) editor_result(f,jfx_desktop_frontend_layer_effects_remove_key(f,track,clip,e,p,jfx_desktop_frontend_timeline_current_frame(f)));
            ImGui::SameLine(); ImGui::Text("%zu keys",jfx_timeline_key_count(t,track,clip,e,p));
            ImGui::PopID();
        }
        if (table) ImGui::EndTable();
        for (size_t i=0;i<k->string_count;++i) {
            char text[512]; const char *value=jfx_timeline_effect_string(t,track,clip,e,i);
            std::snprintf(text,sizeof(text),"%s",value?value:"");
            if (ImGui::InputText(k->strings[i],text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue)) {
                jfx_lut_t *lut=nullptr;
                auto r=(*text && section!=JFX_COLOR_NONE)?jfx_lut_load_auto(text,&lut,f->status,sizeof(f->status)):JFX_SUCCESS;
                if (r==JFX_SUCCESS && lut && std::strcmp(k->name,"lut") && lut->shape==JFX_LUT_SHAPE_2D) r=JFX_ERROR_INVALID_ARGUMENT;
                jfx_lut_destroy(lut);
                if (r==JFX_SUCCESS) r=jfx_desktop_frontend_layer_effects_set_string(f,track,clip,e,i,text);
                editor_result(f,r);
            }
        }
        bool changed=false;
        const char *reset_label = section == JFX_COLOR_CALIBRATION ? "Reset stage" : "Reset parameters";
        if (section!=JFX_COLOR_NONE && ImGui::SmallButton(reset_label)) {
            finish_grading(f);
            if (begin_grading(f)) for (size_t p=0;p<k->param_count;++p)
                editor_result(f,jfx_desktop_frontend_layer_effects_set_param(f,track,clip,e,p,k->params[p].default_value));
            finish_grading(f);
        }
        if (section == JFX_COLOR_CALIBRATION) {
            ImGui::SameLine();
            ImGui::TextDisabled("Renders in shared clip order with layers and grades.");
        }
        uint32_t previous=UINT32_MAX, next=UINT32_MAX;
        for (uint32_t i=0;i<jfx_timeline_effect_count(t,track,clip);++i) {
            if (jfx_color_section(jfx_timeline_effect_kind_desc(t,track,clip,i))!=section) continue;
            if (i<e) previous=i;
            if (i>e) { next=i; break; }
        }
        if (previous!=UINT32_MAX && ImGui::SmallButton("Up")) { editor_result(f,jfx_desktop_frontend_layer_effects_move(f,track,clip,e,previous)); changed=true; }
        ImGui::SameLine();
        if (next!=UINT32_MAX && ImGui::SmallButton("Down")) { editor_result(f,jfx_desktop_frontend_layer_effects_move(f,track,clip,e,next)); changed=true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) { editor_result(f,jfx_desktop_frontend_layer_effects_remove(f,track,clip,e)); changed=true; }
        ImGui::Separator(); ImGui::PopID(); if (changed) break;
    }
    if (f->grading_edit && !ImGui::IsAnyItemActive()) finish_grading(f);
}
void node_panel(jfx_desktop_frontend_t *f) {
    auto *g=jfx_editor_graph(f->editor);
    auto command=[&](const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double v=0,const char *text="") {
        auto r=jfx_desktop_frontend_edit(f,op,a,b,c,v,text); editor_result(f,r);
        g=jfx_editor_graph(f->editor); return r;
    };
    if (f->node_zoom<=0) { f->node_zoom=0.8f; f->node_pan_x=f->node_pan_y=20; }
    if (ImGui::Button("New composition")) { command("graph.new",320,180); f->selected_node=0; f->node_drag=0; f->node_wiring=false; f->node_preview_selected=false; }
    ImGui::SameLine(); if (ImGui::Button("Preview composition")) { command("graph"); f->node_preview_selected=false; }
    ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_undo(f->editor));
    if (ImGui::Button("Undo")) { command("undo"); f->node_drag=0; f->node_wiring=false; }
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_redo(f->editor));
    if (ImGui::Button("Redo")) { command("redo"); f->node_drag=0; f->node_wiring=false; }
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(180); ImGui::InputText("Find operator",f->node_filter,sizeof(f->node_filter));
    ImGui::SameLine();
    if (ImGui::BeginCombo("Add node","Choose typed operator")) {
        for (size_t i=0;i<jfx_node_kind_count();++i) {
            const auto *k=jfx_node_kind_at(i);
            if (*f->node_filter && !std::strstr(k->label,f->node_filter) && !std::strstr(k->name,f->node_filter) && !std::strstr(k->category,f->node_filter)) continue;
            char label[160]; std::snprintf(label,sizeof(label),"%s / %s",k->category,k->label);
            if (ImGui::Selectable(label)) editor_result(f,jfx_desktop_frontend_node_compositing_add_node(f,k->name,nullptr,&f->selected_node));
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(150); ImGui::SliderFloat("Zoom",&f->node_zoom,0.2f,2,"%.2f");
    ImGui::SameLine(); if (ImGui::Button("Fit nodes")) {
        float minx=0,miny=0,maxx=240,maxy=160;
        for (uint32_t n=0;n<jfx_graph_node_count(g);++n) { float x,y; jfx_graph_node_position(g,n,&x,&y); minx=std::fmin(minx,x); miny=std::fmin(miny,y); maxx=std::fmax(maxx,x+220); maxy=std::fmax(maxy,y+140); }
        f->node_zoom=std::fmax(0.1f,std::fmin((ImGui::GetContentRegionAvail().x+150)/(maxx-minx+40),260/(maxy-miny+40)));
        f->node_pan_x=20-minx*f->node_zoom; f->node_pan_y=20-miny*f->node_zoom;
    }
    ImGui::TextUnformatted("Drag nodes; drag output to input; right-click input to disconnect. Middle-drag to pan.");
    ImVec2 origin=ImGui::GetCursorScreenPos(),area(ImGui::GetContentRegionAvail().x,280);
    ImGui::InvisibleButton("Composition canvas",area,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle|ImGuiButtonFlags_MouseButtonRight);
    bool hovered=ImGui::IsItemHovered(); auto *draw=ImGui::GetWindowDrawList(); auto mouse=ImGui::GetIO().MousePos;
    if (hovered && ImGui::IsMouseDragging(2)) { f->node_pan_x+=ImGui::GetIO().MouseDelta.x; f->node_pan_y+=ImGui::GetIO().MouseDelta.y; }
    draw->PushClipRect(origin,ImVec2(origin.x+area.x,origin.y+area.y),true);
    draw->AddRectFilled(origin,ImVec2(origin.x+area.x,origin.y+area.y),IM_COL32(25,30,40,255));
    float zoom=f->node_zoom;
    auto position=[&](uint32_t n) {
        float x=0,y=0; jfx_graph_node_position(g,n,&x,&y);
        if (f->node_drag && n==f->selected_node) { x=f->node_start_x+(mouse.x-f->node_drag_x)/zoom; y=f->node_start_y+(mouse.y-f->node_drag_y)/zoom; }
        return ImVec2(origin.x+f->node_pan_x+x*zoom,origin.y+f->node_pan_y+y*zoom);
    };
    auto port_position=[&](uint32_t n,size_t port,bool output) { auto p=position(n); return ImVec2(p.x+(output?210:0)*zoom,p.y+(44+(float)port*23)*zoom); };
    auto color=[](jfx_port_type_t t) { return t==JFX_PORT_IMAGE?IM_COL32(100,195,250,255):t==JFX_PORT_COLOR?IM_COL32(230,150,90,255):IM_COL32(160,220,130,255); };
    size_t count=jfx_graph_node_count(g);
    if (f->selected_node>=count) f->selected_node=0;
    if (!count) f->node_preview_selected=false;
    for (uint32_t n=0;n<count;++n) {
        const auto *k=jfx_graph_node_kind(g,n);
        for (size_t port=0;port<k->input_count;++port) {
            int from=jfx_graph_input_source(g,n,port);
            if (from>=0) {
                auto a=port_position((uint32_t)from,(size_t)jfx_graph_input_source_port(g,n,port),true),b=port_position(n,port,false);
                draw->AddBezierCubic(a,ImVec2(a.x+60*zoom,a.y),ImVec2(b.x-60*zoom,b.y),b,color(k->inputs[port].type),2);
            }
        }
    }
    int hover_node=-1,hover_side=-1; size_t hover_port=0;
    for (uint32_t n=0;n<count;++n) {
        auto pos=position(n); const auto *k=jfx_graph_node_kind(g,n);
        float height=(55+(float)(k->input_count>k->output_count?k->input_count:k->output_count)*23)*zoom;
        ImVec2 end(pos.x+210*zoom,pos.y+height);
        draw->AddRectFilled(pos,end,n==f->selected_node?IM_COL32(65,140,190,255):IM_COL32(55,65,85,255),4);
        if (n==jfx_editor_output(f->editor)) draw->AddRect(pos,end,IM_COL32(250,215,90,255),4.0f,0,2.0f);
        draw->AddText(ImVec2(pos.x+8,pos.y+5),IM_COL32_WHITE,jfx_graph_node_label(g,n));
        bool node_port=false;
        for (int side=0;side<2;++side) {
            size_t ports=side?k->output_count:k->input_count; const auto *descs=side?k->outputs:k->inputs;
            for (size_t p=0;p<ports;++p) {
                auto at=port_position(n,p,side!=0); draw->AddCircleFilled(at,5,color(descs[p].type));
                draw->AddText(ImVec2(at.x+(side?-85:9)*zoom,at.y-7),IM_COL32_WHITE,descs[p].label);
                bool hit=hovered && std::fabs(mouse.x-at.x)<9 && std::fabs(mouse.y-at.y)<9;
                if (hit) {
                    node_port=true; hover_node=(int)n; hover_side=side; hover_port=p;
                }
            }
        }
        if (!node_port && hovered && ImGui::IsMouseHoveringRect(pos,end)) { hover_node=(int)n; hover_side=-1; }
    }
    /* Hit the last-drawn node only, so covered ports cannot steal a gesture. */
    if (hover_node>=0) {
        uint32_t hit=(uint32_t)hover_node;
        if (ImGui::IsMouseClicked(0)) {
            f->selected_node=hit; f->node_drag=0;
            if (f->node_preview_selected) f->preview_dirty=true;
            if (hover_side==1) { f->node_wiring=true; f->node_wire_source=hit; f->node_wire_port=(uint32_t)hover_port; }
            else if (hover_side<0) {
                f->node_drag=1; f->node_drag_x=mouse.x; f->node_drag_y=mouse.y;
                jfx_graph_node_position(g,hit,&f->node_start_x,&f->node_start_y);
            }
        }
        if (hover_side==0 && ImGui::IsMouseReleased(0) && f->node_wiring) {
            command("node.connect",f->node_wire_source,hit,(uint32_t)hover_port,f->node_wire_port); f->node_wiring=false;
        }
        if (hover_side==0 && ImGui::IsMouseClicked(1)) command("node.disconnect",hit,(uint32_t)hover_port);
        if (hover_side>=0) {
            const auto *k=jfx_graph_node_kind(g,hit); const auto &p=(hover_side?k->outputs:k->inputs)[hover_port];
            ImGui::SetTooltip("%s (%s)%s",p.label,jfx_port_type_name(p.type),p.required?" required":"");
        }
    }
    if (f->node_wiring && f->node_wire_source<count) {
        auto a=port_position(f->node_wire_source,f->node_wire_port,true);
        draw->AddLine(a,mouse,IM_COL32(255,215,90,255),2);
        if (ImGui::IsMouseReleased(0)) f->node_wiring=false;
    }
    draw->PopClipRect();
    if (f->node_drag && ImGui::IsMouseReleased(0)) {
        float x=f->node_start_x+(mouse.x-f->node_drag_x)/zoom,y=f->node_start_y+(mouse.y-f->node_drag_y)/zoom;
        if (std::fabs(x-f->node_start_x)>0.01f || std::fabs(y-f->node_start_y)>0.01f) {
            char text[64]; std::snprintf(text,sizeof(text),"%.9g",(double)y); command("node.position",f->selected_node,0,0,x,text);
        }
        f->node_drag=0;
    }
    if (ImGui::CollapsingHeader("Composition raster / export")) {
        int w=(int)jfx_editor_graph_width(f->editor),h=(int)jfx_editor_graph_height(f->editor);
        if (ImGui::InputInt("Raster width",&w) && w>0) command("graph.size",(uint32_t)w,(uint32_t)h);
        if (ImGui::InputInt("Raster height",&h) && h>0) command("graph.size",(uint32_t)w,(uint32_t)h);
        ImGui::InputText("Composition export (PPM)",f->node_export_path,sizeof(f->node_export_path));
        if (ImGui::Button("Export composition")) editor_result(f,jfx_desktop_frontend_write_graph(f,UINT32_MAX,f->time_seconds,f->node_export_path));
    }
    uint32_t n=f->selected_node; const auto *k=jfx_graph_node_kind(g,n);
    if (!k) { ImGui::TextUnformatted("Add a source to begin composing."); ImGui::TextWrapped("%s",f->status); return; }
    ImGui::Text("%u: %s / %s",n,k->category,k->label);
    char label[JFX_NODE_LABEL_MAX]; std::snprintf(label,sizeof(label),"%s",jfx_graph_node_label(g,n));
    if (ImGui::InputText("Node label",label,sizeof(label),ImGuiInputTextFlags_EnterReturnsTrue)) command("node.label",n,0,0,0,label);
    for (size_t port=0;port<k->input_count;++port) {
        ImGui::PushID((int)port); int from=jfx_graph_input_source(g,n,port);
        if (ImGui::BeginCombo(k->inputs[port].label,from<0?"Disconnected":jfx_graph_node_label(g,(uint32_t)from))) {
            if (ImGui::Selectable("Disconnect")) editor_result(f,jfx_desktop_frontend_node_compositing_disconnect(f,n,port));
            for (uint32_t source=0;source<count;++source) if (source!=n) {
                const auto *sk=jfx_graph_node_kind(g,source);
                for (size_t sp=0;sp<sk->output_count;++sp) if (sk->outputs[sp].type==k->inputs[port].type) {
                    char choice[160]; std::snprintf(choice,sizeof(choice),"%u: %s / %s",source,jfx_graph_node_label(g,source),sk->outputs[sp].label);
                    if (ImGui::Selectable(choice)) editor_result(f,jfx_desktop_frontend_node_compositing_connect(f,source,sp,n,port));
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    for (size_t p=0;p<k->param_count;++p) {
        float v=jfx_graph_node_value(g,n)->scalars[p];
        if (k->params[p].integral) {
            int i=(int)v; if (ImGui::SliderInt(k->params[p].label,&i,(int)k->params[p].minimum,(int)k->params[p].maximum)) editor_result(f,jfx_desktop_frontend_node_compositing_set_param(f,n,p,(float)i));
        } else if (ImGui::SliderFloat(k->params[p].label,&v,k->params[p].minimum,k->params[p].maximum)) editor_result(f,jfx_desktop_frontend_node_compositing_set_param(f,n,p,v));
    }
    for (size_t i=0;i<k->string_count;++i) {
        char text[512]; const char *value=jfx_graph_node_string(g,n,i);
        std::snprintf(text,sizeof(text),"%s",value?value:"");
        if (ImGui::InputText(k->strings[i],text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue)) editor_result(f,jfx_desktop_frontend_node_compositing_set_string(f,n,i,text));
    }
    if (ImGui::Button("Preview output")) editor_result(f,jfx_desktop_frontend_node_compositing_set_output(f,n));
    ImGui::SameLine();
    if (ImGui::Button("Preview selected")) { command("graph"); f->node_preview_selected=true; }
    if (ImGui::Button("Duplicate node")) { if (command("node.duplicate",n)==JFX_SUCCESS) f->selected_node=(uint32_t)jfx_graph_node_count(g)-1; }
    ImGui::SameLine(); if (ImGui::Button("Reset node")) command("node.reset",n);
    ImGui::SameLine();
    if (ImGui::Button("Delete node")) { editor_result(f,jfx_desktop_frontend_node_compositing_remove_node(f,n)); f->selected_node=0; }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        bool modifier=ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (modifier && ImGui::IsKeyPressed(ImGuiKey_D) && command("node.duplicate",n)==JFX_SUCCESS) f->selected_node=(uint32_t)jfx_graph_node_count(g)-1;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) command("node.remove",n);
    }
    ImGui::TextWrapped("%s",f->status);
}
