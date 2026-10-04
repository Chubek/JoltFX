/* Resolve-inspired controls over the descriptor-backed effect parameters.
 * Widgets only produce values; the shared editor owns validation and history. */
void finish_grading(jfx_desktop_frontend_t *f) {
    if (f->grading_edit) {
        editor_result(f,jfx_editor_commit_edit(f->editor));
        f->grading_edit=false;
    }
}
bool begin_grading(jfx_desktop_frontend_t *f) {
    if (f->grading_edit) return true;
    auto r=jfx_editor_begin_edit(f->editor,JFX_PROJECT_KIND_SEQUENCE);
    if (r!=JFX_SUCCESS) { editor_result(f,r); return false; }
    f->grading_edit=true; return true;
}
bool grading_dial(const jfx_param_desc_t &desc,float *value,float width=120) {
    const float unit=ImGui::GetFontSize()/13.0f, radius=26*unit;
    if (!std::isfinite(*value)) *value=desc.default_value;
    auto origin=ImGui::GetCursorScreenPos();
    ImVec2 center(origin.x+width*.5f,origin.y+radius+7*unit);
    ImGui::InvisibleButton("Dial",ImVec2(width,2*radius+14*unit),ImGuiButtonFlags_EnableNav);
    bool changed=false;
    const auto &io=ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0) && !ImGui::IsItemActivated()) {
        float delta=io.MouseDelta.x-io.MouseDelta.y;
        if (delta!=0) { *value+=delta*(desc.maximum-desc.minimum)*(io.KeyShift?.0002f:.002f); changed=true; }
    }
    if (ImGui::IsItemFocused()) {
        int direction=(ImGui::IsKeyPressed(ImGuiKey_RightArrow)?1:0)-(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)?1:0);
        if (direction) { *value+=(float)direction*(desc.maximum-desc.minimum)*(io.KeyShift?.001f:.01f); changed=true; }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { *value=desc.default_value; changed=true; }
    *value=clampf(*value,desc.minimum,desc.maximum);
    auto *draw=ImGui::GetWindowDrawList();
    const float pi=3.141592654f, start=.75f*pi, end=2.25f*pi;
    float fraction=desc.maximum>desc.minimum?(*value-desc.minimum)/(desc.maximum-desc.minimum):0;
    float angle=start+(end-start)*fraction;
    draw->PathArcTo(center,radius+4*unit,start,end,48); draw->PathStroke(IM_COL32(64,73,88,255),0,3*unit);
    if (fraction>0) { draw->PathArcTo(center,radius+4*unit,start,angle,48); draw->PathStroke(IM_COL32(88,187,213,255),0,3*unit); }
    draw->AddCircleFilled(center,radius,ImGui::IsItemActive()?IM_COL32(61,72,87,255):IM_COL32(40,48,62,255),48);
    draw->AddLine(ImVec2(center.x+std::cos(angle)*radius*.5f,center.y+std::sin(angle)*radius*.5f),
        ImVec2(center.x+std::cos(angle)*radius*.85f,center.y+std::sin(angle)*radius*.85f),IM_COL32_WHITE,2*unit);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: drag right/up; Shift for fine control.\nDouble-click to reset. Arrow keys adjust; type below.",desc.label);
    ImGui::TextUnformatted(desc.label);
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputFloat("##Value",value,0,0,"%.4f")) {
        *value=std::isfinite(*value)?clampf(*value,desc.minimum,desc.maximum):desc.default_value;
        changed=true;
    }
    return changed;
}

