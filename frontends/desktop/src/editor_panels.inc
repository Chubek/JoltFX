/* Included in the UI namespace: widgets edit the public models and surface errors. */
void editor_result(jfx_desktop_frontend_t *f, jfx_result_t r) {
    if (r == JFX_SUCCESS) { f->editing = true; f->preview_dirty = true; }
    else std::snprintf(f->status, sizeof(f->status), "%s", jfx_result_to_string(r));
}
void timeline_tracks(jfx_desktop_frontend_t *f) {
    auto *t = jfx_editor_timeline(f->editor);
    if (ImGui::Button("Preview sequence")) {
        editor_result(f, jfx_editor_set_kind(f->editor, JFX_PROJECT_KIND_SEQUENCE));
    }
    ImGui::SameLine(); ImGui::Checkbox("Loop", &f->looping);
    ImGui::SameLine();
    if (ImGui::Button("Add track")) {
        uint32_t track = jfx_timeline_add_track(t, "Video");
        editor_result(f, track == UINT32_MAX ? JFX_ERROR_OUT_OF_MEMORY : JFX_SUCCESS);
        if (track != UINT32_MAX) { f->selected_track = track; f->selected_clip = 0; }
    }
    ImGui::InputText("Media path", f->media_path, sizeof(f->media_path));
    if (ImGui::BeginCombo("Add clip", "Choose source")) {
        for (int source = 0; source < JFX_CLIP_SOURCE_COUNT; ++source) {
            if (ImGui::Selectable(jfx_clip_source_name((jfx_clip_source_t)source))) {
                jfx_clip_desc_t d{}; d.source = (jfx_clip_source_t)source;
                d.image_path = f->media_path; d.start_frame = jfx_desktop_frontend_timeline_current_frame(f);
                d.length_frames = 90; d.opacity = 1; d.enabled = true;
                d.source_params[0]=0.4f; d.source_params[1]=0.6f; d.source_params[2]=0.8f; d.source_params[3]=1;
                uint32_t clip = jfx_timeline_add_clip(t, f->selected_track, &d);
                editor_result(f, clip == UINT32_MAX ? JFX_ERROR_INVALID_ARGUMENT : JFX_SUCCESS);
                if (clip != UINT32_MAX) f->selected_clip = clip;
            }
        }
        ImGui::EndCombo();
    }
    float available = ImGui::GetContentRegionAvail().x;
    float scale = (available > 140 ? available - 120 : 20) / (float)(jfx_timeline_duration(t) + 30);
    for (uint32_t track=0; track<jfx_timeline_track_count(t); ++track) {
        ImGui::PushID((int)track);
        bool muted=jfx_timeline_track_muted(t,track);
        if (ImGui::Checkbox("M",&muted)) editor_result(f,jfx_timeline_set_track_muted(t,track,muted));
        ImGui::SameLine();
        if (ImGui::Selectable(jfx_timeline_track_name(t,track),f->selected_track==track,0,ImVec2(70,20))) f->selected_track=track;
        ImGui::SameLine();
        ImVec2 origin=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("lane",ImVec2(available>120?available-120:20,28));
        auto *draw=ImGui::GetWindowDrawList();
        for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
            float x=origin.x+(float)jfx_timeline_clip_start(t,track,clip)*scale;
            float end=x+(float)jfx_timeline_clip_length(t,track,clip)*scale;
            ImVec2 a(x,origin.y),b(end,origin.y+26);
            bool selected=f->selected_track==track && f->selected_clip==clip;
            draw->AddRectFilled(a,b,selected?IM_COL32(80,160,220,255):IM_COL32(65,90,125,255),3);
            draw->AddText(ImVec2(x+3,origin.y+3),IM_COL32_WHITE,jfx_timeline_clip_name(t,track,clip));
            if (ImGui::IsMouseHoveringRect(a,b) && ImGui::IsMouseClicked(0)) { f->selected_track=track; f->selected_clip=clip; }
        }
        float playhead=origin.x+(float)jfx_desktop_frontend_timeline_current_frame(f)*scale;
        draw->AddLine(ImVec2(playhead,origin.y),ImVec2(playhead,origin.y+28),IM_COL32(255,200,80,255),2);
        ImGui::PopID();
    }
    if (f->selected_clip<jfx_timeline_clip_count(t,f->selected_track)) {
        uint64_t start=jfx_timeline_clip_start(t,f->selected_track,f->selected_clip);
        uint64_t length=jfx_timeline_clip_length(t,f->selected_track,f->selected_clip);
        bool trim=ImGui::InputScalar("Start frame",ImGuiDataType_U64,&start);
        trim=ImGui::InputScalar("Length",ImGuiDataType_U64,&length)||trim;
        if (trim && start<=INT64_MAX && length<=INT64_MAX)
            editor_result(f,jfx_timeline_trim_clip(t,f->selected_track,f->selected_clip,(int64_t)start,(int64_t)length));
        ImGui::SameLine();
        if (ImGui::Button("Delete clip")) editor_result(f,jfx_timeline_remove_clip(t,f->selected_track,f->selected_clip));
    }
}
void effect_stack(jfx_desktop_frontend_t *f) {
    auto *t=jfx_editor_timeline(f->editor); uint32_t track=f->selected_track, clip=f->selected_clip;
    if (clip>=jfx_timeline_clip_count(t,track)) { ImGui::TextUnformatted("Select a clip in the timeline."); return; }
    ImGui::Text("Clip: %s",jfx_timeline_clip_name(t,track,clip));
    if (ImGui::BeginCombo("Add effect","Choose operator")) {
        for (size_t i=0;i<jfx_node_kind_count();++i) {
            const auto *k=jfx_node_kind_at(i);
            if (k->input_count==1 && k->inputs[0].type==JFX_PORT_IMAGE && ImGui::Selectable(k->label))
                editor_result(f,jfx_desktop_frontend_layer_effects_add(f,track,clip,k->name));
        }
        ImGui::EndCombo();
    }
    for (uint32_t e=0;e<jfx_timeline_effect_count(t,track,clip);++e) {
        ImGui::PushID((int)e); const auto *k=jfx_timeline_effect_kind_desc(t,track,clip,e);
        bool enabled=jfx_timeline_effect_enabled(t,track,clip,e);
        if (ImGui::Checkbox(k->label,&enabled)) editor_result(f,jfx_desktop_frontend_layer_effects_set_enabled(f,track,clip,e,enabled));
        float opacity=jfx_timeline_effect_opacity(t,track,clip,e);
        if (ImGui::SliderFloat("Opacity",&opacity,0,1)) editor_result(f,jfx_desktop_frontend_layer_effects_set_opacity(f,track,clip,e,opacity));
        auto blend=jfx_timeline_effect_blend(t,track,clip,e);
        if (ImGui::BeginCombo("Blend",jfx_blend_mode_name(blend))) {
            for (int b=0;b<JFX_BLEND_COUNT;++b) if (ImGui::Selectable(jfx_blend_mode_name((jfx_blend_mode_t)b)))
                editor_result(f,jfx_desktop_frontend_layer_effects_set_blend(f,track,clip,e,(jfx_blend_mode_t)b));
            ImGui::EndCombo();
        }
        for (size_t p=0;p<k->param_count;++p) {
            ImGui::PushID((int)p);
            float v=jfx_timeline_effect_param(t,track,clip,e,p);
            if (ImGui::SliderFloat(k->params[p].label,&v,k->params[p].minimum,k->params[p].maximum))
                editor_result(f,jfx_desktop_frontend_layer_effects_set_param(f,track,clip,e,p,v));
            if (ImGui::SmallButton("Set key")) editor_result(f,jfx_desktop_frontend_layer_effects_add_key(f,track,clip,e,p,jfx_desktop_frontend_timeline_current_frame(f),v));
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove key")) editor_result(f,jfx_desktop_frontend_layer_effects_remove_key(f,track,clip,e,p,jfx_desktop_frontend_timeline_current_frame(f)));
            ImGui::SameLine(); ImGui::Text("%zu keys",jfx_timeline_key_count(t,track,clip,e,p));
            ImGui::PopID();
        }
        for (size_t i=0;i<k->string_count;++i) {
            char text[512]; const char *value=jfx_timeline_effect_string(t,track,clip,e,i);
            std::snprintf(text,sizeof(text),"%s",value?value:"");
            if (ImGui::InputText(k->strings[i],text,sizeof(text))) editor_result(f,jfx_desktop_frontend_layer_effects_set_string(f,track,clip,e,i,text));
        }
        bool changed=false;
        if (e>0 && ImGui::SmallButton("Up")) { editor_result(f,jfx_desktop_frontend_layer_effects_move(f,track,clip,e,e-1)); changed=true; }
        ImGui::SameLine();
        if (e+1<jfx_timeline_effect_count(t,track,clip) && ImGui::SmallButton("Down")) { editor_result(f,jfx_desktop_frontend_layer_effects_move(f,track,clip,e,e+1)); changed=true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) { editor_result(f,jfx_desktop_frontend_layer_effects_remove(f,track,clip,e)); changed=true; }
        ImGui::Separator(); ImGui::PopID(); if (changed) break;
    }
}
void node_panel(jfx_desktop_frontend_t *f) {
    auto *g=jfx_editor_graph(f->editor);
    if (ImGui::BeginCombo("Add node","Choose operator")) {
        for (size_t i=0;i<jfx_node_kind_count();++i) {
            const auto *k=jfx_node_kind_at(i);
            if (ImGui::Selectable(k->label)) editor_result(f,jfx_desktop_frontend_node_compositing_add_node(f,k->name,nullptr,&f->selected_node));
        }
        ImGui::EndCombo();
    }
    /* Canvas shows actual connections. Node selection and port menus share the
     * graph validator, including rejection of cycles and incompatible types. */
    ImVec2 origin=ImGui::GetCursorScreenPos(); auto *draw=ImGui::GetWindowDrawList();
    size_t count=jfx_graph_node_count(g);
    ImGui::Dummy(ImVec2(450,(float)((count+2)/3)*70+10));
    for (uint32_t n=0;n<count;++n) {
        ImVec2 pos(origin.x+(float)(n%3)*150,origin.y+(float)(n/3)*70);
        const auto *k=jfx_graph_node_kind(g,n);
        for (size_t port=0;port<k->input_count;++port) {
            int from=jfx_graph_input_source(g,n,port);
            if (from>=0) draw->AddLine(ImVec2(origin.x+(float)(from%3)*150+135,origin.y+(float)(from/3)*70+20),pos,IM_COL32(130,190,240,255),2);
        }
        ImVec2 end(pos.x+135,pos.y+42);
        draw->AddRectFilled(pos,end,n==f->selected_node?IM_COL32(65,140,190,255):IM_COL32(55,65,85,255),4);
        draw->AddText(ImVec2(pos.x+4,pos.y+4),IM_COL32_WHITE,jfx_graph_node_label(g,n));
        if (ImGui::IsMouseHoveringRect(pos,end) && ImGui::IsMouseClicked(0)) f->selected_node=n;
    }
    uint32_t n=f->selected_node; const auto *k=jfx_graph_node_kind(g,n);
    if (!k) return;
    ImGui::Text("%s",k->label);
    for (size_t port=0;port<k->input_count;++port) {
        ImGui::PushID((int)port); int from=jfx_graph_input_source(g,n,port);
        if (ImGui::BeginCombo(k->inputs[port].label,from<0?"Disconnected":jfx_graph_node_label(g,(uint32_t)from))) {
            if (ImGui::Selectable("Disconnect")) editor_result(f,jfx_desktop_frontend_node_compositing_disconnect(f,n,port));
            for (uint32_t source=0;source<count;++source) if (source!=n) {
                ImGui::PushID((int)source);
                if (ImGui::Selectable(jfx_graph_node_label(g,source))) editor_result(f,jfx_desktop_frontend_node_compositing_connect(f,source,0,n,port));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    for (size_t p=0;p<k->param_count;++p) {
        float v=jfx_graph_node_value(g,n)->scalars[p];
        if (ImGui::SliderFloat(k->params[p].label,&v,k->params[p].minimum,k->params[p].maximum)) editor_result(f,jfx_desktop_frontend_node_compositing_set_param(f,n,p,v));
    }
    for (size_t i=0;i<k->string_count;++i) {
        char text[512]; const char *value=jfx_graph_node_string(g,n,i);
        std::snprintf(text,sizeof(text),"%s",value?value:"");
        if (ImGui::InputText(k->strings[i],text,sizeof(text))) editor_result(f,jfx_desktop_frontend_node_compositing_set_string(f,n,i,text));
    }
    if (ImGui::Button("Preview output")) editor_result(f,jfx_desktop_frontend_node_compositing_set_output(f,n));
    ImGui::SameLine();
    if (ImGui::Button("Delete node")) { editor_result(f,jfx_desktop_frontend_node_compositing_remove_node(f,n)); f->selected_node=0; }
}
void editor_panels(jfx_desktop_frontend_t *f) {
    if (f->show_properties) {
        if (open_panel(f,"Layer Effects",PanelLayout::properties)) effect_stack(f);
        ImGui::End();
    }
    if (f->show_grade) {
        ImGui::SetNextWindowSize(ImVec2(350,420),ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Color Grading",&f->show_grade)) {
            ImGui::TextUnformatted("Grade the selected timeline clip");
            ImGui::InputText("LUT file",f->lut_path,sizeof(f->lut_path));
            if (ImGui::Button("Load LUT")) {
                char path[512]; std::snprintf(path,sizeof(path),"%s",f->lut_path);
                editor_result(f,jfx_desktop_frontend_color_grading_load_lut(f,path));
            }
            float mix=f->lut_mix;
            if (ImGui::SliderFloat("LUT mix",&mix,0,1)) editor_result(f,jfx_desktop_frontend_color_grading_set_lut_mix(f,mix));
            float l[3],g[3],a[3]; jfx_desktop_frontend_color_grading_get_lift_gamma_gain(f,l,g,a);
            bool changed=ImGui::SliderFloat3("Lift",l,-1,1);
            changed=ImGui::SliderFloat3("Gamma",g,0.1f,4)||changed;
            changed=ImGui::SliderFloat3("Gain",a,0,4)||changed;
            if (changed) editor_result(f,jfx_desktop_frontend_color_grading_set_lift_gamma_gain(f,l,g,a));
            ImGui::TextWrapped("%s",f->status);
        }
        ImGui::End();
    }
    if (f->show_nodes) {
        ImGui::SetNextWindowSize(ImVec2(500,460),ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Compositing",&f->show_nodes)) node_panel(f);
        ImGui::End();
    }
}
