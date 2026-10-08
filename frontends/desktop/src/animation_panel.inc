// Vector stage and exposure sheet. Included inside desktop_frontend.cpp's namespace.
void animation_panel(jfx_desktop_frontend_t *frontend) {
    using namespace jfx_drawing;
    auto &e=*frontend->drawing;
    auto &io=ImGui::GetIO();
    if(e.playing) {
        e.elapsed+=(double)io.DeltaTime;
        int advance=(int)(e.elapsed*e.doc.fps);
        if(advance) { e.frame=(e.frame-1+advance)%e.doc.frames+1; e.elapsed-=(double)advance/e.doc.fps; e.selected=-1; }
    }
    bool busy=e.dragging;
    ImGui::BeginDisabled(busy);
    if(ImGui::Button(e.playing?"Pause drawing":"Play drawing")) { e.playing=!e.playing; e.elapsed=0; }
    ImGui::SameLine(); if(ImGui::Button("First frame")) { e.playing=false; e.frame=1; e.selected=-1; }
    ImGui::SameLine(); ImGui::BeginDisabled(e.past.empty()); if(ImGui::Button("Undo drawing")) e.undo(); ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(e.future.empty()); if(ImGui::Button("Redo drawing")) e.redo(); ImGui::EndDisabled();
    ImGui::SameLine(); if(ImGui::Button("Drawing files...")) ImGui::OpenPopup("Drawing files");
    if(e.files_requested) { ImGui::OpenPopup("Drawing files"); e.files_requested=false; }
    if(ImGui::BeginPopup("Drawing files")) {
        ImGui::TextWrapped("Vector drawings use .jfxdraw files, separate from NLE projects and skeletal bytecode.");
        ImGui::InputText("Drawing path",e.path,sizeof(e.path));
        if(ImGui::Button("Save drawing")) std::snprintf(frontend->status,sizeof(frontend->status),"%s",e.save(e.path)?"Drawing saved":"Could not save drawing");
        ImGui::SameLine(); if(ImGui::Button("Load drawing")) std::snprintf(frontend->status,sizeof(frontend->status),"%s",e.load(e.path)?"Drawing loaded (Undo restores previous drawing)":"Could not load drawing: invalid or unreadable file");
        ImGui::InputText("SVG path",e.svg_path,sizeof(e.svg_path));
        if(ImGui::Button("Export frame SVG")) std::snprintf(frontend->status,sizeof(frontend->status),"%s",e.export_svg(e.svg_path)?"Current frame exported as SVG":"Could not export SVG");
        ImGui::EndPopup();
    }
    ImGui::SameLine(); if(ImGui::Button("Skeleton...")) ImGui::OpenPopup("Skeletal runtime");
    if(ImGui::BeginPopup("Skeletal runtime")) {
        auto *scene=jfx_animation_tab_scene(frontend->animation_tab);
        jfx_animation_scene_info_t info{sizeof(info),0,0,0}; jfx_animation_scene_info(scene,&info);
        ImGui::Text("Bones: %u   Keys: %u",info.bone_count,info.key_count);
        ImGui::TextUnformatted("Skeletal bytecode is separate from vector drawings.");
        if(ImGui::Button("Add root bone")) {
            jfx_animation_bone_desc_t desc{sizeof(desc),"root",UINT32_MAX,{{0,0},0,{1,1}}}; uint32_t bone=0;
            if(jfx_animation_scene_add_bone(scene,&desc,&bone)!=JFX_SUCCESS)
                std::snprintf(frontend->status,sizeof(frontend->status),"Unable to add animation bone");
        }
        ImGui::SameLine(); if(ImGui::Button("Compile JFA1")) {
            uint8_t *bytes=nullptr; size_t size=0;
            if(jfx_animation_compile(scene,&bytes,&size)==JFX_SUCCESS) {
                std::snprintf(frontend->status,sizeof(frontend->status),"Compiled skeletal bytecode: %zu bytes",size);
                jfx_animation_bytes_destroy(bytes);
            } else std::snprintf(frontend->status,sizeof(frontend->status),"Skeletal bytecode compilation failed");
        }
        ImGui::EndPopup();
    }
    ImGui::Checkbox("Onion skin",&e.onion); ImGui::SameLine(); ImGui::Checkbox("Stage grid",&e.grid);
    ImGui::SameLine(); ImGui::SetNextItemWidth(130); ImGui::SliderFloat("Stage zoom",&e.zoom,0.25f,8.0f,"%.2fx");
    ImGui::SameLine(); if(ImGui::Button("Fit stage")) { e.zoom=1; e.pan={}; }
    ImGui::EndDisabled();
    float stage_height=std::max(180.0f,ImGui::GetContentRegionAvail().y-190.0f);
    ImGui::BeginChild("Drawing tools",ImVec2(160,stage_height),true);
    ImGui::BeginDisabled(busy || e.playing);
    ImGui::TextUnformatted("TOOLS");
    const char *tools[]={"Select / move (V)","Brush (B)","Line (L)","Rectangle (R)","Ellipse (O)","Eraser (E)","Hand (H)"};
    for(int i=0;i<7;++i) if(ImGui::Selectable(tools[i],e.tool==i)) {e.tool=i;e.selected=-1;}
    ImGui::Separator(); ImGui::TextUnformatted("NEW SHAPE STYLE");
    ImGui::ColorEdit4("Stroke",e.stroke,ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit4("Fill",e.fill,ImGuiColorEditFlags_NoInputs);
    ImGui::Checkbox("Fill shapes",&e.filled);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##Stroke width",&e.width,0.5f,40,"Width %.1f");
    ImGui::BeginDisabled(e.selected<0 || !e.editable());
    if(ImGui::Button("Apply style") && e.checkpoint()) {
        auto &s=e.cel().shapes[(size_t)e.selected];
        s.width=e.width; s.filled=e.filled;
        s.stroke=ImGui::ColorConvertFloat4ToU32(ImVec4(e.stroke[0],e.stroke[1],e.stroke[2],e.stroke[3]));
        s.fill=ImGui::ColorConvertFloat4ToU32(ImVec4(e.fill[0],e.fill[1],e.fill[2],e.fill[3]));
    }
    if(ImGui::Button("Delete selected") && e.checkpoint()) {e.cel().shapes.erase(e.cel().shapes.begin()+e.selected);e.selected=-1;}
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextWrapped("Drag to draw. Select to move. Wheel to zoom; Hand to pan. Escape cancels a stroke.");
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("Vector stage",ImVec2(0,stage_height),true,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 corner=ImGui::GetCursorScreenPos(),avail=ImGui::GetContentRegionAvail();
    avail.x=std::max(1.0f,avail.x); avail.y=std::max(1.0f,avail.y);
    ImGui::InvisibleButton("Drawing canvas",avail,ImGuiButtonFlags_MouseButtonLeft);
    bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    float scale=std::max(0.01f,std::min((avail.x-24)/960,(avail.y-24)/540))*e.zoom;
    ImVec2 origin(corner.x+(avail.x-960*scale)/2+e.pan.x,corner.y+(avail.y-540*scale)/2+e.pan.y);
    auto screen=[&](Point p) {return ImVec2(origin.x+p.x*scale,origin.y+p.y*scale);};
    Point mouse{(io.MousePos.x-origin.x)/scale,(io.MousePos.y-origin.y)/scale};
    bool inside=mouse.x>=0 && mouse.x<=960 && mouse.y>=0 && mouse.y<=540;
    if(hovered && !e.dragging && io.MouseWheel!=0) e.zoom=std::clamp(e.zoom*std::exp(io.MouseWheel*0.12f),0.25f,8.0f);
    if(hovered && ImGui::IsMouseClicked(0) && !e.playing && (e.tool==6 || (inside && e.editable()))) {
        e.last=mouse;
        if(e.tool==0 || e.tool==5) {
            e.selected=e.hit(mouse);
            if(e.selected>=0) {
                if(e.tool==5) {if(e.checkpoint()) {e.cel().shapes.erase(e.cel().shapes.begin()+e.selected);e.selected=-1;}}
                else e.dragging=true;
            }
        } else if(e.tool==6) e.dragging=true;
        else if(e.cel().shapes.size()<256) {
            e.pending=Shape{}; e.pending.kind=(Kind)(e.tool-1); e.pending.width=e.width;
            e.pending.stroke=ImGui::ColorConvertFloat4ToU32(ImVec4(e.stroke[0],e.stroke[1],e.stroke[2],e.stroke[3]));
            e.pending.fill=ImGui::ColorConvertFloat4ToU32(ImVec4(e.fill[0],e.fill[1],e.fill[2],e.fill[3]));
            e.pending.filled=e.filled; e.begin_shape(mouse);
        } else std::snprintf(frontend->status,sizeof(frontend->status),"Drawing limit: 256 shapes per cel");
    }
    // Selection movement is previewed through pending delta and committed once
    // on release. Every drag therefore occupies exactly one history entry.
    if(e.dragging && active && ImGui::IsMouseDown(0)) {
        if(e.tool==6) {e.pan.x+=io.MouseDelta.x;e.pan.y+=io.MouseDelta.y;}
        else if(e.tool>0 && e.tool<5) {
            Point p{std::clamp(mouse.x,0.0f,960.0f),std::clamp(mouse.y,0.0f,540.0f)};
            if(e.tool==1) {
                auto back=e.pending.points.back();
                if(std::hypot(back.x-p.x,back.y-p.y)>1/scale && e.pending.points.size()<2048) e.pending.points.push_back(p);
            } else e.pending.points.back()=p;
        }
    }
    bool cancel=ImGui::IsKeyPressed(ImGuiKey_Escape);
    if(e.dragging && (ImGui::IsMouseReleased(0) || cancel)) {
        if(!cancel && e.tool==0 && e.selected>=0) {
            Point delta{mouse.x-e.last.x,mouse.y-e.last.y};
            if(std::hypot(delta.x,delta.y)>0.01f && e.checkpoint()) { for(auto &p:e.cel().shapes[(size_t)e.selected].points) {p.x+=delta.x;p.y+=delta.y;} }
        } else if(!cancel && e.tool>0 && e.tool<5) {
            if(e.tool==1 || std::hypot(e.pending.points.back().x-e.pending.points.front().x,e.pending.points.back().y-e.pending.points.front().y)>1) {
                e.commit_shape();
            }
        }
        e.dragging=false;
    }
    auto *draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(corner,ImVec2(corner.x+avail.x,corner.y+avail.y),IM_COL32(32,36,44,255));
    ImVec2 end=screen({960,540});
    draw->AddRectFilled(origin,end,IM_COL32(250,249,246,255));
    draw->PushClipRect(ImVec2(std::max(corner.x,origin.x),std::max(corner.y,origin.y)),ImVec2(std::min(corner.x+avail.x,end.x),std::min(corner.y+avail.y,end.y)),true);
    if(e.grid) {
        for(int x=0;x<=960;x+=40) draw->AddLine(screen({(float)x,0}),screen({(float)x,540}),IM_COL32(215,219,223,255));
        for(int y=0;y<=540;y+=40) draw->AddLine(screen({0,(float)y}),screen({960,(float)y}),IM_COL32(215,219,223,255));
    }
    auto shape=[&](const Shape &s,ImU32 tint,Point offset) {
        auto pos=[&](Point p) {return screen({p.x+offset.x,p.y+offset.y});};
        ImU32 stroke=tint?tint:s.stroke,fill=tint?tint:s.fill;
        float width=std::max(0.5f,s.width*scale);
        if(s.kind==Kind::Rectangle || s.kind==Kind::Ellipse) {
            auto a=pos(s.points.front()),b=pos(s.points.back());
            ImVec2 lo(std::min(a.x,b.x),std::min(a.y,b.y)),hi(std::max(a.x,b.x),std::max(a.y,b.y));
            if(s.kind==Kind::Rectangle) {if(s.filled) draw->AddRectFilled(lo,hi,fill); draw->AddRect(lo,hi,stroke,0,0,width);}
            else {
                ImVec2 center((lo.x+hi.x)/2,(lo.y+hi.y)/2),radius(std::max(0.1f,(hi.x-lo.x)/2),std::max(0.1f,(hi.y-lo.y)/2));
                if(s.filled) draw->AddEllipseFilled(center,radius,fill);
                draw->AddEllipse(center,radius,stroke,0,0,width);
            }
        } else {
            for(size_t i=1;i<s.points.size();++i) draw->AddLine(pos(s.points[i-1]),pos(s.points[i]),stroke,width);
            for(auto p:s.points) draw->AddCircleFilled(pos(p),width/2,stroke,8);
        }
    };
    for(size_t li=0;li<e.doc.layers.size();++li) {
        auto &l=e.doc.layers[li]; if(!l.visible) continue;
        const auto &current=e.exposure(l,e.frame);
        if(e.onion && !e.playing) {
            const Cel *previous=nullptr,*next=nullptr;
            for(const auto &c:l.cels) {if(c.frame<current.frame) previous=&c; if(c.frame>current.frame) {next=&c;break;}}
            if(previous) for(const auto &s:previous->shapes) shape(s,IM_COL32(224,88,102,65),{});
            if(next) for(const auto &s:next->shapes) shape(s,IM_COL32(55,130,230,65),{});
        }
        for(size_t i=0;i<current.shapes.size();++i) {
            const auto &s=current.shapes[i]; Point offset{};
            bool selected=(int)li==e.layer && (int)i==e.selected;
            if(selected && e.dragging && e.tool==0) offset={mouse.x-e.last.x,mouse.y-e.last.y};
            shape(s,0,offset);
            if(selected) {
                Point lo=s.points.front(),hi=lo;
                for(auto p:s.points) {lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
                draw->AddRect(screen({lo.x+offset.x-3,lo.y+offset.y-3}),screen({hi.x+offset.x+3,hi.y+offset.y+3}),IM_COL32(35,145,240,255));
            }
        }
    }
    if(e.dragging && e.tool>0 && e.tool<5) shape(e.pending,0,{});
    draw->PopClipRect();
    draw->AddRect(origin,end,IM_COL32(140,148,160,255));
    ImGui::EndChild();
    ImGui::BeginDisabled(e.dragging);
    ImGui::Text("EXPOSURE SHEET   Frame %d / %d   |   960 x 540",e.frame,e.doc.frames);
    ImGui::SameLine(); ImGui::SetNextItemWidth(85);
    int fps=e.doc.fps; if(ImGui::SliderInt("FPS",&fps,1,60) && e.checkpoint()) e.doc.fps=fps;
    ImGui::SameLine(); ImGui::BeginDisabled(!e.editable());
    if(ImGui::Button("Duplicate key")) e.key(false);
    ImGui::SameLine(); if(ImGui::Button("Blank key")) e.key(true);
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(e.doc.layers.size()>=8 || e.playing);
    if(ImGui::Button("Add layer")) e.add_layer();
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-1);
    if(ImGui::SliderInt("##Drawing frame",&e.frame,1,e.doc.frames,"Frame %d")) {e.playing=false;e.selected=-1;}
    ImGui::BeginChild("Exposure rows",ImVec2(0,0),true,ImGuiWindowFlags_HorizontalScrollbar);
    for(size_t li=0;li<e.doc.layers.size();++li) {
        auto &l=e.doc.layers[li]; ImGui::PushID((int)li);
        bool visible=l.visible,locked=l.locked;
        if(ImGui::Checkbox("##visible",&visible) && e.checkpoint()) {l.visible=visible;e.selected=-1;}
        ImGui::SameLine(); if(ImGui::Checkbox("Lock",&locked) && e.checkpoint()) {l.locked=locked;e.selected=-1;}
        ImGui::SameLine(); char label[32]; std::snprintf(label,sizeof(label),"Layer %zu",li+1);
        if(ImGui::Selectable(label,(int)li==e.layer,0,ImVec2(80,20))) {e.layer=(int)li;e.selected=-1;}
        for(int frame=1;frame<=e.doc.frames;++frame) {
            ImGui::SameLine(0,1); ImGui::PushID(frame);
            const auto &c=e.exposure(l,frame); bool key=c.frame==frame;
            char cell[20]; std::snprintf(cell,sizeof(cell),"%s##cel",key?(c.shapes.empty()?"o":"*"):"-");
            bool current_frame=frame==e.frame;
            if(current_frame) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.15f,0.48f,0.72f,1));
            if(ImGui::Button(cell,ImVec2(19,20))) {e.frame=frame;e.layer=(int)li;e.selected=-1;e.playing=false;}
            if(current_frame) ImGui::PopStyleColor();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Frame %d: %s",frame,key?(c.shapes.empty()?"blank key":"drawing key"):"held drawing");
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild(); ImGui::EndDisabled();
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !e.dragging) {
        if(io.KeyCtrl || io.KeySuper) {
            if(ImGui::IsKeyPressed(ImGuiKey_Z)) {if(io.KeyShift)e.redo();else e.undo();}
            if(ImGui::IsKeyPressed(ImGuiKey_S)) std::snprintf(frontend->status,sizeof(frontend->status),"%s",e.save(e.path)?"Drawing saved":"Could not save drawing");
        } else if(!ImGui::IsAnyItemActive()) {
            const ImGuiKey keys[]={ImGuiKey_V,ImGuiKey_B,ImGuiKey_L,ImGuiKey_R,ImGuiKey_O,ImGuiKey_E,ImGuiKey_H};
            for(int i=0;i<7;++i) if(ImGui::IsKeyPressed(keys[i])) {e.tool=i;e.selected=-1;}
            if(ImGui::IsKeyPressed(ImGuiKey_Delete) && e.selected>=0 && e.editable() && e.checkpoint()) {e.cel().shapes.erase(e.cel().shapes.begin()+e.selected);e.selected=-1;}
            if(ImGui::IsKeyPressed(ImGuiKey_F6)) e.key(false);
            if(ImGui::IsKeyPressed(ImGuiKey_F7)) e.key(true);
            if(ImGui::IsKeyPressed(ImGuiKey_Space)) {e.playing=!e.playing;e.elapsed=0;}
        }
    }
    if(e.allocation_failed) {
        std::snprintf(frontend->status,sizeof(frontend->status),"Drawing edit failed: out of memory");
        e.allocation_failed=false;
    }
}
