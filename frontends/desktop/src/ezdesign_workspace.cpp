#include "ezdesign_workspace.h"
#include "modeling3d_preview.h"
#include "jfx/jfx_scheduler.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace jfx::ez {
struct RasterJob {
    tilly::vector<jfx::detail::EditingTriangle> triangles;
    tilly::vector<uint8_t> pixels;
    uint64_t key=0;
    uint64_t revision=0,generation=0;
    Id scope=1;
    bool preview=false;
    unsigned width=0,height=0;
    float quaternion[4]{},distance=12,orthographic_height=0,background[3]{};
    bool antialias=true;
    std::atomic<bool> done{false};
    jfx_result_t result=JFX_SUCCESS;
};
namespace {
void render(void *user) {
    auto &j=*static_cast<RasterJob *>(user);
    j.result=jfx::detail::render_editing_triangles(j.triangles.data(),j.triangles.size(),j.quaternion,j.distance,j.orthographic_height,
        j.width,j.height,j.background,j.antialias,j.pixels.data(),j.pixels.size());
    j.done.store(true,std::memory_order_release);
}
bool same(const View &a,const View &b){return a.target==b.target && a.yaw==b.yaw && a.pitch==b.pitch && a.distance==b.distance && a.orthographic==b.orthographic;}
ImVec2 screen(const Camera &camera,Vec p,float x,float y){glm::dvec2 q;camera.project(p,q);return {x+float(q.x),y+float(q.y)};}
bool is_selected(const Controller &e,const Triangle &t){return (e.selected.kind==Kind::Face && e.selected.id==t.face && e.selected.scope==t.scope) ||
    (!t.path.empty() && t.path.front()==e.selected.id && (e.selected.kind==Kind::Group || e.selected.kind==Kind::Component));}
ImU32 color(glm::dvec3 c){return ImGui::ColorConvertFloat4ToU32({float(c.x),float(c.y),float(c.z),1});}
uint32_t visible_material(const Triangle &t,const Camera &camera){
    auto eye=camera.view.target+camera.orientation()*Vec(0,0,camera.view.distance);
    return glm::dot(glm::cross(t.points[1]-t.points[0],t.points[2]-t.points[0]),eye-t.points[0])>=0?t.material:t.back_material;
}
void unit_text(char *out,size_t size,double metres,unsigned units){std::snprintf(out,size,"%.6g %s",units==1?metres*1000:units==2?metres/.3048:metres,units==1?"mm":units==2?"ft":"m");}
} // namespace
Workspace::~Workspace(){if(job)scheduler_wait_idle();tilly::destroy(job);tilly::destroy(display);}
bool Workspace::busy() const {return job && !job->done.load(std::memory_order_acquire);}
void Workspace::deactivate(){tools.cancel();gizmo_active=false;material_revision=0;texture=nullptr;ImGuizmo::Enable(false);}
bool Workspace::load(const char *path) noexcept {
    if(!path || std::strlen(path)>=sizeof(tools.path) || !std::strrchr(path,'.') || std::strcmp(std::strrchr(path,'.'),".jfx")){
        std::snprintf(tools.message,sizeof(tools.message),"EZDesign projects require a .jfx path (maximum 511 bytes).");return false;
    }
    char owned[sizeof(tools.path)];std::memcpy(owned,path,std::strlen(path)+1);
    if(!tools.editor.load(owned)){std::snprintf(tools.message,sizeof(tools.message),"%s",tools.editor.error);return false;}
    deactivate();tools.active_scope=1;tools.context.clear();tools.selected={};tools.camera.view=tools.editor.doc.view;tools.cached_revision=0;
    std::memcpy(tools.path,owned,std::strlen(owned)+1);std::snprintf(tools.message,sizeof(tools.message),"EZDesign project reopened.");return true;
}
bool Workspace::save(const char *path) noexcept {
    if(!path || std::strlen(path)>=sizeof(tools.path) || !std::strrchr(path,'.') || std::strcmp(std::strrchr(path,'.'),".jfx")){
        std::snprintf(tools.message,sizeof(tools.message),"EZDesign projects require a .jfx path (maximum 511 bytes).");return false;
    }
    char owned[sizeof(tools.path)];std::memcpy(owned,path,std::strlen(path)+1);
    deactivate();tools.editor.doc.view=tools.camera.view;bool ok=tools.editor.save(owned);
    if(ok)std::memcpy(tools.path,owned,std::strlen(owned)+1);
    std::snprintf(tools.message,sizeof(tools.message),"%s",ok?"EZDesign project saved atomically.":tools.editor.error);return ok;
}
void Workspace::raster(jfx_desktop_window_t *window,const float background[3],bool interacting) {
    if(!window)return;
    auto &e=tools;
    unsigned width=unsigned(std::max(1.0,std::min(e.camera.width,1024.0))),height=unsigned(std::max(1.0,std::min(e.camera.height,1024.0)));
    double factor=std::min(1.0,1024/std::max(e.camera.width,e.camera.height));if(interacting)factor*=.5;
    width=unsigned(std::max(1.0,std::round(e.camera.width*factor)));height=unsigned(std::max(1.0,std::round(e.camera.height*factor)));
    bool preview=e.has_geometry_preview();
    uint64_t fingerprint=1469598103934665603ull;
    if(preview){auto add=[&](const void *ptr,size_t n){auto *p=static_cast<const unsigned char *>(ptr);for(size_t i=0;i<n;++i){fingerprint^=p[i];fingerprint*=1099511628211ull;}};
        for(auto &t:e.preview_mesh.triangles)for(Vec p:t.points)add(&p,sizeof(p));}
    if(!same(previous_view,e.camera.view) || previous_revision!=e.editor.revision || previous_preview!=preview || width!=render_width || height!=render_height ||
       !std::equal(background,background+3,last_background) || (preview && fingerprint!=previous_preview_hash) || previous_selection!=e.selected.id || previous_scope!=e.active_scope){
        ++desired_key;previous_view=e.camera.view;previous_revision=e.editor.revision;previous_preview=preview;render_width=width;render_height=height;std::copy(background,background+3,last_background);
        previous_preview_hash=fingerprint;previous_selection=e.selected.id;
        previous_scope=e.active_scope;
    }
    if(job && job->done.load(std::memory_order_acquire)){
        // Motion may display a completed camera/gesture sample while queuing
        // the newest one. Never publish another document, scope or cancelled
        // gesture; strict idle publication restores the final full-size frame.
        bool applies=job->revision==e.editor.revision && job->scope==e.active_scope && job->preview==preview &&
            (!preview || job->generation==e.preview_generation);
        if(job->result==JFX_SUCCESS && applies && (job->key==desired_key || interacting)){
            tilly::destroy(display);display=job;job=nullptr;texture=nullptr;render_key=display->key;
        }
        else {tilly::destroy(job);job=nullptr;}
    }
    if(!job && render_key!=desired_key){
        auto *next=tilly::create<RasterJob>();if(!next){std::snprintf(e.message,sizeof(e.message),"Viewport allocation failed.");return;}
        try {
            next->key=desired_key;next->width=width;next->height=height;next->antialias=!interacting;std::copy(background,background+3,next->background);
            next->revision=e.editor.revision;next->scope=e.active_scope;next->generation=e.preview_generation;next->preview=preview;
            auto q=e.camera.orientation();next->quaternion[0]=float(q.x);next->quaternion[1]=float(q.y);next->quaternion[2]=float(q.z);next->quaternion[3]=float(q.w);
            next->distance=float(e.camera.view.distance);next->orthographic_height=e.camera.view.orthographic?float(e.camera.view.distance*2*std::tan(glm::radians(22.5))):0;
            auto &mesh=preview?e.preview_mesh:e.index.mesh;next->triangles.reserve(mesh.triangles.size());
            for(auto &t:mesh.triangles){jfx::detail::EditingTriangle tri;
                auto c=e.editor.doc.materials[visible_material(t,e.camera)].color;if(is_selected(e,t))c={.95,.67,.22};
                for(size_t i=0;i<3;++i){auto p=t.points[i]-e.camera.view.target;tri.positions[i]={float(p.x),float(p.y),float(p.z)};tri.color[i]=float(c[int(i)]);}next->triangles.push_back(tri);
            }
            next->pixels.resize(size_t(width)*height*4);
            if(scheduler_submit(render,next,0)){job=next;next=nullptr;}
        }catch(...){std::snprintf(e.message,sizeof(e.message),"Viewport allocation failed; last valid image retained.");}
        tilly::destroy(next);
    }
    if(display && !texture)texture=jfx_desktop_window_upload_rgba8(window,display->width,display->height,display->pixels.data());
}
void Workspace::overlays(float x,float y,float width,float height) {
    auto &e=tools;auto *draw=ImGui::GetWindowDrawList();draw->PushClipRect({x,y},{x+width,y+height},true);
    auto line=[&](Vec a,Vec b,ImU32 c,float thickness=1){glm::dvec2 aa,bb;if(e.camera.project(a,aa) && e.camera.project(b,bb))draw->AddLine({x+float(aa.x),y+float(aa.y)},{x+float(bb.x),y+float(bb.y)},c,thickness);};
    // Guides are overlay-only: never contribute vertices or face closure.
    double extent=std::max(1.0,e.camera.view.distance);
    line({-extent,0,0},{extent,0,0},IM_COL32(155,65,65,180));line({0,-extent,0},{0,extent,0},IM_COL32(65,170,85,180));line({0,0,-extent},{0,0,extent},IM_COL32(65,110,190,180));
    // Authoritative editable edges, including nested instances. No earcut diagonals.
    auto edges=[&](auto &&self,Id id,const glm::dmat4 &matrix,unsigned depth)->void {
        if(depth>31)return;
        auto &s=scope(e.has_geometry_preview()?e.candidate:e.editor.doc,id);
        for(auto &edge:s.edges){Vec a=Vec(matrix*glm::dvec4(vertex(s,edge.a).position,1)),b=Vec(matrix*glm::dvec4(vertex(s,edge.b).position,1));
            if(!e.wireframe && !e.has_geometry_preview()){glm::dvec2 pixel;if(!e.camera.project((a+b)*.5,pixel))continue;auto ray=e.camera.ray(pixel);auto h=e.index.intersect(ray);
                if(h.entity.kind!=Kind::None && glm::dot((a+b)*.5-ray.origin,ray.direction)>h.depth+Tolerances::plane*4)continue;}
            line(a,b,edge.id==e.selected.id?IM_COL32(255,190,70,255):IM_COL32(200,210,225,255),edge.id==e.selected.id?3.f:1.3f);
        }
        for(auto &i:s.instances)self(self,i.definition,matrix*i.transform,depth+1);
    };edges(edges,e.active_scope,glm::dmat4(1),0);
    if(e.selected.kind==Kind::Vertex){auto p=vertex(scope(e.has_geometry_preview()?e.candidate:e.editor.doc,e.active_scope),e.selected.id).position;draw->AddCircleFilled(screen(e.camera,p,x,y),5,IM_COL32(255,190,70,255));}
    if(e.started && e.pending.points.size()>1 && e.tool!=Tool::Pull && e.tool!=Tool::Offset){
        auto a=e.pending.points[0],b=e.pending.points[1];ImU32 c=e.preview_valid?IM_COL32(100,235,170,255):IM_COL32(250,100,90,255);
        if(e.tool==Tool::Rectangle || e.tool==Tool::Opening){Plane p=e.construction;if(e.tool==Tool::Opening)p=plane(scope(e.editor.doc,e.active_scope),face(scope(e.editor.doc,e.active_scope),e.pending.entity));
            auto aa=p.project(a),bb=p.project(b);Vec ab=p.point({bb.x,aa.y}),ba=p.point({aa.x,bb.y});line(a,ab,c,2);line(ab,b,c,2);line(b,ba,c,2);line(ba,a,c,2);
        }else line(a,b,c,2);
    }
    if(e.snap.valid){glm::dvec2 q;if(e.camera.project(e.snap.position,q)){
        ImVec2 p{x+float(q.x),y+float(q.y)};draw->AddCircle(p,6,IM_COL32(100,240,175,255),12,2);draw->AddText({p.x+10,p.y-18},IM_COL32(170,255,215,255),e.snap.hint());
    }}
    for(auto &dim:e.editor.doc.dimensions){
        if(dim.a.scope!=e.active_scope || dim.b.scope!=e.active_scope)continue;
        Vec a,b;bool resolved=resolve(e.editor.doc,dim.a,a) && resolve(e.editor.doc,dim.b,b);
        if(!resolved)continue;
        glm::dvec2 aa,bb;if(!e.camera.project(a,aa)||!e.camera.project(b,bb))continue;
        glm::dvec2 delta=bb-aa;double n=glm::length(delta);glm::dvec2 shift=n>1?glm::dvec2(-delta.y,delta.x)*18.0/n:glm::dvec2(0,18);
        ImVec2 p{x+float(aa.x+shift.x),y+float(aa.y+shift.y)},q{x+float(bb.x+shift.x),y+float(bb.y+shift.y)};
        draw->AddLine(p,q,IM_COL32(250,225,150,255));draw->AddLine(screen(e.camera,a,x,y),p,IM_COL32(220,210,170,255));draw->AddLine(screen(e.camera,b,x,y),q,IM_COL32(220,210,170,255));
        char text[64];unit_text(text,sizeof(text),glm::length(b-a),e.editor.doc.units);ImVec2 pos{(p.x+q.x)*.5f,(p.y+q.y)*.5f};auto size=ImGui::CalcTextSize(text);
        draw->AddRectFilled({pos.x-3,pos.y-2},{pos.x+size.x+3,pos.y+size.y+2},IM_COL32(20,25,30,235));draw->AddText(pos,IM_COL32(255,240,190,255),text);
    }
    draw->PopClipRect();
}
bool Workspace::gizmo(float x,float y,float width,float height) {
    auto &e=tools;if(e.tool!=Tool::Select || e.selected.kind==Kind::None)return false;
    auto &s=scope(e.editor.doc,e.active_scope);const Instance *instance=nullptr;for(auto &i:s.instances)if(i.id==e.selected.id)instance=&i;
    if(!gizmo_active){
        glm::dmat4 matrix(1);
        if(instance)matrix=instance->transform;
        else {
            auto ids=entity_vertices(s,e.selected.id);Vec origin=vertex(s,ids.front()).position,offset(0);
            for(Id id:ids)offset+=vertex(s,id).position-origin;
            gizmo_pivot=origin+offset/double(ids.size());matrix[3]=glm::dvec4(gizmo_pivot,1);
        }
        matrix[3]-=glm::dvec4(e.camera.view.target,0);gizmo_matrix=glm::mat4(matrix);gizmo_origin=glm::vec3(gizmo_matrix[3]);
    }
    auto view=glm::mat4(e.camera.view_matrix()),projection=glm::mat4(e.camera.projection());
    ImGuizmo::Enable(true);ImGuizmo::SetOrthographic(e.camera.view.orthographic);ImGuizmo::SetDrawlist();ImGuizmo::SetRect(x,y,width,height);
    auto operation=gizmo_operation==0?ImGuizmo::TRANSLATE:gizmo_operation==1?ImGuizmo::ROTATE:ImGuizmo::SCALE;
    float snap[]={gizmo_operation==1?15.f:gizmo_operation==2?.1f:.1f, .1f,.1f};
    bool changed=ImGuizmo::Manipulate(glm::value_ptr(view),glm::value_ptr(projection),operation,local?ImGuizmo::LOCAL:ImGuizmo::WORLD,glm::value_ptr(gizmo_matrix),nullptr,ImGui::GetIO().KeyCtrl?snap:nullptr);
    bool using_handle=ImGuizmo::IsUsing(),over=ImGuizmo::IsOver(operation);
    if(using_handle)gizmo_active=true;
    if(changed && gizmo_active){
        e.pending=Command{};e.pending.op=instance?Op::Transform:Op::TransformGeometry;e.pending.scope=e.active_scope;e.pending.entity=e.selected.id;
        e.pending.transform=glm::dmat4(gizmo_matrix);
        if(instance)e.pending.transform[3]+=glm::dvec4(e.camera.view.target,0);
        else {e.pending.pivot=gizmo_pivot;e.pending.transform[3]=glm::dvec4(Vec(glm::vec3(gizmo_matrix[3])-gizmo_origin),1);}
        e.preview_valid=e.editor.preview(e.pending,e.candidate);if(e.preview_valid)e.preview_mesh=triangulate(e.candidate,e.active_scope);
        else std::snprintf(e.message,sizeof(e.message),"%s",e.editor.error);
    }
    if(gizmo_active && (ImGui::IsKeyPressed(ImGuiKey_Escape)||!ImGui::GetIO().MouseDown[0])){
        if(!ImGui::IsKeyPressed(ImGuiKey_Escape) && e.preview_valid)e.action(e.pending);
        gizmo_active=false;e.cancel();ImGuizmo::Enable(false);
    }return using_handle || over;
}
void Workspace::draw(jfx_desktop_window_t *window,const float background[3]) {
    auto &e=tools;try{e.refresh();}catch(const std::exception &error){ImGui::TextWrapped("EZDesign: %s",error.what());return;}
    bool gesture_at_frame_start=e.started || gizmo_active;
    auto safe=[&](auto &&fn){try{fn();}catch(const std::exception &error){e.cancel();std::snprintf(e.message,sizeof(e.message),"%s",error.what());}};
    auto command=[&](Op op){Command c;c.op=op;c.scope=e.active_scope;c.entity=e.selected.id;return c;};
    if(ImGui::Button("Undo EZDesign"))safe([&]{deactivate();e.history(false);});
    ImGui::SameLine();
    if(ImGui::Button("Redo EZDesign"))safe([&]{deactivate();e.history(true);});
    ImGui::SameLine();
    if(ImGui::Button("EZDesign files..."))e.files_requested=true;
    ImGui::SameLine();
    if(ImGui::Button("Fit model"))safe([&]{e.camera.fit(e.editor.doc,e.active_scope);});
    ImGui::SameLine();
    if(ImGui::Button("Fit selection"))safe([&]{
        if(e.selected.kind==Kind::Face){auto &s=scope(e.editor.doc,e.selected.scope);auto &f=face(s,e.selected.id);Vec low(1e100),high(-1e100);for(Id v:f.outer){auto p=vertex(s,v).position;low=glm::min(low,p);high=glm::max(high,p);}e.camera.view.target=(low+high)*.5;e.camera.view.distance=std::max(.01,glm::length(high-low)*1.5);}
        else { e.camera.fit(e.editor.doc,e.active_scope); }
    });
    ImGui::SameLine();ImGui::Checkbox("Orthographic",&e.camera.view.orthographic);
    if(e.files_requested){deactivate();ImGui::OpenPopup("EZDesign files");e.files_requested=false;}
    if(ImGui::BeginPopup("EZDesign files")){
        ImGui::TextWrapped("Native .jfx EZDesign projects retain editable topology and components. OBJ is a flattened mesh, not an editable project.");
        ImGui::InputText("Project path",e.path,sizeof(e.path));
        if(ImGui::Button("Save EZDesign"))safe([&]{save(e.path);});
        ImGui::SameLine();
        if(ImGui::Button("Open EZDesign"))safe([&]{load(e.path);});
        ImGui::InputText("OBJ path",e.export_path,sizeof(e.export_path));
        if(ImGui::Button("Export OBJ"))safe([&]{bool ok=e.editor.export_obj(e.export_path);std::snprintf(e.message,sizeof(e.message),"%s",ok?"Root model exported as OBJ (metres).":e.editor.error);});
        ImGui::InputText("PLY path",ply_path,sizeof(ply_path));
        if(ImGui::Button("Export PLY"))safe([&]{bool ok=e.editor.export_ply(ply_path);std::snprintf(e.message,sizeof(e.message),"%s",ok?"Root mesh exported as PLY; reopen in the 3D Design PLY importer.":e.editor.error);});
        if(ImGui::Button("New blank EZDesign"))safe([&]{e.cancel();e.action(command(Op::New));e.active_scope=1;e.context.clear();e.camera.view=View{};});
        ImGui::TextWrapped("%s",e.message);ImGui::EndPopup();
    }
    float height=std::max(120.f,ImGui::GetContentRegionAvail().y-52);
    ImGui::BeginChild("EZDesign tools",{145,height},true);
    const char *names[]={"Select (V)","Line (L)","Rectangle (R)","Circle (C)","Arc (A)","Push/Pull (P)","Opening (O)","Offset (F)","Dimension (T)"};
    for(int i=0;i<9;++i)if(ImGui::Selectable(names[i],int(e.tool)==i)){deactivate();e.set_tool(Tool(i));}
    ImGui::Separator();ImGui::TextWrapped("Exact length / depth");ImGui::SetNextItemWidth(-1);
    if(ImGui::InputText("##EZ measurement",e.measurement,sizeof(e.measurement),ImGuiInputTextFlags_EnterReturnsTrue))safe([&]{e.move(e.cursor);e.confirm();});
    if(ImGui::Button("Apply preview"))safe([&]{e.move(e.cursor);e.confirm();});
    ImGui::TextWrapped("m / mm / cm / ft / in\nX / Y / Z: axis lock\nShift: hold direction\nEscape: cancel");
    ImGui::Separator();ImGui::TextUnformatted("Views");
    if(ImGui::Button("Top")){e.camera.view.yaw=0;e.camera.view.pitch=90;e.camera.view.orthographic=true;}
    ImGui::SameLine();if(ImGui::Button("Front")){e.camera.view.yaw=0;e.camera.view.pitch=0;e.camera.view.orthographic=true;}
    if(ImGui::Button("Right")){e.camera.view.yaw=90;e.camera.view.pitch=0;e.camera.view.orthographic=true;}
    ImGui::SameLine();if(ImGui::Button("Iso")){e.camera.view.yaw=35;e.camera.view.pitch=30;e.camera.view.orthographic=false;}
    if(ImGui::Button("Bottom")){e.camera.view.yaw=0;e.camera.view.pitch=-90;e.camera.view.orthographic=true;}
    if(ImGui::Button("Back")){e.camera.view.yaw=180;e.camera.view.pitch=0;e.camera.view.orthographic=true;}
    ImGui::SameLine();if(ImGui::Button("Left")){e.camera.view.yaw=-90;e.camera.view.pitch=0;e.camera.view.orthographic=true;}
    ImGui::Checkbox("Wireframe",&e.wireframe);
    ImGui::Separator();ImGui::TextDisabled("Unavailable:");ImGui::TextWrapped("Follow Me\nSolid Booleans\nMesh import\nUV / textures\nSection planes");
    ImGui::EndChild();ImGui::SameLine();
    float inspector=ImGui::GetContentRegionAvail().x>650?235.f:180.f;
    ImGui::BeginChild("EZDesign viewport",{std::max(100.f,ImGui::GetContentRegionAvail().x-inspector-8),height},true,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();size.x=std::max(1.f,size.x);size.y=std::max(1.f,size.y);e.camera.width=double(size.x);e.camera.height=double(size.y);
    auto *draw=ImGui::GetWindowDrawList();draw->ChannelsSplit(2);draw->ChannelsSetCurrent(1);bool over_gizmo=false;safe([&]{over_gizmo=gizmo(origin.x,origin.y,size.x,size.y);});draw->ChannelsSetCurrent(0);
    if(over_gizmo || gizmo_active)ImGui::Dummy(size);else ImGui::InvisibleButton("EZDesign canvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();auto &io=ImGui::GetIO();bool owns=(hovered || active) && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    glm::dvec2 mouse(io.MousePos.x-origin.x,io.MousePos.y-origin.y);
    bool navigation=owns && (io.MouseDown[1] || io.MouseDown[2] || io.MouseWheel!=0);
    if(owns && !over_gizmo && !gizmo_active)safe([&]{
        if(e.started && e.tool==Tool::Line && e.pending.points.size()==2){
            if(io.KeyShift && !e.direction_locked){Vec delta=e.pending.points[1]-e.pending.points[0];if(glm::length(delta)>Tolerances::weld){e.locked_direction=glm::normalize(delta);e.direction_locked=true;}}
            if(!io.KeyShift)e.direction_locked=false;
        }
        if(io.MouseDown[1] && active)e.camera.orbit({io.MouseDelta.x,io.MouseDelta.y});
        if(io.MouseDown[2] && active)e.camera.pan({io.MouseDelta.x,io.MouseDelta.y});
        if(io.MouseWheel!=0){ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,ImGui::GetItemID());Vec target=e.hover.position;e.camera.zoom(double(io.MouseWheel),mouse,e.hover.entity.kind!=Kind::None?&target:nullptr);}
        e.move(mouse);
        if(hovered && ImGui::IsMouseClicked(0))e.click(mouse);
        if(hovered && ImGui::IsMouseDoubleClicked(0) && e.tool==Tool::Select)e.enter();
    });
    safe([&]{raster(window,background,navigation || e.started || gizmo_active);});
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},color({background[0],background[1],background[2]}));
    if(!e.wireframe && texture)draw->AddImage(ImTextureRef(ImTextureID((uintptr_t)texture)),origin,{origin.x+size.x,origin.y+size.y});
    // Headless tests use the same derived triangles; actual windows always use
    // the shared z-buffered raster above, never painter-order viewport geometry.
    if(!window && !e.wireframe)safe([&]{
        const auto &mesh=e.has_geometry_preview()?e.preview_mesh:e.index.mesh;
        for(auto &t:mesh.triangles){glm::dvec2 p[3];bool valid=true;for(size_t i=0;i<3;++i)valid=valid && e.camera.project(t.points[i],p[i]);if(valid){auto c=is_selected(e,t)?glm::dvec3(.95,.67,.22):e.editor.doc.materials[visible_material(t,e.camera)].color;draw->AddTriangleFilled({origin.x+float(p[0].x),origin.y+float(p[0].y)},{origin.x+float(p[1].x),origin.y+float(p[1].y)},{origin.x+float(p[2].x),origin.y+float(p[2].y)},color(c));}}
    });
    safe([&]{overlays(origin.x,origin.y,size.x,size.y);});draw->ChannelsMerge();ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("EZDesign inspector",{0,height},true);
    ImGui::TextUnformatted("EDIT CONTEXT");ImGui::Text("%s / %llu",e.active_scope==1?"Root":"Definition (isolated)",(unsigned long long)e.active_scope);
    if(e.active_scope!=1 && ImGui::Button("Leave context"))safe([&]{deactivate();e.leave();});
    ImGui::Text("Selected ID: %llu",(unsigned long long)e.selected.id);
    auto &s=scope(e.editor.doc,e.active_scope);ImGui::Text("%zu vertices\n%zu edges\n%zu faces",s.vertices.size(),s.edges.size(),s.faces.size());
    if(ImGui::Button("New empty group"))safe([&]{
        e.cancel();if(e.action(command(Op::NewGroup))){auto &items=scope(e.editor.doc,e.active_scope).instances;e.selected={Kind::Group,e.active_scope,items.back().id};e.enter();}
    });
    if(ImGui::Button("Group entire context"))safe([&]{e.cancel();e.action(command(Op::Group));});
    if(ImGui::Button("Component from context"))safe([&]{e.cancel();auto c=command(Op::Group);c.value=1;e.action(c);});
    if(e.selected.kind==Kind::Group || e.selected.kind==Kind::Component){
        if(ImGui::Button("Edit definition"))safe([&]{deactivate();e.enter();});
        if(ImGui::Button("Make Unique"))safe([&]{e.action(command(Op::Unique));});
        ImGui::InputScalarN("Copy translation (m)",ImGuiDataType_Double,copy_offset,3);
        if(ImGui::Button("Linked copy"))safe([&]{auto c=command(Op::Instance);c.points={{copy_offset[0],copy_offset[1],copy_offset[2]}};e.action(c);});
    }
    if(e.selected.kind!=Kind::None){
        ImGui::Combo("Handle",&gizmo_operation,"Move\0Rotate\0Scale\0");ImGui::Checkbox("Local axes",&local);
    }
    if(ImGui::CollapsingHeader("Materials"))safe([&]{
        auto load_color=[&]{
            material_index=std::clamp(material_index,0,int(e.editor.doc.materials.size())-1);
            auto color=e.editor.doc.materials[size_t(material_index)].color;
            for(int i=0;i<3;++i)material_color[i]=float(color[i]);
            material_revision=e.editor.revision;
        };
        if(material_revision!=e.editor.revision)load_color();
        char label[32];std::snprintf(label,sizeof(label),"Material %d",material_index+1);
        if(ImGui::BeginCombo("Palette entry",label)){
            for(int i=0;i<int(e.editor.doc.materials.size());++i){
                std::snprintf(label,sizeof(label),"Material %d",i+1);
                if(ImGui::Selectable(label,i==material_index)){material_index=i;load_color();}
            }ImGui::EndCombo();
        }
        ImGui::ColorEdit3("Color",material_color);
        if(ImGui::Button("Apply color")){
            deactivate();auto edit=command(Op::EditMaterial);edit.material=uint32_t(material_index);
            edit.color={material_color[0],material_color[1],material_color[2]};e.action(edit);
        }
        ImGui::SameLine();ImGui::BeginDisabled(e.editor.doc.materials.size()>=32);
        if(ImGui::Button("Create material")){
            deactivate();auto edit=command(Op::AddMaterial);edit.color={material_color[0],material_color[1],material_color[2]};
            if(e.action(edit))material_index=int(e.editor.doc.materials.size())-1;
        }ImGui::EndDisabled();
        ImGui::TextWrapped("Color edits are staged until Apply or Create. Palette changes affect all faces using that entry.");
    });
    if(e.selected.kind==Kind::Face){
        auto &selected_scope=scope(e.editor.doc,e.active_scope);const auto &selected_face=face(selected_scope,e.selected.id);
        ImGui::Text("Area: %.6g m2",area(selected_scope,selected_face));
        ImGui::Text("Front: %u / Back: %u",selected_face.front+1,selected_face.back+1);
        ImGui::Checkbox("Paint back side",&paint_back);
        int columns=std::max(1,int(ImGui::GetContentRegionAvail().x/(ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.x)));
        for(uint32_t i=0;i<e.editor.doc.materials.size();++i){ImGui::PushID(int(i));auto c=e.editor.doc.materials[i].color;
            if(ImGui::ColorButton("Paint face",{float(c.x),float(c.y),float(c.z),1}))safe([&]{deactivate();auto edit=command(Op::Paint);edit.material=i;edit.back_side=paint_back;e.action(edit);});
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Material %u - paint %s",i+1,paint_back?"back":"front");
            if(i+1<e.editor.doc.materials.size() && (i+1)%unsigned(columns)!=0)ImGui::SameLine();
            ImGui::PopID();
        }
        if(ImGui::Button("Reverse face"))safe([&]{deactivate();e.action(command(Op::ReverseFace));});
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Reverse outer and hole loops; swap front/back assignments to preserve physical-side colors.");
    }
    if(ImGui::Button("Delete selected"))safe([&]{deactivate();e.action(command(Op::Delete));});
    if(ImGui::Button("Remove stray topology"))safe([&]{e.cancel();e.action(command(Op::Cleanup));});
    ImGui::Separator();ImGui::TextUnformatted("Construction plane");
    if(ImGui::Button("Ground XZ")){e.cancel();e.construction=Plane{};}
    if(e.selected.kind==Kind::Face && ImGui::Button("Use face plane"))safe([&]{e.cancel();auto &current=scope(e.editor.doc,e.active_scope);e.construction=plane(current,face(current,e.selected.id));});
    int units=int(e.editor.doc.units);if(ImGui::Combo("Units",&units,"Metres\0Millimetres\0Feet\0"))e.editor.doc.units=unsigned(units);
    for(auto &dim:e.editor.doc.dimensions){Vec a,b;bool ok=resolve(e.editor.doc,dim.a,a) && resolve(e.editor.doc,dim.b,b);char text[64];if(ok)unit_text(text,sizeof(text),glm::length(b-a),e.editor.doc.units);else std::snprintf(text,sizeof(text),"Unresolved target");ImGui::Text("Dim %llu: %s",(unsigned long long)dim.id,text);}
    ImGui::TextWrapped("RMB orbit; MMB pan; wheel zoom toward cursor. Definition editing isolates its own coordinates; separate scopes never weld.");
    ImGui::EndChild();
    ImGui::TextWrapped("%s",e.message);ImGui::TextDisabled("%s",e.expected());
    bool focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if(focused && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))safe([&]{
        bool ctrl=io.KeyCtrl || io.KeySuper;
        if(ctrl && ImGui::IsKeyPressed(ImGuiKey_S))save(e.path);
        if(ctrl && ImGui::IsKeyPressed(ImGuiKey_O))e.files_requested=true;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){if(gesture_at_frame_start || e.started || gizmo_active)deactivate();else e.leave();}
        if(ImGui::IsKeyPressed(ImGuiKey_Enter)){if(e.started)e.confirm();else {deactivate();e.enter();}}
        if(ImGui::IsKeyPressed(ImGuiKey_Delete)){deactivate();e.action(command(Op::Delete));}
        if(!ctrl){ImGuiKey keys[]={ImGuiKey_V,ImGuiKey_L,ImGuiKey_R,ImGuiKey_C,ImGuiKey_A,ImGuiKey_P,ImGuiKey_O,ImGuiKey_F,ImGuiKey_T};for(int i=0;i<9;++i)if(ImGui::IsKeyPressed(keys[i])){deactivate();e.set_tool(Tool(i));}
            for(int i=0;i<3;++i)if(ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_X)+i)))e.axis=e.axis==i+1?0:i+1;}
    });
}
} // namespace jfx::ez
