#include "tools.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace jfx::ez {
namespace {
constexpr double pi=3.14159265358979323846;
bool bounds_hit(const Bounds &b,Ray ray,double maximum) {
    double lo=0,hi=maximum;
    for(int i=0;i<3;++i) {
        if(std::abs(ray.direction[i])<1e-15){if(ray.origin[i]<b.low[i] || ray.origin[i]>b.high[i])return false;continue;}
        double a=(b.low[i]-ray.origin[i])/ray.direction[i],c=(b.high[i]-ray.origin[i])/ray.direction[i];if(a>c)std::swap(a,c);
        lo=std::max(lo,a);hi=std::min(hi,c);if(lo>hi+Tolerances::plane)return false;
    }return true;
}
double triangle_hit(const Triangle &t,Ray ray) {
    Vec a=t.points[0],e1=t.points[1]-a,e2=t.points[2]-a,h=glm::cross(ray.direction,e2);double det=glm::dot(e1,h);
    if(std::abs(det)<1e-14*glm::length(e1)*glm::length(e2))return 1e100;
    Vec s=ray.origin-a;double u=glm::dot(s,h)/det;if(u<-1e-10 || u>1+1e-10)return 1e100;
    Vec q=glm::cross(s,e1);double v=glm::dot(ray.direction,q)/det;if(v<-1e-10 || u+v>1+1e-10)return 1e100;
    double depth=glm::dot(e2,q)/det;return depth>=0?depth:1e100;
}
double distance(glm::dvec2 p,glm::dvec2 a,glm::dvec2 b,double &t) {
    auto ab=b-a;double n=glm::dot(ab,ab);t=n>0?std::clamp(glm::dot(p-a,ab)/n,0.0,1.0):0;return glm::length(p-a-ab*t);
}
bool visible(Vec p,const PickIndex &index,const Camera &camera) {
    glm::dvec2 pixel;if(!camera.project(p,pixel))return false;
    Ray ray=camera.ray(pixel);auto hit=index.intersect(ray);
    return hit.entity.kind==Kind::None || glm::dot(p-ray.origin,ray.direction)<=hit.depth+Tolerances::plane*4;
}
bool exists(const Document &d,Entity e) {
    for(auto &s:d.scopes)if(s.id==e.scope){
        for(auto &v:s.vertices)if(e.kind==Kind::Vertex && v.id==e.id)return true;
        for(auto &v:s.edges)if(e.kind==Kind::Edge && v.id==e.id)return true;
        for(auto &v:s.faces)if(e.kind==Kind::Face && v.id==e.id)return true;
        for(auto &v:s.instances)if((e.kind==Kind::Group || e.kind==Kind::Component) && v.id==e.id)return true;
    }
    return false;
}
} // namespace
glm::dquat Camera::orientation() const {return glm::angleAxis(glm::radians(view.yaw),Vec(0,1,0))*glm::angleAxis(glm::radians(-view.pitch),Vec(1,0,0));}
glm::dmat4 Camera::view_matrix() const {
    // Camera-relative coordinates at the renderer/gizmo boundary.
    return glm::translate(glm::mat4_cast(glm::conjugate(orientation())),Vec(0))*glm::translate(glm::dmat4(1),-orientation()*Vec(0,0,view.distance));
}
glm::dmat4 Camera::projection() const {
    if(view.orthographic){double half=view.distance*std::tan(glm::radians(22.5));return glm::ortho(-half*width/height,half*width/height,-half,half,.0001,1e10);}
    return glm::perspective(glm::radians(45.0),width/height,.0001,1e10);
}
Ray Camera::ray(glm::dvec2 pixel) const {
    double focal=height*.5/std::tan(glm::radians(22.5));Vec local((pixel.x-width*.5)/focal,-(pixel.y-height*.5)/focal,-1);
    auto q=orientation();Vec eye=view.target+q*Vec(0,0,view.distance);
    if(view.orthographic)return {eye+q*Vec(local.x*view.distance,local.y*view.distance,0),q*Vec(0,0,-1)};
    return {eye,glm::normalize(q*local)};
}
bool Camera::project(Vec p,glm::dvec2 &pixel,double *depth) const {
    Vec local=glm::conjugate(orientation())*(p-view.target)-Vec(0,0,view.distance);double z=-local.z;
    if(z<.0001)return false;
    double focal=height*.5/std::tan(glm::radians(22.5));double divisor=view.orthographic?view.distance:z;
    pixel={width*.5+local.x*focal/divisor,height*.5-local.y*focal/divisor};if(depth)*depth=z;return true;
}
bool Camera::on_plane(glm::dvec2 pixel,const Plane &p,Vec &result) const {
    Ray r=ray(pixel);double den=glm::dot(r.direction,p.normal);if(std::abs(den)<1e-9)return false;
    double t=glm::dot(p.origin-r.origin,p.normal)/den;if(t<0)return false;result=r.origin+r.direction*t;return true;
}
void Camera::orbit(glm::dvec2 delta){view.yaw+=delta.x*.35;view.pitch=std::clamp(view.pitch+delta.y*.35,-89.9,89.9);}
void Camera::pan(glm::dvec2 delta){double metres=2*view.distance*std::tan(glm::radians(22.5))/height;view.target+=orientation()*Vec(-delta.x*metres,delta.y*metres,0);}
void Camera::zoom(double wheel,glm::dvec2 cursor,const Vec *target) {
    Plane p;p.origin=target?*target:view.target;p.normal=orientation()*Vec(0,0,1);Vec before,after;
    bool valid=on_plane(cursor,p,before);view.distance=std::clamp(view.distance*std::exp(-wheel*.15),.001,1e8);
    if(valid && on_plane(cursor,p,after))view.target+=before-after;
}
void Camera::fit(const Document &d,Id id) {
    auto mesh=triangulate(d,id);Bounds b;
    for(auto &t:mesh.triangles)for(Vec p:t.points){b.low=glm::min(b.low,p);b.high=glm::max(b.high,p);}
    for(auto &v:scope(d,id).vertices){b.low=glm::min(b.low,v.position);b.high=glm::max(b.high,v.position);}
    if(b.low.x>b.high.x){view.target={};view.distance=12;return;}
    view.target=(b.low+b.high)*.5;view.distance=std::max(.01,glm::length(b.high-b.low)*1.5);
}
int PickIndex::build(size_t start,size_t count) {
    Node node;node.start=start;node.count=count;
    for(size_t i=start;i<start+count;++i)for(Vec p:mesh.triangles[order[i]].points){node.bounds.low=glm::min(node.bounds.low,p);node.bounds.high=glm::max(node.bounds.high,p);}
    int index=int(nodes.size());nodes.push_back(node);
    if(count>8){Vec extent=node.bounds.high-node.bounds.low;int axis=extent.x>extent.y?0:1;if(extent.z>extent[axis])axis=2;
        auto center=[&](size_t i){auto &t=mesh.triangles[i];return t.points[0][axis]+t.points[1][axis]+t.points[2][axis];};
        size_t middle=start+count/2;std::nth_element(order.begin()+static_cast<ptrdiff_t>(start),order.begin()+static_cast<ptrdiff_t>(middle),order.begin()+static_cast<ptrdiff_t>(start+count),[&](size_t a,size_t b){return center(a)<center(b);});
        int left=build(start,count/2),right=build(middle,count-count/2);nodes[size_t(index)].left=left;nodes[size_t(index)].right=right;
    }return index;
}
void PickIndex::rebuild(Mesh next) {PickIndex candidate;candidate.mesh=std::move(next);candidate.order.resize(candidate.mesh.triangles.size());
    for(size_t i=0;i<candidate.order.size();++i)candidate.order[i]=i;
    if(!candidate.order.empty())candidate.build(0,candidate.order.size());
    *this=std::move(candidate);
}
Hit PickIndex::intersect(Ray ray) const {
    Hit hit;if(nodes.empty())return hit;std::array<int,64> stack{};size_t count=1;stack[0]=0;
    while(count){auto &node=nodes[size_t(stack[--count])];if(!bounds_hit(node.bounds,ray,hit.depth))continue;
        if(node.left>=0){stack[count++]=node.left;stack[count++]=node.right;continue;}
        for(size_t i=node.start;i<node.start+node.count;++i){auto &t=mesh.triangles[order[i]];double depth=triangle_hit(t,ray);
            if(depth<hit.depth-1e-10 || (std::abs(depth-hit.depth)<1e-10 && t.face<hit.entity.id)){
                hit.depth=depth;hit.entity={Kind::Face,t.scope,t.face};hit.path=t.path;hit.position=ray.origin+ray.direction*depth;
                hit.normal=glm::normalize(glm::cross(t.points[1]-t.points[0],t.points[2]-t.points[0]));
            }
        }
    }return hit;
}
const char *Snap::hint() const {
    switch(kind){case SnapKind::Length:return "Exact length";case SnapKind::Endpoint:return "Endpoint";case SnapKind::Midpoint:return "Midpoint";case SnapKind::Origin:return "Origin";
    case SnapKind::Axis:return "Axis / alignment";case SnapKind::Edge:return "On Edge";case SnapKind::Face:return "On Face";default:return "Construction Plane";}
}
Snap Inference::resolve(const Document &d,Id id,const PickIndex &index,const Camera &camera,glm::dvec2 cursor,const Plane &plane,const Vec *anchor,int axis) {
    Snap chosen;double score=1e100;auto &s=scope(d,id);
    auto add=[&](Vec point,SnapKind kind,Id entity,double radius=Tolerances::snap_pixels){
        // Every drawing snap belongs to the operation plane. No post-snap
        // projection of a point off that plane can make preview and commit differ.
        if(std::abs(glm::dot(point-plane.origin,plane.normal))>Tolerances::plane)return;
        glm::dvec2 pixel;if(!camera.project(point,pixel))return;double distance=glm::length(pixel-cursor);if(distance>radius || !visible(point,index,camera))return;
        double rank=double(int(SnapKind::Endpoint)-int(kind))*1.5+distance;
        if(last.valid && last.id==entity && last.kind==kind && glm::length(last.position-point)<Tolerances::weld)rank-=3;
        if(rank<score-1e-9 || (std::abs(rank-score)<1e-9 && entity<chosen.id)){chosen={point,kind,entity,true};score=rank;}
    };
    for(auto &v:s.vertices)add(v.position,SnapKind::Endpoint,v.id,last.id==v.id?12:9);
    for(auto &e:s.edges){Vec a=vertex(s,e.a).position,b=vertex(s,e.b).position;add((a+b)*.5,SnapKind::Midpoint,e.id);
        glm::dvec2 aa,bb;double za=0,zb=0;
        if(camera.project(a,aa,&za) && camera.project(b,bb,&zb)){double t=0;distance(cursor,aa,bb,t);
            if(!camera.view.orthographic)t=(t/zb)/((1-t)/za+t/zb);
            add(a+(b-a)*t,SnapKind::Edge,e.id);}}
    add(Vec(0),SnapKind::Origin,0);
    auto face_hit=index.intersect(camera.ray(cursor));if(face_hit.entity.scope==id && face_hit.path.empty() && face_hit.entity.kind!=Kind::None)add(face_hit.position,SnapKind::Face,face_hit.entity.id);
    Vec point;if(camera.on_plane(cursor,plane,point)){
        if(anchor){for(int i=0;i<3;++i){Vec aligned=*anchor;aligned[i]=point[i];add(aligned,SnapKind::Axis,Id(i+1));}}
        if(!chosen.valid)chosen={point,SnapKind::Plane,0,true};
    }
    if(chosen.valid && axis && anchor){Vec direction(0);direction[axis-1]=1;
        // Closest points between the view ray and locked infinite direction.
        auto ray=camera.ray(cursor);Vec w=ray.origin-*anchor;double dot=glm::dot(ray.direction,direction),den=1-dot*dot;
        if(den>1e-10){double t=(glm::dot(w,direction)-dot*glm::dot(w,ray.direction))/den;chosen={*anchor+direction*t,SnapKind::Axis,Id(axis),true};}
    }
    last=chosen;return chosen;
}
void Controller::refresh() {
    if(cached_revision==editor.revision)return;
    bool found=false;for(auto &s:editor.doc.scopes)found=found || s.id==active_scope;
    if(!found){active_scope=1;context.clear();cancel();}
    if(!exists(editor.doc,selected))selected={};
    index.rebuild(triangulate(editor.doc,active_scope));cached_revision=editor.revision;
}
void Controller::set_tool(Tool next){cancel();tool=next;axis=0;measurement[0]=0;std::snprintf(message,sizeof(message),"%s",expected());}
void Controller::cancel() noexcept {++preview_generation;started=false;stage=0;preview_valid=false;direction_locked=false;preview_mesh.triangles.clear();inference.last={};}
const char *Controller::expected() const {
    switch(tool){case Tool::Select:return "Click to select; double-click / Enter to edit a component. Escape leaves context.";
    case Tool::Line:return started?"Click next endpoint; length accepts m/mm/cm/ft/in. X/Y/Z lock. Escape ends chain.":"Click the first endpoint on the construction plane.";
    case Tool::Rectangle:return started?"Click opposite corner. Escape cancels.":"Click first rectangle corner on the construction plane.";
    case Tool::Circle:return started?"Click radius point or enter an exact radius.":"Click circle center (sampled polygon).";
    case Tool::Arc:return stage==2?"Click arc end direction (counterclockwise); Escape cancels.":started?"Click radius/start point.":"Click arc center (sampled edges).";
    case Tool::Pull:return started?"Move vertically for signed depth; enter exact distance or click to commit.":"Click a face to Push/Pull.";
    case Tool::Opening:return started?"Click opposite corner; boundary contact cuts a door notch in an isolated prism.":"Click first opening corner on a solid face.";
    case Tool::Offset:return started?"Enter a signed offset (negative inset); inspect preview and confirm.":"Click an isolated planar face to offset.";
    case Tool::Measure:return started?"Click second measurement endpoint; creates a persistent dimension.":"Click first measurement endpoint.";}
    return "";
}
void Controller::move(glm::dvec2 pixel) {
    refresh();cursor=pixel;hover=index.intersect(camera.ray(pixel));
    Plane p=construction;
    if(started && (tool==Tool::Opening || tool==Tool::Pull || tool==Tool::Offset))p=plane(scope(editor.doc,active_scope),face(scope(editor.doc,active_scope),pending.entity));
    snap=inference.resolve(editor.doc,active_scope,index,camera,pixel,p,started?&anchor:nullptr,axis);
    if(started){
        if(tool==Tool::Pull){double metres=2*camera.view.distance*std::tan(glm::radians(22.5))/camera.height;pending.value=(drag_start_y-pixel.y)*metres;}
        else if(snap.valid){Vec point=snap.position;
            if(direction_locked && tool==Tool::Line){point=anchor+locked_direction*glm::dot(point-anchor,locked_direction);snap.kind=SnapKind::Axis;}
            double exact=0;if(measurement[0] && parse_length(measurement,editor.doc.units,exact) && (tool==Tool::Line || tool==Tool::Circle)){
                Vec direction=point-anchor;if(glm::length(direction)>Tolerances::weld){point=anchor+glm::normalize(direction)*exact;snap.kind=SnapKind::Length;}
            }
            snap.position=point;
            if(tool==Tool::Line || tool==Tool::Rectangle || tool==Tool::Opening || tool==Tool::Circle || tool==Tool::Arc || tool==Tool::Measure)pending.points={anchor,point};
        }
        update_preview();
    }
}
void Controller::update_preview() {
    preview_valid=false;preview_mesh.triangles.clear();
    Command c=pending;c.scope=active_scope;
    if(measurement[0]){
        double exact=0;
        if(tool!=Tool::Line && tool!=Tool::Circle && tool!=Tool::Pull && tool!=Tool::Offset){
            std::snprintf(message,sizeof(message),"This tool uses click positions; clear numeric input to continue.");return;
        }
        if(!parse_length(measurement,editor.doc.units,exact) || (tool==Tool::Circle && exact<=Tolerances::weld)){
            std::snprintf(message,sizeof(message),"Invalid distance; use a finite length with m, mm, cm, ft or in (circle radius must be positive).");return;
        }
        if(tool==Tool::Pull || tool==Tool::Offset)c.value=exact;
    }
    if(tool==Tool::Circle || tool==Tool::Arc){
        if(c.points.size()!=2)return;
        auto a=construction.project(anchor),b=construction.project(c.points[1]);double radius=glm::length(b-a);
        double start=0,sweep=2*pi;
        if(tool==Tool::Arc){if(stage<2)return;auto q=construction.project(second);radius=glm::length(q-a);start=std::atan2(q.y-a.y,q.x-a.x);sweep=std::atan2(b.y-a.y,b.x-a.x)-start;while(sweep<=0)sweep+=2*pi;}
        c.points.clear();unsigned count=std::clamp(segments,8u,128u);
        for(unsigned i=0;i<=(tool==Tool::Arc?count:count-1);++i){double angle=start+sweep*double(i)/count;c.points.push_back(construction.point(a+glm::dvec2(std::cos(angle),std::sin(angle))*radius));}
        c.op=tool==Tool::Circle?Op::Polygon:Op::Line;
    }
    if(tool==Tool::Measure){preview_valid=pending.points.size()==2;return;}
    if(editor.preview(c,candidate)){preview_mesh=triangulate(candidate,active_scope);preview_valid=true;
        std::snprintf(message,sizeof(message),"%s",expected());
    }else std::snprintf(message,sizeof(message),"%s",editor.error);
}
bool Controller::confirm() {
    if(!started)return false;
    update_preview();if(!preview_valid)return false;
    Command c=pending;c.scope=active_scope;
    if(tool==Tool::Measure){c.op=Op::Dimension;c.a=measure_start;c.b={active_scope,snap.kind==SnapKind::Endpoint?snap.id:0,pending.points.back()};}
    else if(tool==Tool::Circle || tool==Tool::Arc){
        // Commit the very same candidate produced by sampled preview, through
        // the ordinary command path (no UI-only geometry mutation).
        auto a=construction.project(anchor),b=construction.project(pending.points[1]);double radius=glm::length(b-a),start=0,sweep=2*pi;
        if(tool==Tool::Arc){auto q=construction.project(second);radius=glm::length(q-a);start=std::atan2(q.y-a.y,q.x-a.x);sweep=std::atan2(b.y-a.y,b.x-a.x)-start;while(sweep<=0)sweep+=2*pi;}
        c.points.clear();unsigned count=std::clamp(segments,8u,128u);for(unsigned i=0;i<=(tool==Tool::Arc?count:count-1);++i){double angle=start+sweep*double(i)/count;c.points.push_back(construction.point(a+glm::dvec2(std::cos(angle),std::sin(angle))*radius));}c.op=tool==Tool::Circle?Op::Polygon:Op::Line;
    }
    if(measurement[0] && (tool==Tool::Pull || tool==Tool::Offset))parse_length(measurement,editor.doc.units,c.value);
    bool line=tool==Tool::Line;Vec next=pending.points.empty()?anchor:pending.points.back();
    if(!action(c))return false;
    if(tool==Tool::Pull || tool==Tool::Opening || tool==Tool::Offset)selected={Kind::Face,active_scope,pending.entity};
    else if(tool!=Tool::Measure && !scope(editor.doc,active_scope).faces.empty())selected={Kind::Face,active_scope,scope(editor.doc,active_scope).faces.back().id};
    cancel();measurement[0]=0;
    if(line){started=true;anchor=next;pending=Command{};pending.scope=active_scope;pending.normal=construction.normal;pending.points={anchor,anchor};}
    return true;
}
bool Controller::click(glm::dvec2 pixel) {
    move(pixel);
    if(tool==Tool::Select){
        if(snap.valid && (snap.kind==SnapKind::Endpoint || snap.kind==SnapKind::Edge || snap.kind==SnapKind::Midpoint))selected={snap.kind==SnapKind::Endpoint?Kind::Vertex:Kind::Edge,active_scope,snap.id};
        else selected=hover.entity;
        if(!hover.path.empty()){Id id=hover.path[0];for(auto &i:scope(editor.doc,active_scope).instances)if(i.id==id)selected={i.component?Kind::Component:Kind::Group,active_scope,id};}
        return true;
    }
    if(started){if(tool==Tool::Arc && stage==1){second=snap.position;stage=2;return true;}return confirm();}
    pending=Command{};pending.scope=active_scope;pending.normal=construction.normal;
    if(tool==Tool::Pull || tool==Tool::Opening || tool==Tool::Offset){
        if(hover.entity.kind!=Kind::Face || !hover.path.empty() || hover.entity.scope!=active_scope){std::snprintf(message,sizeof(message),"Enter the component first, then select an editable face.");return false;}
        selected=hover.entity;pending.entity=hover.entity.id;anchor=hover.position;pending.normal=hover.normal;drag_start_y=pixel.y;
        pending.op=tool==Tool::Pull?Op::Extrude:tool==Tool::Opening?Op::Opening:Op::Offset;pending.value=tool==Tool::Offset?-.1:0;
    }else{
        if(!snap.valid)return false;
        anchor=snap.position;
        pending.op=tool==Tool::Rectangle?Op::Rectangle:tool==Tool::Measure?Op::Dimension:Op::Line;
        measure_start={active_scope,snap.kind==SnapKind::Endpoint?snap.id:0,anchor};
    }
    pending.points={anchor,anchor};started=true;stage=1;std::snprintf(message,sizeof(message),"%s",expected());return true;
}
bool Controller::action(const Command &command) {
    bool ok=editor.execute(command);std::snprintf(message,sizeof(message),"%s",ok?"Edit committed (one undo step).":editor.error);if(ok)refresh();return ok;
}
void Controller::history(bool redo){cancel();bool ok=redo?editor.redo():editor.undo();if(ok)refresh();std::snprintf(message,sizeof(message),"%s",ok?(redo?"Redo":"Undo"):editor.error);}
bool Controller::enter(){
    if(selected.kind!=Kind::Component && selected.kind!=Kind::Group)return false;
    for(auto &i:scope(editor.doc,active_scope).instances)if(i.id==selected.id){context.push_back(i.id);active_scope=i.definition;selected={};cancel();cached_revision=0;refresh();camera.fit(editor.doc,active_scope);return true;}
    return false;
}
void Controller::leave(){cancel();if(context.empty())return;context.pop_back();active_scope=1;
    for(Id id:context)for(auto &i:scope(editor.doc,active_scope).instances)if(i.id==id){active_scope=i.definition;break;}
    selected={};cached_revision=0;refresh();camera.fit(editor.doc,active_scope);
}
} // namespace jfx::ez
