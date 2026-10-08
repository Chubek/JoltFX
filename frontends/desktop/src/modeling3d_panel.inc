/* Shared scene commands; local state is selection, paths and transport only. */
void modeling3d_panel(jfx_desktop_frontend_t *f) {
    auto *scene=jfx_editor_scene3d(f->editor);
    auto command=[&](const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double value=0,const char *text="") {
        editor_result(f,jfx_desktop_frontend_edit(f,op,a,b,c,value,text));
    };
    uint32_t fps=jfx_scene3d_fps(scene),frames=jfx_scene3d_frames(scene);
    f->duration_seconds=(double)frames/fps;
    ImGui::TextUnformatted("3D Modeling & Animation");
    if (ImGui::Button(f->playing?"Pause 3D":"Play 3D")) {
        if (f->playing) jfx_desktop_frontend_pause(f); else jfx_desktop_frontend_play(f);
    }
    ImGui::SameLine(); if (ImGui::Button("Stop 3D")) { jfx_desktop_frontend_pause(f); jfx_desktop_frontend_seek(f,0); }
    ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_undo(f->editor)); if (ImGui::Button("Undo 3D")) command("undo"); ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_redo(f->editor)); if (ImGui::Button("Redo 3D")) command("redo"); ImGui::EndDisabled();
    int frame=int(std::min(double(frames-1),std::floor(f->time_seconds*fps+1.e-7)));
    if (ImGui::SliderInt("3D frame",&frame,0,int(frames)-1)) jfx_desktop_frontend_seek(f,(double)frame/fps);
    int clock[2]={int(fps),int(frames)};
    if (ImGui::InputInt2("FPS / duration frames",clock,ImGuiInputTextFlags_EnterReturnsTrue) && clock[0]>0 && clock[1]>0)
        command("3d.clock",uint32_t(clock[0]),uint32_t(clock[1]));
    if (ImGui::Button("Add cube")) command("3d.add",0,0,0,0,"cube");
    ImGui::SameLine(); if (ImGui::Button("Add sphere")) command("3d.add",0,0,0,0,"sphere");
    ImGui::SameLine(); if (ImGui::Button("Add plane")) command("3d.add",0,0,0,0,"plane");
    ImGui::SameLine(); if (ImGui::Button("New 3D scene")) { command("3d.new"); f->scene_object=0; jfx_desktop_frontend_seek(f,0); }
    bool table=ImGui::BeginTable("3D workspace",2,ImGuiTableFlags_Resizable);
    if (table) { ImGui::TableSetupColumn("Scene / inspector",ImGuiTableColumnFlags_WidthFixed,310); ImGui::TableSetupColumn("3D viewport"); ImGui::TableNextColumn(); }
    if (ImGui::BeginChild("3D inspector",ImVec2(0,0))) {
        uint32_t count=jfx_scene3d_object_count(scene);
        if (f->scene_object<0 || uint32_t(f->scene_object)>=count) f->scene_object=0;
        ImGui::Text("Scene objects (%u)",count);
        for (uint32_t i=0;i<count;++i) {
            jfx_object3d_info_t info={}; info.size=sizeof(info); jfx_scene3d_object_info(scene,i,&info);
            ImGui::PushID(int(i));
            if (ImGui::Selectable(info.name,uint32_t(f->scene_object)==i)) { f->scene_object=int(i); f->scene_vertex=0; }
            ImGui::PopID();
        }
        jfx_object3d_info_t info={}; info.size=sizeof(info); uint32_t selected=uint32_t(f->scene_object);
        if (jfx_scene3d_object_info(scene,selected,&info)==JFX_SUCCESS) {
            ImGui::Separator(); ImGui::Text("%u vertices | %u triangles | %u keys",info.vertices,info.triangles,info.keys);
            if (ImGui::InputText("Object name",info.name,sizeof(info.name),ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.name",selected,0,0,0,info.name);
            if (ImGui::Checkbox("Visible",&info.visible)) command("3d.visible",selected,0,0,info.visible?1:0);
            if (ImGui::Button("Duplicate mesh")) command("3d.duplicate",selected);
            ImGui::SameLine(); if (ImGui::Button("Delete mesh")) command("3d.remove",selected);
            const char *channels[]={"Position X","Position Y","Position Z","Rotation X","Rotation Y","Rotation Z","Scale X","Scale Y","Scale Z"};
            for (uint32_t ch=0;ch<9;++ch) {
                ImGui::PushID(int(ch)); ImGui::SetNextItemWidth(145);
                if (ImGui::InputFloat(channels[ch],&info.transform[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.transform",selected,ch,0,double(info.transform[ch]));
                ImGui::SameLine(); if (ImGui::SmallButton("Key")) command("3d.key",selected,ch,uint32_t(frame),double(info.transform[ch]));
                ImGui::PopID();
            }
            ImGui::TextDisabled("Enter commits a value; Key records it at the playhead.");
            for (uint32_t ch=0;ch<3;++ch) { char label[32]; std::snprintf(label,sizeof(label),"Material %s",ch==0?"R":ch==1?"G":"B");
                if (ImGui::InputFloat(label,&info.color[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.color",selected,ch,0,double(info.color[ch])); }
            if (ImGui::CollapsingHeader("Mesh editing")) {
                ImGui::InputInt("Vertex index",&f->scene_vertex); float vertex[3];
                if (f->scene_vertex>=0 && jfx_scene3d_vertex(scene,selected,uint32_t(f->scene_vertex),vertex)==JFX_SUCCESS)
                    for (uint32_t ch=0;ch<3;++ch) { const char *label=ch==0?"Vertex X":ch==1?"Vertex Y":"Vertex Z";
                        if (ImGui::InputFloat(label,&vertex[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.vertex",selected,uint32_t(f->scene_vertex),ch,double(vertex[ch])); }
                if (ImGui::Button("Subdivide (libigl)")) command("3d.subdivide",selected);
                if (ImGui::Button("Align principal axes (VTK)")) command("3d.align",selected);
            }
            if (ImGui::CollapsingHeader("Animation keys")) {
                ImGui::Combo("Channel",&f->scene_channel,"Position X\0Position Y\0Position Z\0Rotation X\0Rotation Y\0Rotation Z\0Scale X\0Scale Y\0Scale Z\0");
                ImGui::Combo("Interpolation",&f->scene_interpolation,"Hold\0Linear\0Smooth\0");
                if (ImGui::Button("Set key interpolation")) command("3d.interpolation",selected,uint32_t(f->scene_channel),uint32_t(frame),f->scene_interpolation);
                if (ImGui::Button("Remove key at frame")) command("3d.key_remove",selected,uint32_t(f->scene_channel),uint32_t(frame));
                float evaluated[9]; jfx_scene3d_sample(scene,selected,f->time_seconds,evaluated);
                ImGui::Text("Evaluated position: %.2f %.2f %.2f",double(evaluated[0]),double(evaluated[1]),double(evaluated[2]));
            }
            if (ImGui::CollapsingHeader("Rigid bodies (Bullet)")) {
                if (ImGui::InputFloat("Mass (0 = static)",&info.mass,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.mass",selected,0,0,double(info.mass));
                ImGui::InputInt("Bake frames",&f->scene_bake_frames);
                if (ImGui::Button("Bake rigid-body animation") && f->scene_bake_frames>0) command("3d.bake",0,0,uint32_t(f->scene_bake_frames));
                ImGui::TextWrapped("Convex mesh colliders; add a static plane for a floor. Bake replaces position/rotation keys of dynamic objects.");
            }
        }
        ImGui::Separator(); ImGui::InputText("PLY path",f->scene_mesh_path,sizeof(f->scene_mesh_path));
        if (ImGui::Button("Import PLY")) command("3d.import_ply",0,0,0,0,f->scene_mesh_path);
        ImGui::SameLine(); if (ImGui::Button("Export PLY")) command("3d.export_ply",selected,0,0,0,f->scene_mesh_path);
        ImGui::InputText("PNG path",f->scene_png_path,sizeof(f->scene_png_path));
        if (ImGui::Button("Export 3D PNG")) editor_result(f,jfx_scene3d_write_png(scene,f->time_seconds,960,540,f->scene_png_path));
        if (ImGui::Button("Export 3D video...")) ImGui::OpenPopup("3D encoded export");
        if (ImGui::BeginPopupModal("3D encoded export",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            encoded_export(f); ImGui::TextWrapped("%s",f->status);
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::TextWrapped("File > Save/Open uses shared .jfx scenes with embedded meshes.");
    }
    ImGui::EndChild();
    if (table) ImGui::TableNextColumn();
    if (ImGui::BeginChild("3D viewport",ImVec2(0,0))) {
        float camera[7]; jfx_scene3d_camera(scene,camera);
        const char *labels[]={"Orbit yaw","Orbit pitch","Camera distance","Target X","Target Y","Target Z","Field of view"};
        for (uint32_t i=0;i<7;++i) { ImGui::SetNextItemWidth(115);
            if (ImGui::InputFloat(labels[i],&camera[i],0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.camera",i,0,0,double(camera[i]));
            if (i%2==0 && i<6) ImGui::SameLine(); }
        if (f->preview_dirty || f->playing) {
            if (jfx_desktop_frontend_render_rgba8(f,kPreviewWidth,kPreviewHeight,f->preview_rgba,size_t(kPreviewWidth)*kPreviewHeight*4)==JFX_SUCCESS) f->preview_dirty=false;
        }
        if (f->window) {
            auto texture=jfx_desktop_window_upload_rgba8(f->window,kPreviewWidth,kPreviewHeight,f->preview_rgba);
            if (texture) { float width=ImGui::GetContentRegionAvail().x; ImGui::Image((ImTextureID)(intptr_t)texture,ImVec2(width,width*float(kPreviewHeight)/float(kPreviewWidth))); }
        } else ImGui::TextUnformatted("Headless shaded 3D viewport rendered.");
        ImGui::TextWrapped("%s",f->status);
        if (f->export_job && jfx_export_state(f->export_job)==JFX_EXPORT_RUNNING) {
            ImGui::Text("Exported %llu frames",(unsigned long long)jfx_export_completed_frames(f->export_job));
            if (ImGui::Button("Cancel 3D export")) jfx_export_cancel(f->export_job);
        }
    }
    ImGui::EndChild(); if (table) ImGui::EndTable();
}