bool grading_wheel(const char *label,const jfx_param_desc_t *params,float rgb[3],float width) {
    const float unit=ImGui::GetFontSize()/13.0f;
    const float radius=std::fmax(8*unit,std::fmin(65*unit,(width-16)*.5f)), pi=3.141592654f;
    ImGui::TextUnformatted(label);
    auto origin=ImGui::GetCursorScreenPos();
    ImVec2 center(origin.x+width*.5f,origin.y+radius+8*unit);
    ImGui::InvisibleButton("Wheel",ImVec2(width,2*radius+16*unit),ImGuiButtonFlags_EnableNav);
    float master=(rgb[0]+rgb[1]+rgb[2])/3;
    float x=rgb[0]-master,y=(rgb[1]-rgb[2])/1.732050808f;
    bool changed=false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        auto mouse=ImGui::GetIO().MousePos;
        x=(mouse.x-center.x)/radius; y=-(mouse.y-center.y)/radius;
        float length=std::sqrt(x*x+y*y);
        if (length>1) { x/=length; y/=length; }
        changed=true;
    }
    if (ImGui::IsItemFocused()) {
        float step=ImGui::GetIO().KeyShift?.002f:.02f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { x-=step; changed=true; }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { x+=step; changed=true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { y+=step; changed=true; }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { y-=step; changed=true; }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        for (int c=0;c<3;++c) rgb[c]=params[c].default_value;
        master=(rgb[0]+rgb[1]+rgb[2])/3; x=y=0; changed=true;
    }
    if (changed) {
        const float offsets[]={x,-.5f*x+.866025404f*y,-.5f*x-.866025404f*y};
        for (int c=0;c<3;++c) rgb[c]=clampf(master+offsets[c],params[c].minimum,params[c].maximum);
    }
    auto *draw=ImGui::GetWindowDrawList();
    const ImU32 neutral=IM_COL32(70,73,80,255);
    for (int i=0;i<96;++i) {
        float a=2*pi*(float)i/96,b=2*pi*(float)(i+1)/96;
        float r,g,blue,r2,g2,b2;
        ImGui::ColorConvertHSVtoRGB((float)i/96, .85f,.9f,r,g,blue);
        ImGui::ColorConvertHSVtoRGB((float)(i+1)/96,.85f,.9f,r2,g2,b2);
        /* Red at right, green at upper-left, blue at lower-left. */
        draw->PrimReserve(3,3); auto idx=draw->_VtxCurrentIdx;
        draw->PrimWriteIdx((ImDrawIdx)idx); draw->PrimWriteIdx((ImDrawIdx)(idx+1)); draw->PrimWriteIdx((ImDrawIdx)(idx+2));
        ImVec2 uv=ImGui::GetFontTexUvWhitePixel();
        draw->PrimWriteVtx(center,uv,neutral);
        draw->PrimWriteVtx(ImVec2(center.x+radius*std::cos(a),center.y-radius*std::sin(a)),uv,ImGui::ColorConvertFloat4ToU32(ImVec4(r,g,blue,1)));
        draw->PrimWriteVtx(ImVec2(center.x+radius*std::cos(b),center.y-radius*std::sin(b)),uv,ImGui::ColorConvertFloat4ToU32(ImVec4(r2,g2,b2,1)));
    }
    draw->AddCircle(center,radius+2*unit,IM_COL32(115,127,145,255),96,2*unit);
    float length=std::sqrt(x*x+y*y), scale=length>1?1/length:1;
    ImVec2 puck(center.x+x*radius*scale,center.y-y*radius*scale);
    draw->AddCircleFilled(puck,5*unit,IM_COL32(18,22,29,255));
    draw->AddCircle(puck,5*unit,IM_COL32_WHITE,16,2*unit);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: drag the puck toward a color.\nDouble-click to reset; arrow keys adjust.",label);
    jfx_param_desc_t luminance=params[0]; luminance.label="Master"; luminance.default_value=(params[0].default_value+params[1].default_value+params[2].default_value)/3;
    float next=master;
    if (grading_dial(luminance,&next,width)) {
        for (int c=0;c<3;++c) rgb[c]=clampf(rgb[c]+next-master,params[c].minimum,params[c].maximum);
        changed=true;
    }
    if (ImGui::SmallButton("Reset wheel")) {
        for (int c=0;c<3;++c) rgb[c]=params[c].default_value;
        changed=true;
    }
    return changed;
}
