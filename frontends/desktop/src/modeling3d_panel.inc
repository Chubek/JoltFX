/* Shared editing viewport for Design/Animation/Sculpting and GameDev. Production
 * rendering stays in Rendering/export; editing never waits for a ray tracer. */
bool modeling3d_interactive(const jfx_desktop_frontend_t *f) {
    return f->scene_navigation || f->scene_transforming || f->scene_sculpting || f->playing ||
        (f->workspace==JFX_DESKTOP_WORKSPACE_GAME && f->game_running);
}
bool modeling3d_inspection(const jfx_desktop_frontend_t *f) {
    return f->workspace==JFX_DESKTOP_WORKSPACE_GAME || f->scene_mode!=2 || f->scene_shading!=JFX_SCENE3D_SHADING_PHONG;
}
bool modeling3d_navigation_released(const jfx_desktop_frontend_t *f) {
    const auto &io=ImGui::GetIO();
    return !io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] &&
        (!f->scene_wheel_navigation || (io.MouseWheel==0 && ImGui::GetTime()>=f->scene_wheel_deadline));
}
void modeling3d_preview(jfx_desktop_frontend_t *f,jfx_scene3d_t *scene) {
    if (f->scene_png_job && f->scene_png_job->done.load(std::memory_order_acquire)) {
        if (f->scene_png_job->result==JFX_SUCCESS) std::snprintf(f->status,sizeof(f->status),"3D PNG exported: %.140s",f->scene_png_job->path.c_str());
        else editor_result(f,f->scene_png_job->result);
        tilly::destroy(f->scene_png_job); f->scene_png_job=nullptr;
    }
    if (f->scene_preview_job && f->scene_preview_job->done.load(std::memory_order_acquire)) {
        auto *job=f->scene_preview_job;
        bool interactive=modeling3d_interactive(f);
        bool current=job->revision==f->scene_revision;
        if ((current || interactive) && (interactive || !job->interactive) && job->width==f->scene_width && job->height==f->scene_height &&
            job->options.renderer==f->scene_render_options.renderer && int(job->shading)==f->scene_shading &&
            job->inspection==(modeling3d_inspection(f) || interactive) &&
            std::equal(job->background,job->background+3,f->preferences.viewport_background)) {
            if (job->result==JFX_SUCCESS) {
                // Publish the complete owned frame, including its dimensions.
                // Never clear the previous image on resize, edit or failure.
                tilly::destroy(f->scene_display); f->scene_display=job;
                f->scene_texture=nullptr;
                jfx_scene3d_destroy(job->scene); job->scene=nullptr;
                const char *name=job->shading==JFX_SCENE3D_SHADING_GOURAUD?"Gouraud":
                    job->shading==JFX_SCENE3D_SHADING_WIREFRAME?"Wireframe":
                    job->options.renderer==JFX_SCENE3D_RENDER_MONKEYTRACE?"MonkeyTrace CPU":
                    job->options.renderer==JFX_SCENE3D_RENDER_FILAMENT?"Filament":
                    job->options.renderer==JFX_SCENE3D_RENDER_JOLT_CPU?"Jolt Renderer CPU":
                    job->options.renderer==JFX_SCENE3D_RENDER_JOLT_GPU?"Jolt Renderer GPU":
                    job->options.renderer==JFX_SCENE3D_RENDER_RASTER?"Raster":"Vulkan";
                if (job->fallback) std::snprintf(f->status,sizeof(f->status),"%s render failed; raster fallback",name);
                else std::snprintf(f->status,sizeof(f->status),"%s rendered %ux%u",job->interactive?"Interactive raster":job->inspection?"Editing raster":name,job->width,job->height);
            } else editor_result(f,job->result);
            f->preview_dirty=!current || f->playing || f->time_seconds!=job->seconds;
        }
        if (job!=f->scene_display) tilly::destroy(job);
        f->scene_preview_job=nullptr;
    }
    if (!f->scene_width || !f->scene_height || f->scene_preview_job || (!f->preview_dirty && !f->playing)) return;
    ScenePreviewJob *job=nullptr;
    try {
        job=tilly::create<ScenePreviewJob>();
        if (!job) throw std::bad_alloc();
        job->scene=jfx_scene3d_clone(scene); if (!job->scene) throw std::bad_alloc();
        job->width=f->scene_width; job->height=f->scene_height;
        job->seconds=f->time_seconds; job->revision=f->scene_revision; job->options=f->scene_render_options;
        job->shading=jfx_scene3d_shading_t(f->scene_shading);
        job->interactive=modeling3d_interactive(f);
        job->inspection=modeling3d_inspection(f) || job->interactive;
        std::copy(f->preferences.viewport_background,f->preferences.viewport_background+3,job->background);
        job->pixels.resize(size_t(job->width)*job->height*4);
        if (!scheduler_submit(scene_preview_render_task,job,0)) { tilly::destroy(job); editor_result(f,JFX_ERROR_BUSY); return; }
        f->scene_preview_job=job; f->preview_dirty=false;
    } catch (...) { tilly::destroy(job); editor_result(f,JFX_ERROR_OUT_OF_MEMORY); }
}
void modeling3d_png(jfx_desktop_frontend_t *f,jfx_scene3d_t *scene) {
    ScenePreviewJob *job=nullptr;
    try {
        job=tilly::create<ScenePreviewJob>(); if (!job) throw std::bad_alloc();
        job->scene=jfx_scene3d_clone(scene); if (!job->scene) throw std::bad_alloc();
        job->width=960; job->height=540; job->seconds=f->time_seconds;
        job->options=f->scene_render_options; job->options.timeout_ms=60000; job->path=f->scene_png_path;
        if (job->path.empty()) { tilly::destroy(job); editor_result(f,JFX_ERROR_INVALID_ARGUMENT); return; }
        if (!scheduler_submit(scene_preview_render_task,job,0)) { tilly::destroy(job); editor_result(f,JFX_ERROR_BUSY); return; }
        f->scene_png_job=job;
    } catch (...) { tilly::destroy(job); editor_result(f,JFX_ERROR_OUT_OF_MEMORY); }
}
void modeling3d_sequence(jfx_desktop_frontend_t *f,jfx_scene3d_t *scene) {
    ImGui::TextUnformatted("Render image sequence");
    ImGui::TextWrapped("Filament renders the scene; stb writes lossless RGBA PNG frames. Rendering uses a snapshot of the scene, animation, camera and selected engine.");
    if (!jfx_scene3d_renderer_available(JFX_SCENE3D_RENDER_FILAMENT))
        ImGui::TextWrapped("Filament is not compiled in this build. Enable JFX_FILAMENT=ON for the Filament renderer.");
    bool running=desktop_export_running(f);
    ImGui::BeginDisabled(running);
    ImGui::InputText("Sequence prefix",f->scene_sequence_prefix,sizeof(f->scene_sequence_prefix));
    ImGui::InputInt("Render width",&f->scene_sequence_width);
    ImGui::InputInt("Render height",&f->scene_sequence_height);
    ImGui::InputScalar("First frame",ImGuiDataType_U64,&f->scene_sequence_start);
    ImGui::InputScalar("Frame count (0: remaining)",ImGuiDataType_U64,&f->scene_sequence_count);
    uint64_t frames=jfx_scene3d_frames(scene);
    uint64_t remaining=f->scene_sequence_start<frames?frames-f->scene_sequence_start:0;
    uint64_t count=f->scene_sequence_count?f->scene_sequence_count:remaining;
    bool valid=f->scene_sequence_prefix[0] && f->scene_sequence_width>0 && f->scene_sequence_width<=2048 &&
        f->scene_sequence_height>0 && f->scene_sequence_height<=2048 && count && count<=remaining;
    ImGui::Text("Scene rate: %u fps | %llu frames",jfx_scene3d_fps(scene),(unsigned long long)frames);
    ImGui::TextWrapped("Output: %s_%06llu.png",f->scene_sequence_prefix,(unsigned long long)f->scene_sequence_start);
    ImGui::TextWrapped("Use a filename prefix in an existing directory. Each completed frame replaces its numbered PNG. Cancellation keeps completed frames.");
    if (!valid) ImGui::TextWrapped("Enter a prefix, dimensions from 1 to 2048 and a range within the scene.");
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Render PNG sequence")) {
        jfx_export_destroy(f->export_job); f->export_job=nullptr;
        jfx_export_options_t options{}; options.size=sizeof(options);
        options.container="png_sequence"; options.path=f->scene_sequence_prefix;
        options.width=uint32_t(f->scene_sequence_width); options.height=uint32_t(f->scene_sequence_height);
        options.start_frame=f->scene_sequence_start; options.frame_count=f->scene_sequence_count;
        auto r=jfx_desktop_frontend_export_begin(f,&options,&f->export_job);
        editor_result(f,r);
        f->export_image_sequence=true; f->export_result_snapshot=JFX_SUCCESS;
        f->export_worker_scene=f->export_job!=nullptr;
        if (r==JFX_SUCCESS) std::snprintf(f->status,sizeof(f->status),"PNG sequence queued: %llu frames",(unsigned long long)count);
    }
    ImGui::EndDisabled(); ImGui::EndDisabled();
    if (running) {
        auto completed=desktop_export_completed(f),total=desktop_export_total(f);
        ImGui::Text("Rendered %llu / %llu frames",(unsigned long long)completed,(unsigned long long)total);
        ImGui::ProgressBar(total?float(double(completed)/double(total)):0);
        if (ImGui::Button("Cancel image sequence")) desktop_export_cancel(f);
        if (f->export_cancel_pending) ImGui::TextUnformatted("Cancelling after the active frame...");
    }
    ImGui::TextWrapped("%s",f->status);
}
// Matrices match Core's camera-local-to-world quaternion and Rz * Ry * Rx TRS.
bool modeling3d_transform_gizmo(jfx_desktop_frontend_t *f,jfx_scene3d_t *scene,ImVec2 origin,ImVec2 size) {
    if (f->scene_mode==3 || f->playing || f->scene_navigation) return false;
    jfx_object3d_info_t info{}; info.size=sizeof(info);
    if (f->scene_object<0 || jfx_scene3d_object_info(scene,uint32_t(f->scene_object),&info)!=JFX_SUCCESS || !info.visible || info.keys) return false;
    // Base-transform handles must not pretend to edit keyed/scripted poses.
    for (unsigned i=0;i<9;++i) {
        char script[2]={};
        if (jfx_scene3d_script(scene,uint32_t(f->scene_object),i,script,sizeof(script))!=JFX_SUCCESS || script[0]) return false;
    }
    auto &io=ImGui::GetIO();
    if (!f->scene_transforming && !io.WantTextInput && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt &&
        ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin,ImVec2(origin.x+size.x,origin.y+size.y)) && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) f->scene_gizmo_operation=0;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) f->scene_gizmo_operation=1;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) f->scene_gizmo_operation=2;
    }
    if (f->scene_transforming && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        editor_result(f,jfx_desktop_frontend_edit(f,"3d.transform_cancel",0,0,0,0,""));
        f->scene_transforming=false; ImGuizmo::Enable(false); return true;
    }
    float c[7],xyzw[4]; jfx_scene3d_camera(scene,c); jfx_scene3d_camera_quaternion(scene,xyzw);
    glm::quat q(xyzw[3],xyzw[0],xyzw[1],xyzw[2]);
    glm::vec3 eye=glm::vec3(c[3],c[4],c[5])+q*glm::vec3(0,0,c[2]);
    auto view=glm::mat4_cast(glm::conjugate(q))*glm::translate(glm::mat4(1),-eye);
    auto projection=glm::perspective(glm::radians(c[6]),size.x/size.y,.01f,20000.f);
    auto *t=info.transform;
    auto matrix=glm::translate(glm::mat4(1),glm::vec3(t[0],t[1],t[2]))*
        glm::mat4_cast(glm::quat(glm::radians(glm::vec3(t[3],t[4],t[5]))))*
        glm::scale(glm::mat4(1),glm::vec3(t[6],t[7],t[8]));
    const ImGuizmo::OPERATION operations[]={ImGuizmo::TRANSLATE,ImGuizmo::ROTATE,ImGuizmo::SCALE};
    int op=f->scene_gizmo_operation;
    float step=op==0?.5f:op==1?15.f:.1f; float snap[]={step,step,step};
    ImGuizmo::Enable(true); ImGuizmo::SetOrthographic(false); ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
    bool changed=ImGuizmo::Manipulate(glm::value_ptr(view),glm::value_ptr(projection),operations[op],
        f->scene_gizmo_world && op!=2?ImGuizmo::WORLD:ImGuizmo::LOCAL,glm::value_ptr(matrix),nullptr,
        f->scene_gizmo_snap || io.KeyCtrl?snap:nullptr);
    // The no-argument overload deliberately reports LAST frame's hover.
    bool using_gizmo=ImGuizmo::IsUsing(),over=ImGuizmo::IsOver(operations[op]);
    if (using_gizmo && !f->scene_transforming) {
        auto r=jfx_desktop_frontend_edit(f,"3d.transform_begin",uint32_t(f->scene_object),0,0,0,"");
        editor_result(f,r); f->scene_transforming=r==JFX_SUCCESS;
    }
    if (changed && f->scene_transforming) {
        glm::vec3 scale(glm::length(glm::vec3(matrix[0])),glm::length(glm::vec3(matrix[1])),glm::length(glm::vec3(matrix[2])));
        glm::mat3 rotation(matrix); for (int i=0;i<3;++i) rotation[i]/=std::max(scale[i],.001f);
        auto angles=glm::degrees(glm::eulerAngles(glm::normalize(glm::quat_cast(rotation))));
        // Preserve untouched channels (and multi-turn Euler values).
        float next[9]; std::copy(t,t+9,next);
        if (op==0) for (int i=0;i<3;++i) next[i]=matrix[3][i];
        if (op==1) for (int i=0;i<3;++i) next[3+i]=angles[i];
        if (op==2) for (int i=0;i<3;++i) next[6+i]=std::max(scale[i],.001f);
        char text[256]; std::snprintf(text,sizeof(text),"%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g",
            double(next[0]),double(next[1]),double(next[2]),double(next[3]),double(next[4]),double(next[5]),double(next[6]),double(next[7]),double(next[8]));
        editor_result(f,jfx_desktop_frontend_edit(f,"3d.transform_set",uint32_t(f->scene_object),0,0,0,text));
    }
    if (f->scene_transforming && !io.MouseDown[0]) {
        editor_result(f,jfx_desktop_frontend_edit(f,"3d.transform_end",0,0,0,0,"")); f->scene_transforming=false;
    }
    return using_gizmo || over;
}
void modeling3d_viewport(jfx_desktop_frontend_t *f,jfx_scene3d_t *scene) {
    auto command=[&](const char *op,double value=0,const char *text="") {
        auto r=jfx_desktop_frontend_edit(f,op,0,0,0,value,text); editor_result(f,r); return r==JFX_SUCCESS;
    };
    ImGui::BeginDisabled(f->scene_transforming || f->scene_navigation);
    ImGui::SetNextItemWidth(110); ImGui::Combo("Gizmo",&f->scene_gizmo_operation,"Move\0Rotate\0Scale\0");
    ImGui::SameLine(); ImGui::Checkbox("World axes",&f->scene_gizmo_world);
    ImGui::SameLine(); ImGui::Checkbox("Snap (Ctrl)",&f->scene_gizmo_snap);
    ImGui::EndDisabled();
    auto &io=ImGui::GetIO(); auto available=ImGui::GetContentRegionAvail();
    float width=std::max(120.f,available.x),height=std::max(120.f,std::min(width*9/16,available.y-50));
    ImVec2 origin=ImGui::GetCursorScreenPos(),size(width,height);
    auto *draw=ImGui::GetWindowDrawList(); ImVec2 end(origin.x+width,origin.y+height);
    // Process input BEFORE taking a worker snapshot. Split draw channels so the
    // resulting frame remains behind handles, despite being submitted last.
    ImDrawListSplitter layers; layers.Split(draw,2); layers.SetCurrentChannel(draw,1);
    auto present=[&] {
        float pixels_w=f->window?width*io.DisplayFramebufferScale.x:320.f;
        float pixels_h=pixels_w*height/width;
        float limit=float(f->preferences.preview_max_dimension);
        float scale=std::min({1.f,limit/pixels_w,limit/pixels_h});
        uint32_t want_w=uint32_t(std::max(1.f,std::round(pixels_w*scale)));
        uint32_t want_h=uint32_t(std::max(1.f,std::round(pixels_h*scale))),rw=want_w,rh=want_h;
        jfx_osg_preview_size(modeling3d_interactive(f)?1:0,want_w,want_h,&rw,&rh);
        if (f->scene_width!=rw || f->scene_height!=rh) {
            f->scene_width=rw; f->scene_height=rh; f->preview_dirty=true; ++f->scene_revision;
        }
        modeling3d_preview(f,scene);
        layers.SetCurrentChannel(draw,0);
        const auto *color=f->preferences.viewport_background;
        draw->AddRectFilled(origin,end,ImGui::ColorConvertFloat4ToU32(ImVec4(color[0],color[1],color[2],1)));
        if (f->window && f->scene_display) {
            const auto *frame=f->scene_display;
            if (!f->scene_texture) f->scene_texture=jfx_desktop_window_upload_rgba8(f->window,frame->width,frame->height,frame->pixels.data());
            if (f->scene_texture) draw->AddImage((ImTextureID)(intptr_t)f->scene_texture,origin,end);
        }
        layers.Merge(draw);
    };
    float xyzw[4]; jfx_scene3d_camera_quaternion(scene,xyzw); glm::quat q(xyzw[3],xyzw[0],xyzw[1],xyzw[2]);
    glm::quat view=glm::conjugate(q); ImVec2 center(end.x-65,origin.y+65);
    /* OSG-manipulator pick radius (14px) replaces the legacy 9px so the thin
     * rings are easy to grab; the hovered ring draws thicker. */
    float nearest=jfx_osg_gimbal_pick_px(); int hit=-1; ImVec2 tangent(1,0);
    const ImU32 colors[]={IM_COL32(240,85,75,255),IM_COL32(105,220,125,255),IM_COL32(100,150,255,255)};
    const ImU32 hover_colors[]={IM_COL32(255,130,120,255),IM_COL32(150,255,170,255),IM_COL32(150,190,255,255)};
    if (f->preferences.show_gimbal) {
    draw->AddCircleFilled(center,53,IM_COL32(15,18,24,190));
    for (unsigned axis=0;axis<3;++axis) {
        glm::vec3 u(0),v(0),a(0); u[int((axis+1)%3)]=1; v[int((axis+2)%3)]=1; a[int(axis)]=1;
        ImVec2 previous;
        for (unsigned i=0;i<=64;++i) {
            float angle=float(i)*6.2831853f/64; glm::vec3 p=view*(u*std::cos(angle)+v*std::sin(angle));
            ImVec2 point(center.x+p.x*44,center.y-p.y*44);
            if (i) draw->AddLine(previous,point,colors[axis],2.0f);
            float distance=std::hypot(io.MousePos.x-point.x,io.MousePos.y-point.y);
            if (distance<nearest) {
                nearest=distance; hit=int(axis);
                auto direction=view*(-u*std::sin(angle)+v*std::cos(angle));
                float length=std::hypot(direction.x,direction.y);
                if (length>.001f) tangent=ImVec2(direction.x/length,-direction.y/length);
            }
            previous=point;
        }
        auto tip=view*a; ImVec2 point(center.x+tip.x*35,center.y-tip.y*35);
        draw->AddLine(center,point,colors[axis],2); const char *label=axis==0?"X":axis==1?"Y":"Z";
        draw->AddText(point,colors[axis],label);
    }
    if (hit>=0) {
        /* Second pass: redraw the hovered ring thicker and brighter so the
         * grabbable axis reads clearly at the wider 14px pick radius. */
        glm::vec3 u(0),v(0); u[int((hit+1)%3)]=1; v[int((hit+2)%3)]=1;
        ImVec2 previous;
        for (unsigned i=0;i<=64;++i) {
            float angle=float(i)*6.2831853f/64; glm::vec3 p=view*(u*std::cos(angle)+v*std::sin(angle));
            ImVec2 point(center.x+p.x*44,center.y-p.y*44);
            if (i) draw->AddLine(previous,point,hover_colors[hit],3.5f);
            previous=point;
        }
    }
    }
    draw->PushClipRect(origin,end,true);
    bool gizmo=modeling3d_transform_gizmo(f,scene,origin,size);
    draw->PopClipRect();
    bool camera_input=io.MouseWheel!=0 || io.MouseDown[1] || io.MouseDown[2];
    if (f->scene_transforming || (gizmo && !camera_input)) ImGui::Dummy(size);
    else ImGui::InvisibleButton("##3D navigation",size,ImGuiButtonFlags_MouseButtonLeft|
        ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    // A hovered ImGuizmo uses Dummy, not an item ID, to preserve activation.
    // Still claim its wheel so zoom cannot also scroll the inspector/canvas.
    if (gizmo && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin,end))
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,ImGui::GetID("##3D navigation"));
    if (f->scene_transforming || (gizmo && !camera_input)) { present(); return; }
    if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    bool sculpt_mode=f->workspace==JFX_DESKTOP_WORKSPACE_MODELING3D && f->scene_mode==3;
    if (sculpt_mode && hovered && hit<0) {
        float camera[7]; jfx_scene3d_camera(scene,camera);
        float radius=f->scene_sculpt_radius*height/(2*camera[2]*std::tan(camera[6]*.00872664626f));
        draw->AddCircle(io.MousePos,std::clamp(radius,4.f,200.f),IM_COL32(255,195,100,230),48,1.5f);
    }
    if (sculpt_mode && active && io.MouseClicked[0] && hit<0 && !io.KeyAlt && !f->scene_navigation) {
        auto r=jfx_desktop_frontend_edit(f,"3d.sculpt_begin",uint32_t(f->scene_object),0,0,0,"");
        editor_result(f,r); f->scene_sculpting=r==JFX_SUCCESS;
    }
    if (f->scene_sculpting) {
        if (active && hovered && io.MouseDown[0] && (io.MouseClicked[0] || io.MouseDelta.x!=0 || io.MouseDelta.y!=0)) {
            char text[192];
            std::snprintf(text,sizeof(text),"%.9g %.9g %.9g %.9g %.17g",
                double((io.MousePos.x-origin.x)/width),double((io.MousePos.y-origin.y)/height),
                double(width/height),double(f->scene_sculpt_radius),f->time_seconds);
            auto r=jfx_desktop_frontend_edit(f,"3d.sculpt",uint32_t(f->scene_object),
                uint32_t(io.KeyShift?2:f->scene_sculpt_brush),f->scene_sculpt_mirror?1u:0u,
                double(f->scene_sculpt_strength)*(io.KeyCtrl?-1:1),text);
            editor_result(f,r);
        }
        present(); return;
    }
    // A scroll burst is one camera gesture and uses the same fast preview as a
    // drag. Avoid serializing/copying every mesh once per wheel event.
    if (hovered && io.MouseWheel!=0) {
        if (!f->scene_navigation && command("3d.navigation_begin")) {
            f->scene_navigation=true; f->scene_wheel_navigation=true; f->scene_gizmo_axis=-1;
        }
        if (f->scene_navigation) {
            f->scene_wheel_deadline=ImGui::GetTime()+.18;
            double direction=f->preferences.invert_zoom?-1.:1.;
            command("3d.dolly",std::clamp(double(io.MouseWheel)*double(f->preferences.zoom_speed)*direction,-10.,10.));
        }
    }
    if (active && !f->scene_navigation && (io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2]) &&
        (!sculpt_mode || !io.MouseClicked[0] || hit>=0 || io.KeyAlt)) {
        if (command("3d.navigation_begin")) {
            f->scene_navigation=true; f->scene_wheel_navigation=false; f->scene_gizmo_axis=io.MouseClicked[0]?hit:-1;
            f->scene_gimbal_tangent[0]=tangent.x; f->scene_gimbal_tangent[1]=tangent.y;
        }
    }
    if (active && f->scene_navigation && (io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2])) f->scene_wheel_navigation=false;
    if (active && f->scene_navigation && !io.MouseClicked[0] && !io.MouseClicked[1] && !io.MouseClicked[2] && (io.MouseDelta.x!=0 || io.MouseDelta.y!=0)) {
        char text[128];
        // Pointer displacement is already integrated by ImGui. Never clip it per
        // frame or multiply by delta time: both make sensitivity frame-rate dependent.
        float mdx=io.MouseDelta.x,mdy=io.MouseDelta.y;
        double speed=double(f->preferences.orbit_speed);
        if (f->scene_gizmo_axis>=0) {
            const char *axes[]={"1 0 0","0 1 0","0 0 1"};
            double along=double(mdx)*double(f->scene_gimbal_tangent[0])+double(mdy)*double(f->scene_gimbal_tangent[1]);
            command("3d.orbit_axis",along*speed*2,axes[f->scene_gizmo_axis]);
        } else if (io.MouseDown[1] || io.MouseDown[2] || io.KeyShift) {
            float camera[7]; jfx_scene3d_camera(scene,camera); float scale=2*camera[2]*std::tan(camera[6]*.00872664626f)/height*f->preferences.pan_speed;
            std::snprintf(text,sizeof(text),"%.9g %.9g 0",double(-mdx*scale),double(mdy*scale)); command("3d.pan",0,text);
        } else {
            bool roll=io.KeyAlt && !sculpt_mode;
            std::snprintf(text,sizeof(text),"%.9g %.9g %.9g",roll?0.:double(-mdx)*speed,roll?0.:double(-mdy)*speed,roll?double(mdx)*speed:0.);
            command("3d.orbit",0,text);
        }
    }
    present();
}
void modeling3d_panel(jfx_desktop_frontend_t *f) {
    auto *scene=jfx_editor_scene3d(f->editor);
    auto command=[&](const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double value=0,const char *text="") {
        auto result=jfx_desktop_frontend_edit(f,op,a,b,c,value,text); editor_result(f,result);
        if (result==JFX_SUCCESS && (!std::strcmp(op,"3d.add") || !std::strcmp(op,"3d.duplicate") || !std::strcmp(op,"3d.import_ply") || !std::strcmp(op,"3d.poly_copy"))) {
            f->scene_object=int(jfx_scene3d_object_count(scene))-1; f->scene_vertex=0;
        }
    };
    auto &io=ImGui::GetIO();
    if (f->scene_transforming && (ImGui::IsKeyPressed(ImGuiKey_Escape) || !io.MouseDown[0])) {
        command(ImGui::IsKeyPressed(ImGuiKey_Escape)?"3d.transform_cancel":"3d.transform_end");
        f->scene_transforming=false; ImGuizmo::Enable(false);
    }
    if (f->scene_sculpting && (ImGui::IsKeyPressed(ImGuiKey_Escape) || !io.MouseDown[0])) {
        command(ImGui::IsKeyPressed(ImGuiKey_Escape)?"3d.sculpt_cancel":"3d.sculpt_end"); f->scene_sculpting=false;
    }
    if (f->scene_navigation && (ImGui::IsKeyPressed(ImGuiKey_Escape) || modeling3d_navigation_released(f))) {
        command(ImGui::IsKeyPressed(ImGuiKey_Escape)?"3d.navigation_cancel":"3d.navigation_end"); f->scene_navigation=false;
    }
    uint32_t fps=jfx_scene3d_fps(scene),frames=jfx_scene3d_frames(scene);
    f->duration_seconds=(double)frames/fps;
    ImGui::TextUnformatted("3D Design, Animation & Sculpting");
    if (ImGui::BeginTabBar("3D modes")) {
        int previous=f->scene_mode;
        if (ImGui::BeginTabItem("Design")) { f->scene_mode=0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Animation")) { f->scene_mode=1; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sculpting")) { f->scene_mode=3; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Rendering")) { f->scene_mode=2; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        if (previous!=f->scene_mode && f->scene_sculpting) {
            command("3d.sculpt_end"); f->scene_sculpting=false;
        }
        if (previous!=f->scene_mode && f->scene_mode==3) {
            jfx_desktop_frontend_pause(f);
            f->scene_shading=JFX_SCENE3D_SHADING_GOURAUD; f->preview_dirty=true; ++f->scene_revision;
        }
        if (previous!=f->scene_mode && f->scene_navigation) {
            command("3d.navigation_end"); f->scene_navigation=false;
        }
        if (previous!=f->scene_mode && f->scene_transforming) {
            command("3d.transform_end"); f->scene_transforming=false; ImGuizmo::Enable(false);
        }
        if (f->scene_mode==2 && !f->scene_render_initialized) {
            f->scene_render_initialized=true;
            if (jfx_scene3d_renderer_available(JFX_SCENE3D_RENDER_FILAMENT)) {
                auto options=f->scene_render_options; options.renderer=JFX_SCENE3D_RENDER_FILAMENT;
                editor_result(f,jfx_desktop_frontend_set_scene3d_render_options(f,&options));
            }
            f->scene_shading=JFX_SCENE3D_SHADING_PHONG; f->preview_dirty=true; ++f->scene_revision;
        }
    }
    const bool animation=f->scene_mode==1,rendering=f->scene_mode==2,sculpting=f->scene_mode==3;
    if (animation) {
        if (ImGui::Button(f->playing?"Pause 3D":"Play 3D")) {
            if (f->playing) jfx_desktop_frontend_pause(f); else jfx_desktop_frontend_play(f);
        }
        ImGui::SameLine(); if (ImGui::Button("Stop 3D")) { jfx_desktop_frontend_pause(f); jfx_desktop_frontend_seek(f,0); }
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!jfx_editor_can_undo(f->editor)); if (ImGui::Button("Undo 3D")) command("undo"); ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_redo(f->editor)); if (ImGui::Button("Redo 3D")) command("redo"); ImGui::EndDisabled();
    int frame=int(std::min(double(frames-1),std::floor(f->time_seconds*fps+1.e-7)));
    if (animation) {
        if (ImGui::SliderInt("3D frame",&frame,0,int(frames)-1)) jfx_desktop_frontend_seek(f,(double)frame/fps);
        int clock[2]={int(fps),int(frames)};
        if (ImGui::InputInt2("FPS / duration frames",clock,ImGuiInputTextFlags_EnterReturnsTrue) && clock[0]>0 && clock[1]>0)
            command("3d.clock",uint32_t(clock[0]),uint32_t(clock[1]));
    }
    if (!animation && !rendering) {
        if (ImGui::Button("Add cube")) command("3d.add",0,0,0,0,"cube");
        ImGui::SameLine(); if (ImGui::Button("Add sphere") && f->scene_segments>=8 && f->scene_segments<=128) command("3d.add",uint32_t(f->scene_segments),0,0,0,"sphere");
        ImGui::SameLine(); if (ImGui::Button("Add plane")) command("3d.add",0,0,0,0,"plane");
        ImGui::SameLine(); if (ImGui::Button("New 3D scene")) { command("3d.new"); f->scene_object=0; jfx_desktop_frontend_seek(f,0); }
        const char *primitives[]={"cylinder","cone","torus","capsule","pyramid","disk","nurbs","metaball",
            "tube","hemisphere","wedge","tetrahedron","octahedron","icosahedron"};
        ImGui::SetNextItemWidth(150); ImGui::Combo("More primitives",&f->scene_primitive,primitives,int(sizeof(primitives)/sizeof(primitives[0])),14);
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputInt("Segments",&f->scene_segments);
        ImGui::SameLine(); ImGui::BeginDisabled(f->scene_segments<8 || f->scene_segments>128);
        if (ImGui::Button("Add primitive"))
            command("3d.add",uint32_t(f->scene_segments),0,0,0,primitives[f->scene_primitive]);
        ImGui::EndDisabled();
    }
    bool table=ImGui::BeginTable("3D workspace",2,ImGuiTableFlags_Resizable);
    if (table) { ImGui::TableSetupColumn("Scene / inspector",ImGuiTableColumnFlags_WidthFixed,310); ImGui::TableSetupColumn("3D viewport"); ImGui::TableNextColumn(); }
    if (ImGui::BeginChild("3D inspector",ImVec2(0,0))) {
        if (rendering) modeling3d_sequence(f,scene);
        else {
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
            jfx_procedural3d_info_t procedural={}; procedural.size=sizeof(procedural); jfx_scene3d_procedural_info(scene,selected,&procedural);
            ImGui::Separator(); ImGui::Text("%u vertices | %u triangles | %u keys",info.vertices,info.triangles,info.keys);
            if (sculpting) {
                ImGui::TextUnformatted("Surface sculpting (libigl / GLM)");
                ImGui::Combo("Brush",&f->scene_sculpt_brush,"Draw\0Inflate\0Smooth\0Flatten\0Pinch\0");
                ImGui::SliderFloat("Brush radius (local units)",&f->scene_sculpt_radius,.01f,5.f,"%.2f");
                ImGui::SliderFloat("Brush strength",&f->scene_sculpt_strength,.01f,1.f,"%.2f");
                ImGui::Checkbox("Mirror X",&f->scene_sculpt_mirror);
                ImGui::TextWrapped("Left-drag sculpts. Shift smooths; Ctrl reverses Draw/Inflate/Pinch. Alt-drag or the gimbal orbits; right/middle pans. Escape cancels the stroke.");
                if (procedural.generator) ImGui::TextWrapped("Triangulate the generator before sculpting.");
                if (procedural.instances>1) ImGui::TextWrapped("Make clones real before sculpting.");
                if (ImGui::Button("Triangulate / make editable")) command("3d.triangulate",selected);
                ImGui::BeginDisabled(procedural.generator!=0);
                if (ImGui::Button("Refine sculpt (libigl)")) command("3d.subdivide",selected);
                ImGui::EndDisabled();
                ImGui::TextUnformatted("Create polygon-level shape");
                if (ImGui::Button("Low-Poly")) command("3d.poly_copy",selected,0);
                ImGui::SameLine(); if (ImGui::Button("Mid-Poly")) command("3d.poly_copy",selected,1);
                ImGui::SameLine(); if (ImGui::Button("High-Poly")) command("3d.poly_copy",selected,2);
                ImGui::TextWrapped("Low targets 15%%, Mid 50%% (meshoptimizer, up to 10%% relative error); High refines to 4x triangles (libigl). Targets are best effort. A new selected shape retains animation; the source is hidden. Undo or select the source to derive another level.");
                ImGui::Separator();
            }
            if (ImGui::InputText("Object name",info.name,sizeof(info.name),ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.name",selected,0,0,0,info.name);
            if (ImGui::Checkbox("Visible",&info.visible)) command("3d.visible",selected,0,0,info.visible?1:0);
            if (ImGui::Checkbox("Smooth shading",&procedural.smooth)) command("3d.smooth",selected,0,0,procedural.smooth?1:0);
            if (!animation) {
                if (ImGui::Button("Duplicate mesh")) command("3d.duplicate",selected);
                ImGui::SameLine(); if (ImGui::Button("Delete mesh")) command("3d.remove",selected);
            }
            const char *channels[]={"Position X","Position Y","Position Z","Rotation X","Rotation Y","Rotation Z","Scale X","Scale Y","Scale Z"};
            for (uint32_t ch=0;ch<9;++ch) {
                ImGui::PushID(int(ch)); ImGui::SetNextItemWidth(145);
                if (ImGui::InputFloat(channels[ch],&info.transform[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.transform",selected,ch,0,double(info.transform[ch]));
                if (animation) { ImGui::SameLine(); if (ImGui::SmallButton("Key")) command("3d.key",selected,ch,uint32_t(frame),double(info.transform[ch])); }
                ImGui::PopID();
            }
            ImGui::TextDisabled(animation?"Enter commits a value; Key records it at the playhead.":"Enter commits a base transform.");
            for (uint32_t ch=0;ch<3;++ch) { char label[32]; std::snprintf(label,sizeof(label),"Material %s",ch==0?"R":ch==1?"G":"B");
                if (ImGui::InputFloat(label,&info.color[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.color",selected,ch,0,double(info.color[ch])); }
            if (!animation && ImGui::CollapsingHeader("Mesh editing")) {
                ImGui::BeginDisabled(procedural.generator!=0);
                ImGui::InputInt("Vertex index",&f->scene_vertex); float vertex[3];
                if (f->scene_vertex>=0 && jfx_scene3d_vertex(scene,selected,uint32_t(f->scene_vertex),vertex)==JFX_SUCCESS)
                    for (uint32_t ch=0;ch<3;++ch) { const char *label=ch==0?"Vertex X":ch==1?"Vertex Y":"Vertex Z";
                        if (ImGui::InputFloat(label,&vertex[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.vertex",selected,uint32_t(f->scene_vertex),ch,double(vertex[ch])); }
                if (ImGui::Button("Subdivide (libigl)")) command("3d.subdivide",selected);
                if (ImGui::Button("Align principal axes (VTK)")) command("3d.align",selected);
                if (ImGui::Button("Optimize mesh (meshoptimizer)")) command("3d.optimize",selected);
                if (ImGui::Button("Convex hull (Bullet)")) command("3d.hull",selected);
                ImGui::EndDisabled();
                if (procedural.generator && ImGui::Button("Make generator editable")) command("3d.make_editable",selected);
            }
            if (!animation && procedural.generator && ImGui::CollapsingHeader(procedural.generator==1?"NURBS surface":"Metaball field")) {
                int resolution=int(procedural.resolution);
                if (ImGui::InputInt("Surface resolution",&resolution,1,8,ImGuiInputTextFlags_EnterReturnsTrue) && resolution>=8 && resolution<=64)
                    command("3d.resolution",selected,uint32_t(resolution));
                bool nurbs=procedural.generator==1; int &point=nurbs?f->scene_control:f->scene_ball;
                ImGui::InputInt(nurbs?"Control point (0-15)":"Ball index",&point);
                uint32_t point_count=nurbs?procedural.control_count:procedural.ball_count;
                if (point>=0 && uint32_t(point)<point_count) {
                    float *p=nurbs?procedural.controls[point]:procedural.balls[point];
                    const char *labels[]={"Control X","Control Y","Control Z",nurbs?"Rational weight":"Ball radius"};
                    for (unsigned ch=0;ch<4;++ch) if (ImGui::InputFloat(labels[ch],&p[ch],0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue))
                        command(nurbs?"3d.nurbs_point":"3d.metaball_point",selected,uint32_t(point),ch,double(p[ch]));
                }
                if (!nurbs) {
                    if (ImGui::Button("Add metaball")) command("3d.metaball_add",selected,0,0,0,"0 1 0 1");
                    if (ImGui::Button("Remove metaball") && point>=0) command("3d.metaball_remove",selected,uint32_t(point));
                } else ImGui::TextWrapped("Bicubic rational 4 x 4 control grid. Weights change the surface without changing its control positions.");
            }
            if (!animation && ImGui::CollapsingHeader("Cloner")) {
                ImGui::Combo("Clone arrangement",&f->scene_clone_mode,"Off\0Linear\0Radial\0Grid\0");
                ImGui::InputInt("Clone count",&f->scene_clone_count);
                ImGui::InputFloat("Clone spacing / radius",&f->scene_clone_spacing);
                if (ImGui::Button("Apply cloner") && f->scene_clone_count>0)
                    command("3d.cloner",selected,uint32_t(f->scene_clone_mode),uint32_t(f->scene_clone_count),double(f->scene_clone_spacing));
                ImGui::Text("Current: mode %u, %u instances, %.2f spacing",procedural.cloner_mode,procedural.instances,double(procedural.spacing));
                if (ImGui::Button("Make clones real at playhead")) command("3d.cloner_make_real",selected,0,0,f->time_seconds);
            }
            if (animation && ImGui::CollapsingHeader("Joltscript animation")) {
                ImGui::Combo("Script channel",&f->scene_channel,"Position X\0Position Y\0Position Z\0Rotation X\0Rotation Y\0Rotation Z\0Scale X\0Scale Y\0Scale Z\0");
                ImGui::InputTextMultiline("##Animation script",f->scene_script,sizeof(f->scene_script),ImVec2(-1,110));
                if (ImGui::Button("Load assigned animation script")) editor_result(f,jfx_scene3d_script(scene,selected,uint32_t(f->scene_channel),f->scene_script,sizeof(f->scene_script)));
                if (ImGui::Button("Apply animation script")) command("3d.script",selected,uint32_t(f->scene_channel),0,0,f->scene_script);
                if (ImGui::Button("Remove animation script")) command("3d.script",selected,uint32_t(f->scene_channel),0,0,"");
                ImGui::InputText("Animation .jolt path",f->scene_script_path,sizeof(f->scene_script_path));
                if (ImGui::Button("Load animation .jolt")) command("3d.script_file",selected,uint32_t(f->scene_channel),0,0,f->scene_script_path);
                ImGui::TextWrapped("Inputs: time (seconds), frame, clone index, keyed value. One scalar output drives the selected transform channel during preview and export.");
                ImGui::TextWrapped("Joltscript supports loops, functions and built-in imports such as jolt.math and jolt.time. Use floating inputs and a numeric result; typed programs run on the CPU.");
            }
            if (animation && ImGui::CollapsingHeader("Animation keys")) {
                ImGui::Combo("Channel",&f->scene_channel,"Position X\0Position Y\0Position Z\0Rotation X\0Rotation Y\0Rotation Z\0Scale X\0Scale Y\0Scale Z\0");
                ImGui::Combo("Interpolation",&f->scene_interpolation,"Hold\0Linear\0Smooth\0");
                if (ImGui::Button("Set key interpolation")) command("3d.interpolation",selected,uint32_t(f->scene_channel),uint32_t(frame),f->scene_interpolation);
                if (ImGui::Button("Remove key at frame")) command("3d.key_remove",selected,uint32_t(f->scene_channel),uint32_t(frame));
                float evaluated[9]; jfx_scene3d_sample(scene,selected,f->time_seconds,evaluated);
                ImGui::Text("Evaluated position: %.2f %.2f %.2f",double(evaluated[0]),double(evaluated[1]),double(evaluated[2]));
            }
            if (animation && ImGui::CollapsingHeader("Motion path (GLM)")) {
                ImGui::InputText("Four XYZ path points",f->scene_path_points,sizeof(f->scene_path_points));
                ImGui::InputInt("Path first frame",&f->scene_path_first);
                ImGui::InputInt("Path last frame",&f->scene_path_last);
                if (ImGui::Button("Bake Catmull-Rom path") && f->scene_path_first>=0 && f->scene_path_last>f->scene_path_first)
                    command("3d.path_bake",selected,uint32_t(f->scene_path_first),uint32_t(f->scene_path_last),0,f->scene_path_points);
                ImGui::TextWrapped("Moves from point 2 to point 3; points 1 and 4 set tangents. Replaces XYZ keys in this range. Remove position scripts first.");
                if (ImGui::Button("Create low-poly animation proxy")) command("3d.poly_copy",selected,0);
                ImGui::TextWrapped("meshoptimizer reduces the mesh while preserving transform keys, scripts and cloner settings.");
            }
            if (animation && ImGui::CollapsingHeader("Rigid bodies (Bullet)")) {
                if (ImGui::InputFloat("Mass (0 = static)",&info.mass,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.mass",selected,0,0,double(info.mass));
                ImGui::InputInt("Bake frames",&f->scene_bake_frames);
                if (ImGui::Button("Bake rigid-body animation") && f->scene_bake_frames>0) command("3d.bake",0,0,uint32_t(f->scene_bake_frames));
                ImGui::TextWrapped("Convex mesh colliders; add a static plane for a floor. Bake replaces position/rotation keys of dynamic objects.");
            }
        }
        if (!animation) {
            ImGui::Separator(); ImGui::InputText("PLY path",f->scene_mesh_path,sizeof(f->scene_mesh_path));
            if (ImGui::Button("Import PLY")) command("3d.import_ply",0,0,0,0,f->scene_mesh_path);
            ImGui::SameLine(); if (ImGui::Button("Export PLY")) command("3d.export_ply",selected,0,0,0,f->scene_mesh_path);
        }
        ImGui::InputText("PNG path",f->scene_png_path,sizeof(f->scene_png_path));
        ImGui::BeginDisabled(f->scene_png_job!=nullptr);
        if (ImGui::Button("Export 3D PNG")) modeling3d_png(f,scene);
        ImGui::EndDisabled(); if (f->scene_png_job) ImGui::TextUnformatted("Exporting 3D PNG snapshot...");
        if (ImGui::Button("Export 3D video...")) ImGui::OpenPopup("3D encoded export");
        if (ImGui::BeginPopupModal("3D encoded export",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            encoded_export(f); ImGui::TextWrapped("%s",f->status);
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (animation && ImGui::CollapsingHeader("Ingagi drive (ECS / animation / cloner / shader)")) {
            ImGui::TextWrapped("Run the shared Ingagi source entry to drive this scene: ECS spawns map to scene objects, engine_scene_* writes keys/cloners/colors. See ingagi/stdlib/joltfx_3d and examples/joltfx_3d_drive.ing.");
            ImGui::InputText("Drive entry",f->drive_entry,sizeof(f->drive_entry));
            ImGui::BeginDisabled(!jfx_desktop_frontend_game_available());
            if (ImGui::Button("Run Ingagi drive")) {
                auto r=jfx_desktop_frontend_drive_3d(f,f->drive_entry[0]?f->drive_entry:"drive");
                if (r!=JFX_SUCCESS) std::snprintf(f->status,sizeof(f->status),"%s",jfx_desktop_frontend_game_error(f));
                else std::snprintf(f->status,sizeof(f->status),"Ingagi drive completed: %s",jfx_desktop_frontend_game_output(f));
            }
            ImGui::EndDisabled();
            if (!jfx_desktop_frontend_game_available()) ImGui::TextDisabled("Enable JFX_INGAGI to run Ingagi drives.");
        }
        if (animation && ImGui::CollapsingHeader("Ingagi Animation Pack (.iap)")) {
            ImGui::TextWrapped("Serializes this scene's animation event stream (keys, cloners) plus packed assets into a versioned binary. Import applies a pack's stream onto the matching scene objects in one undoable step.");
            ImGui::InputText(".iap path",f->iap_path,sizeof(f->iap_path));
            if (ImGui::Button("Export .iap")) {
                const char *assets[1]={f->scene_mesh_path[0]?f->scene_mesh_path:nullptr};
                size_t n=f->scene_mesh_path[0]?1:0;
                editor_result(f,jfx_desktop_frontend_export_iap(f,f->iap_path,n?assets:nullptr,n));
            }
            ImGui::SameLine();
            if (ImGui::Button("Import .iap")) {
                editor_result(f,jfx_desktop_frontend_edit(f,"3d.iap_import",0,0,0,0,f->iap_path));
            }
            ImGui::SameLine();
            if (ImGui::Button("Inspect .iap")) {
                uint32_t pack_fps=0,pack_frames=0,objects=0,assets=0;
                auto r=jfx_desktop_frontend_iap_info(f->iap_path,&pack_fps,&pack_frames,&objects,&assets);
                if (r==JFX_SUCCESS) std::snprintf(f->status,sizeof(f->status),".iap: %u fps %u frames %u objects %u assets",pack_fps,pack_frames,objects,assets);
                else std::snprintf(f->status,sizeof(f->status),"%s",jfx_result_to_string(r));
            }
            ImGui::TextWrapped("Asset packing uses the PLY path field above when set.");
        }
        ImGui::TextWrapped("File > Save/Open uses shared .jfx scenes with embedded meshes.");
        }
    }
    ImGui::EndChild();
    if (table) ImGui::TableNextColumn();
    // Reserve scrollbar width even when status/help text crosses a line. Its
    // appearance must not resize and invalidate the requested render target.
    if (ImGui::BeginChild("3D viewport",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        float camera[7]; jfx_scene3d_camera(scene,camera);
        const char *labels[]={"Orbit yaw","Orbit pitch","Camera distance","Target X","Target Y","Target Z","Field of view"};
        for (uint32_t i=0;i<7;++i) { ImGui::SetNextItemWidth(115);
            if (ImGui::InputFloat(labels[i],&camera[i],0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)) command("3d.camera",i,0,0,double(camera[i]));
            if (i%2==0 && i<6) ImGui::SameLine(); }
        const char *views[]={"Front","Right","Top","Back","Left","Bottom"};
        for (unsigned i=0;i<6;++i) { if (i) ImGui::SameLine(); if (ImGui::SmallButton(views[i])) command("3d.view",i); }
        ImGui::SetNextItemWidth(250);
        if (ImGui::Combo("Viewport shading",&f->scene_shading,"Rendered\0Gouraud Shading\0Wireframe Shading\0")) {
            f->preview_dirty=true; ++f->scene_revision;
        }
        ImGui::TextDisabled(rendering?"Rendered preview uses the production engine.":"Fast editing raster; production engine is used in Rendering/export.");
        const char *renderers[]={"Raster viewport","MonkeyTrace (CPU ray tracing)","Vulkan rasterizer","Filament (GPU rasterizer)","Jolt Renderer (CPU / Ingagi)","Jolt Renderer (GPU / Vulkan)"};
        int renderer=int(f->scene_render_options.renderer);
        ImGui::SetNextItemWidth(250);
        if (ImGui::BeginCombo("Rendering engine",renderers[renderer])) {
            for (int i=0;i<6;++i) {
                bool available=jfx_scene3d_renderer_available(jfx_scene3d_renderer_t(i));
                ImGui::BeginDisabled(!available);
                if (ImGui::Selectable(renderers[i],i==renderer)) {
                    auto options=f->scene_render_options; options.renderer=jfx_scene3d_renderer_t(i);
                    editor_result(f,jfx_desktop_frontend_set_scene3d_render_options(f,&options));
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if (f->scene_render_options.renderer==JFX_SCENE3D_RENDER_MONKEYTRACE || f->scene_render_options.renderer==JFX_SCENE3D_RENDER_JOLT_CPU) {
            auto options=f->scene_render_options;
            int samples=int(options.samples),bounces=int(options.bounces);
            ImGui::SetNextItemWidth(115); bool changed=ImGui::SliderInt("Ray samples",&samples,1,64);
            ImGui::SameLine(); ImGui::SetNextItemWidth(115); changed=ImGui::SliderInt("Reflections",&bounces,0,8)||changed;
            ImGui::SetNextItemWidth(200); changed=ImGui::SliderFloat("Mirror fraction",&options.reflectivity,0,1)||changed;
            if (changed) { options.samples=uint32_t(samples); options.bounces=uint32_t(bounces); editor_result(f,jfx_desktop_frontend_set_scene3d_render_options(f,&options)); }
            ImGui::TextWrapped("Ingagi ECS / MonkeyTrace BVH. Up to 4096 visible instance triangles; flat normals. Editing/navigation use the fast raster; Rendering/export use the selected quality.");
        }
        // Always occupy one line: conditional text here caused a resize/render
        // feedback loop that discarded completed snapshots and flashed black.
        ImGui::TextUnformatted(f->scene_preview_job?"Rendering scene snapshot...":"Preview ready");
        if (f->scene_render_options.renderer==JFX_SCENE3D_RENDER_FILAMENT)
            ImGui::TextWrapped("Filament: lit PBR surfaces, smooth normals, directional shadows and FXAA. Up to 4096 visible instances / 6,291,456 expanded vertices. Preview and exports share the selected renderer.");
        ImGui::TextWrapped(sculpting?"Left: sculpt | Shift: smooth | Ctrl: reverse | Alt: orbit | Middle/right: pan | Wheel: zoom | Escape: cancel stroke.":"Drag: orbit | Middle/right or Shift-drag: pan | Wheel: zoom | Alt-drag: roll. Drag the colored gimbal rings to rotate each axis. Escape cancels a drag.");
        modeling3d_viewport(f,scene);
        ImGui::Text("Viewport %ux%u%s | OSG %s | pick %.0fpx",f->scene_width,f->scene_height,
            f->scene_navigation?" (drag: half-res)":"",jfx_osg_have_osg()?"headers":"fallback",double(jfx_osg_gimbal_pick_px()));
        ImGui::TextUnformatted(f->status);
        if (desktop_export_running(f)) {
            ImGui::Text("Exported %llu frames",(unsigned long long)desktop_export_completed(f));
            if (ImGui::Button("Cancel 3D export")) desktop_export_cancel(f);
        }
    }
    ImGui::EndChild(); if (table) ImGui::EndTable();
}
