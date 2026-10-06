void native_callback(void *user,jfx_vst3_edit_phase_t phase,uint32_t,double) {
    auto *f=static_cast<jfx_desktop_frontend_t *>(user);
    if (phase==JFX_VST3_EDIT_BEGIN) ++f->native_gesture;
    if (phase==JFX_VST3_EDIT_END && f->native_gesture) --f->native_gesture;
    if (phase==JFX_VST3_EDIT_VALUE || phase==JFX_VST3_EDIT_DIRTY) f->native_dirty=true;
}
jfx_result_t native_sync(jfx_desktop_frontend_t *f,bool finish) {
    if (!f || !f->vst3_inspector || !f->native_session) return JFX_SUCCESS;
    jfx_result_t r=JFX_SUCCESS;
    if (f->native_dirty) {
        if (!f->native_edit) {
            finish_audio(f); finish_grading(f);
            r=jfx_editor_begin_edit(f->editor,JFX_PROJECT_KIND_SEQUENCE);
            if (r!=JFX_SUCCESS) return r;
            f->native_edit=true;
        }
        /* Flush controller changes into the metadata processor before asking it
         * for its opaque state. Playback owns independent model-snapshot racks. */
        float silent[2]{};
        r=jfx_vst3_process(f->vst3_inspector,0,1,silent);
        size_t bytes=0;
        void *data=imgui_alloc(JFX_VST3_MAX_STATE_BYTES,nullptr);
        if (!data) return JFX_ERROR_OUT_OF_MEMORY;
        if (r==JFX_SUCCESS) r=jfx_vst3_save_state(f->vst3_inspector,data,JFX_VST3_MAX_STATE_BYTES,&bytes);
        char *hex=r==JFX_SUCCESS?static_cast<char *>(imgui_alloc(bytes*2+1,nullptr)):nullptr;
        if (r==JFX_SUCCESS && !hex) r=JFX_ERROR_OUT_OF_MEMORY;
        if (r==JFX_SUCCESS) {
            auto *p=static_cast<unsigned char *>(data);
            for (size_t i=0;i<bytes;++i) { hex[i*2]="0123456789abcdef"[p[i]>>4]; hex[i*2+1]="0123456789abcdef"[p[i]&15]; } hex[bytes*2]=0;
            r=jfx_editor_command(f->editor,"audio.insert.state",f->vst3_inspector_track,f->vst3_inspector_insert,0,0,hex);
            if (r==JFX_SUCCESS) r=jfx_editor_command(f->editor,"audio.insert.clear_params",f->vst3_inspector_track,f->vst3_inspector_insert,0,0,"");
        }
        imgui_free(hex,nullptr); imgui_free(data,nullptr);
        if (r!=JFX_SUCCESS) return r;
        f->native_dirty=false;
        reset_audio(f); f->preview_dirty=true;
    }
    if (f->native_edit && (finish || !f->native_gesture)) {
        r=jfx_editor_commit_edit(f->editor); f->native_edit=false;
        if (r==JFX_SUCCESS) { reset_audio(f); f->preview_dirty=true; }
    }
    return r;
}
void close_inspector(jfx_desktop_frontend_t *f) {
    if (!f) return;
    if (f->native_edit) { jfx_editor_cancel_edit(f->editor); f->native_edit=false; }
    jfx_desktop_window_plugin_close(f->window);
    if (f->vst3_inspector) jfx_vst3_set_edit_callback(f->vst3_inspector,nullptr,nullptr);
    jfx_vst3_destroy(f->vst3_inspector); f->vst3_inspector=nullptr;
    f->native_session=f->native_dirty=false; f->native_gesture=0;
}
jfx_result_t open_inspector(jfx_desktop_frontend_t *f,const jfx_audio_insert_t &in) {
    auto r=native_sync(f,true); if (r!=JFX_SUCCESS) return r;
    close_inspector(f); finish_audio(f);
    const void *state=nullptr; size_t bytes=0;
    r=jfx_timeline_get_audio_insert_state(jfx_editor_timeline(f->editor),f->selected_track,f->vst3_insert,&state,&bytes);
    if (r==JFX_SUCCESS) r=jfx_vst3_create_with_state(&in,state,bytes,48000,4096,&f->vst3_inspector);
    if (r==JFX_SUCCESS) {
        f->vst3_inspector_track=f->selected_track; f->vst3_inspector_insert=f->vst3_insert;
        std::snprintf(f->vst3_inspector_path,sizeof(f->vst3_inspector_path),"%s",in.path);
        std::snprintf(f->vst3_inspector_cid,sizeof(f->vst3_inspector_cid),"%s",in.cid);
    }
    return r;
}
void recording_cancel(jfx_desktop_frontend_t *f) {
    jfx_desktop_window_capture_end(f->window); jfx_audio_recording_destroy(f->recording); f->recording=nullptr;
    f->input_peak[0]=f->input_peak[1]=0;
}
void recording_poll(jfx_desktop_frontend_t *f) {
    if (!f->recording) return;
    float pcm[JFX_AUDIO_MAX_BLOCK_FRAMES*2];
    for (unsigned block=0;block<32;++block) {
        uint32_t frames=0;
        if (!jfx_desktop_window_capture_read(f->window,pcm,JFX_AUDIO_MAX_BLOCK_FRAMES,&frames)) {
            recording_cancel(f); std::snprintf(f->status,sizeof(f->status),"Input device stopped or capture overran; take cancelled."); return;
        }
        if (!frames) break;
        auto r=jfx_audio_recording_push(f->recording,pcm,frames);
        if (r!=JFX_SUCCESS) { recording_cancel(f); editor_result(f,r); return; }
        for (unsigned c=0;c<2;++c) { f->input_peak[c]=0; for (uint32_t i=0;i<frames;++i) f->input_peak[c]=std::max(f->input_peak[c],std::fabs(pcm[i*2+c])); }
    }
    f->time_seconds=(double)f->recording_start/jfx_timeline_fps(jfx_editor_timeline(f->editor))+(double)jfx_audio_recording_frames(f->recording)/48000;
}
void recording_panel(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("Stereo input recording / 48 kHz float WAV");
    ImGui::BeginDisabled(f->recording!=nullptr);
    ImGui::InputText("Take file",f->recording_path,sizeof(f->recording_path));
    const char *name=f->recording_device<0?"Default input":jfx_desktop_window_input_name(f->recording_device);
    if (ImGui::BeginCombo("Input device",name?name:"Unavailable")) {
        if (ImGui::Selectable("Default input",f->recording_device<0)) f->recording_device=-1;
        int count=jfx_desktop_window_input_count();
        for (int i=0;i<count;++i) if (const char *device=jfx_desktop_window_input_name(i)) {
            ImGui::PushID(i); if (ImGui::Selectable(device,i==f->recording_device)) f->recording_device=i; ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    auto *t=jfx_editor_timeline(f->editor);
    if (ImGui::BeginCombo("Record track",jfx_timeline_track_name(t,f->selected_track))) {
        for (uint32_t i=0;i<jfx_timeline_track_count(t);++i) if (ImGui::Selectable(jfx_timeline_track_name(t,i),i==f->selected_track)) f->selected_track=i;
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("Record a take starting at the current playhead. Stop installs the WAV and adds an undoable audio clip. Cancel discards the take.");
    ImGui::BeginDisabled(!f->window || !*f->recording_path || !jfx_timeline_track_count(t));
    if (ImGui::Button("Record input")) {
        finish_audio(f); auto r=native_sync(f,true); close_inspector(f); f->playing=false; reset_audio(f);
        if (r==JFX_SUCCESS) r=jfx_audio_recording_begin(f->recording_path,48000,&f->recording);
        if (r==JFX_SUCCESS && !jfx_desktop_window_capture_begin(f->window,f->recording_device)) { recording_cancel(f); r=JFX_ERROR_BACKEND_FAILURE; }
        if (r==JFX_SUCCESS) { f->recording_track=f->selected_track; f->recording_start=jfx_desktop_frontend_timeline_current_frame(f); } else editor_result(f,r);
    }
    ImGui::EndDisabled(); ImGui::EndDisabled();
    if (!f->recording) return;
    uint64_t samples=jfx_audio_recording_frames(f->recording);
    ImGui::Text("Recording %.2f seconds | Input peaks %.3f / %.3f",(double)samples/48000,(double)f->input_peak[0],(double)f->input_peak[1]);
    if (ImGui::Button("Stop and add take")) {
        recording_poll(f);
        if (f->recording) {
            samples=jfx_audio_recording_frames(f->recording);
            auto r=jfx_audio_recording_finish(f->recording); recording_cancel(f);
            if (r==JFX_SUCCESS && samples) {
                double duration=std::ceil((double)samples*jfx_timeline_fps(t)/48000);
                r=jfx_desktop_frontend_edit(f,"clip.add",f->recording_track,JFX_CLIP_AUDIO,(uint32_t)f->recording_start,duration,f->recording_path);
                if (r==JFX_SUCCESS) { f->selected_track=f->recording_track; f->selected_clip=(uint32_t)jfx_timeline_clip_count(jfx_editor_timeline(f->editor),f->selected_track)-1; }
            }
            editor_result(f,r);
        }
    }
    ImGui::SameLine(); if (ImGui::Button("Cancel take")) recording_cancel(f);
}
void automation_panel(jfx_desktop_frontend_t *f) {
    auto *t=jfx_editor_timeline(f->editor);
    if (ImGui::BeginCombo("Automation track",jfx_timeline_track_name(t,f->selected_track))) {
        for (uint32_t i=0;i<jfx_timeline_track_count(t);++i) if (ImGui::Selectable(jfx_timeline_track_name(t,i),i==f->selected_track)) f->selected_track=i;
        ImGui::EndCombo();
    }
    ImGui::Combo("Automation target",&f->automation_target,"Track gain\0VST3 parameter\0");
    if (f->automation_target) {
        ImGui::InputInt("Insert index (0-based)",&f->automation_insert);
        ImGui::InputScalar("Parameter ID",ImGuiDataType_U32,&f->automation_parameter);
        ImGui::TextDisabled("Parameter IDs are shown in the VST3 inspector. Automation overrides the static value during playback.");
    }
    ImGui::InputScalar("Automation frame",ImGuiDataType_U64,&f->automation_frame);
    ImGui::SameLine(); if (ImGui::Button("Use playhead")) f->automation_frame=jfx_desktop_frontend_timeline_current_frame(f);
    ImGui::SliderFloat("Automation value",&f->automation_value,0,f->automation_target?1:16,"%.4f");
    ImGui::Combo("Interpolation",&f->automation_interpolation,"Linear\0Hold\0Smooth\0");
    auto edit=[&](const char *op,uint64_t at) {
        char text[128];
        if (!std::strcmp(op,"audio.automation.remove")) std::snprintf(text,sizeof(text),"%s %llu",f->automation_target?"plugin":"gain",(unsigned long long)at);
        else std::snprintf(text,sizeof(text),"%s %llu %d",f->automation_target?"plugin":"gain",(unsigned long long)at,f->automation_interpolation);
        editor_result(f,jfx_desktop_frontend_edit(f,op,f->selected_track,f->automation_target?(uint32_t)f->automation_insert:0,f->automation_target?f->automation_parameter:0,f->automation_value,text)); t=jfx_editor_timeline(f->editor);
    };
    if (ImGui::Button("Add / update automation key")) edit("audio.automation.key",f->automation_frame);
    ImGui::SameLine(); if (ImGui::Button("Delete automation key")) edit("audio.automation.remove",f->automation_frame);
    for (uint32_t a=0;a<jfx_timeline_audio_automation_count(t,f->selected_track);++a) {
        jfx_audio_automation_lane_t lane{}; lane.size=sizeof(lane); jfx_timeline_get_audio_automation(t,f->selected_track,a,&lane);
        ImGui::PushID((int)a);
        if (lane.target==JFX_AUTOMATION_TRACK_GAIN) ImGui::TextUnformatted("Track gain lane");
        else ImGui::Text("Insert %u / parameter %u",lane.insert,lane.parameter);
        ImGui::SameLine(); if (ImGui::SmallButton("Select lane")) { f->automation_target=(int)lane.target; f->automation_insert=(int)lane.insert; f->automation_parameter=lane.parameter; f->automation_interpolation=(int)lane.interpolation; }
        ImVec2 origin=ImGui::GetCursorScreenPos(); float width=std::max(200.0f,ImGui::GetContentRegionAvail().x);
        double end=(double)std::max(jfx_timeline_duration(t),lane.keys[lane.key_count-1].frame); if (end<1) end=1;
        double scale=lane.target==JFX_AUTOMATION_TRACK_GAIN?16:1;
        auto *draw=ImGui::GetWindowDrawList(); draw->AddRectFilled(origin,ImVec2(origin.x+width,origin.y+70),IM_COL32(30,35,40,255));
        ImVec2 previous{};
        for (uint32_t k=0;k<lane.key_count;++k) {
            ImVec2 p(origin.x+(float)((double)lane.keys[k].frame/end)*width,origin.y+65-(float)(lane.keys[k].value/scale)*60);
            if (k) { if (lane.interpolation==JFX_INTERP_HOLD) draw->AddLine(previous,ImVec2(p.x,previous.y),IM_COL32(110,180,250,255),2); else draw->AddLine(previous,p,IM_COL32(110,180,250,255),2); }
            draw->AddCircleFilled(p,3,IM_COL32(240,210,110,255)); previous=p;
        }
        ImGui::Dummy(ImVec2(width,70));
        for (uint32_t k=0;k<lane.key_count;++k) {
            ImGui::PushID((int)k); ImGui::Text("Frame %llu: %.6g",(unsigned long long)lane.keys[k].frame,lane.keys[k].value);
            ImGui::SameLine(); if (ImGui::SmallButton("Select key")) {
                f->automation_target=(int)lane.target; f->automation_insert=(int)lane.insert; f->automation_parameter=lane.parameter;
                f->automation_frame=lane.keys[k].frame; f->automation_value=(float)lane.keys[k].value; f->automation_interpolation=(int)lane.interpolation;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}
void midi_panel(jfx_desktop_frontend_t *f) {
    auto *t=jfx_editor_timeline(f->editor);
    ImGui::TextWrapped("MIDI note clips drive a VST3 instrument in the first enabled insert slot. Pitch 60 is middle C. Notes use source frames, preserving split/trim/slip edits.");
    if (ImGui::Button("Add MIDI track")) {
        if (jfx_desktop_frontend_edit(f,"track.add",0,0,0,0,"Instrument")==JFX_SUCCESS) { t=jfx_editor_timeline(f->editor); f->selected_track=(uint32_t)jfx_timeline_track_count(t)-1; }
    }
    if (ImGui::BeginCombo("MIDI track",jfx_timeline_track_name(t,f->selected_track))) {
        for (uint32_t i=0;i<jfx_timeline_track_count(t);++i) if (ImGui::Selectable(jfx_timeline_track_name(t,i),i==f->selected_track)) { f->selected_track=i; f->selected_clip=0; }
        ImGui::EndCombo();
    }
    ImGui::InputInt("MIDI clip duration (frames)",&f->audio_import_frames);
    if (ImGui::Button("Add MIDI clip") && f->audio_import_frames>0) {
        auto r=jfx_desktop_frontend_edit(f,"clip.add",f->selected_track,JFX_CLIP_MIDI,(uint32_t)jfx_desktop_frontend_timeline_current_frame(f),f->audio_import_frames,""); editor_result(f,r);
        t=jfx_editor_timeline(f->editor);
        if (r==JFX_SUCCESS) f->selected_clip=(uint32_t)jfx_timeline_clip_count(t,f->selected_track)-1;
    }
    if (ImGui::BeginCombo("Note clip",jfx_timeline_clip_name(t,f->selected_track,f->selected_clip))) {
        for (uint32_t i=0;i<jfx_timeline_clip_count(t,f->selected_track);++i) if (jfx_timeline_clip_source(t,f->selected_track,i)==JFX_CLIP_MIDI) {
            ImGui::PushID((int)i); if (ImGui::Selectable(jfx_timeline_clip_name(t,f->selected_track,i),i==f->selected_clip)) f->selected_clip=i; ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (jfx_timeline_clip_source(t,f->selected_track,f->selected_clip)!=JFX_CLIP_MIDI) return;
    ImGui::InputInt("Pitch (0-127)",&f->midi_pitch); ImGui::InputInt("Channel (0-15)",&f->midi_channel);
    ImGui::InputScalar("Note start (source frames)",ImGuiDataType_U64,&f->midi_frame);
    ImGui::InputScalar("Note length (frames)",ImGuiDataType_U64,&f->midi_length);
    ImGui::SliderFloat("Velocity",&f->midi_velocity,.01f,1);
    auto note_command=[&](const char *op,uint32_t n) {
        char text[128]; std::snprintf(text,sizeof(text),"%d %llu %llu %d",f->midi_pitch,(unsigned long long)f->midi_frame,(unsigned long long)f->midi_length,f->midi_channel);
        editor_result(f,jfx_desktop_frontend_edit(f,op,f->selected_track,f->selected_clip,n,f->midi_velocity,text)); t=jfx_editor_timeline(f->editor);
    };
    if (ImGui::Button("Add note")) note_command("midi.note.add",0);
    ImGui::BeginChild("Piano roll",ImVec2(0,180),true);
    if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(720);
    ImVec2 origin=ImGui::GetCursorScreenPos(); float width=std::max(200.0f,ImGui::GetContentRegionAvail().x)-30;
    uint64_t in=jfx_timeline_clip_in_point(t,f->selected_track,f->selected_clip),length=jfx_timeline_clip_length(t,f->selected_track,f->selected_clip);
    auto *draw=ImGui::GetWindowDrawList();
    for (int row=0;row<128;++row) {
        float y=origin.y+(float)row*12; char pitch[8]; std::snprintf(pitch,sizeof(pitch),"%d",127-row);
        draw->AddText(ImVec2(origin.x,y),IM_COL32(160,160,165,255),pitch);
        draw->AddLine(ImVec2(origin.x+30,y),ImVec2(origin.x+30+width,y),IM_COL32(85,85,90,255));
    }
    for (uint32_t n=0;n<jfx_timeline_midi_note_count(t,f->selected_track,f->selected_clip);++n) {
        jfx_midi_note_t note{}; note.size=sizeof(note); jfx_timeline_get_midi_note(t,f->selected_track,f->selected_clip,n,&note);
        if (note.frame>=in+length || note.frame+note.length<=in) continue;
        float x=origin.x+30+(float)((double)(std::max(note.frame,in)-in)/(double)length)*width;
        float end=origin.x+30+(float)((double)(std::min(note.frame+note.length,in+length)-in)/(double)length)*width;
        float y=origin.y+(float)(127-note.pitch)*12;
        draw->AddRectFilled(ImVec2(x,y+1),ImVec2(std::max(x+2,end),y+11),IM_COL32(90,190,150,255));
    }
    ImGui::Dummy(ImVec2(width+30,1536)); ImGui::EndChild();
    for (uint32_t n=0;n<jfx_timeline_midi_note_count(t,f->selected_track,f->selected_clip);++n) {
        jfx_midi_note_t note{}; note.size=sizeof(note); jfx_timeline_get_midi_note(t,f->selected_track,f->selected_clip,n,&note);
        ImGui::PushID((int)n); ImGui::Text("%u: pitch %u ch %u | %llu + %llu | %.2f",n+1,note.pitch,note.channel,(unsigned long long)note.frame,(unsigned long long)note.length,(double)note.velocity);
        ImGui::SameLine(); if (ImGui::SmallButton("Select note")) { f->midi_pitch=note.pitch; f->midi_channel=note.channel; f->midi_frame=note.frame; f->midi_length=note.length; f->midi_velocity=note.velocity; }
        ImGui::SameLine(); if (ImGui::SmallButton("Update note")) note_command("midi.note.set",n);
        ImGui::SameLine(); bool removed=ImGui::SmallButton("Delete note"); if (removed) note_command("midi.note.remove",n);
        ImGui::PopID(); if (removed) break;
    }
}
