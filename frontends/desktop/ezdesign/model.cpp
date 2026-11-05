#include "model.h"
#include <mapbox/earcut.hpp>
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jfx::ez {
namespace {
using V2=glm::dvec2;
using Input=std::basic_istringstream<char,std::char_traits<char>,tilly::allocator<char>>;
using Output=std::basic_ostringstream<char,std::char_traits<char>,tilly::allocator<char>>;
constexpr size_t max_entities=8192, max_bytes=16*1024*1024;
void need(bool ok,const char *message) { if(!ok) throw std::invalid_argument(message); }
bool finite(Vec p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
    std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})<=Tolerances::coordinate_limit; }
double cross(V2 a,V2 b) { return a.x*b.y-a.y*b.x; }
double length2(Vec p) { return glm::dot(p,p); }
bool near(Vec a,Vec b) { return length2(a-b)<=Tolerances::weld*Tolerances::weld; }
template<class T> const T &find(const tilly::vector<T> &items,Id id) {
    for(auto &item:items) if(item.id==id) return item;
    throw std::invalid_argument("Entity reference does not exist in the active scope");
}
template<class T> T &find(tilly::vector<T> &items,Id id) { return const_cast<T &>(find(static_cast<const tilly::vector<T>&>(items),id)); }
Plane make_plane(Vec origin,Vec normal) {
    need(finite(origin) && finite(normal) && glm::length(normal)>Tolerances::weld,"Invalid construction plane");
    Plane p;p.origin=origin;p.normal=glm::normalize(normal);
    Vec seed=std::abs(p.normal.x)<.8?Vec(1,0,0):Vec(0,0,1);
    p.u=glm::normalize(seed-p.normal*glm::dot(seed,p.normal));p.v=glm::cross(p.normal,p.u);return p;
}
tilly::vector<V2> projected(const Scope &s,const Loop &loop,const Plane &p) {
    tilly::vector<V2> out;for(Id id:loop) out.push_back(p.project(vertex(s,id).position));return out;
}
double signed_area(const tilly::vector<V2> &loop) {
    double sum=0;for(size_t i=0;i<loop.size();++i) sum+=cross(loop[i],loop[(i+1)%loop.size()]);return sum*.5;
}
bool on_segment(V2 p,V2 a,V2 b,double tolerance=Tolerances::weld) {
    V2 ab=b-a;double n=glm::length(ab);if(n<=tolerance) return glm::length(p-a)<=tolerance;
    double t=glm::dot(p-a,ab)/(n*n);
    return t>=-tolerance/n && t<=1+tolerance/n && std::abs(cross(p-a,ab))/n<=tolerance;
}
bool inside(V2 p,const tilly::vector<V2> &loop,bool boundary=false) {
    bool out=false;for(size_t i=0,j=loop.size()-1;i<loop.size();j=i++) {
        auto a=loop[j],b=loop[i];if(on_segment(p,a,b)) return boundary;
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) out=!out;
    }return out;
}
bool segments_intersect(V2 a,V2 b,V2 c,V2 d) {
    double ab=glm::length(b-a),cd=glm::length(d-c);
    double x=cross(b-a,c-a),y=cross(b-a,d-a),z=cross(d-c,a-c),w=cross(d-c,b-c);
    double eps=Tolerances::weld;
    if(((x>eps*ab && y<-eps*ab)||(y>eps*ab && x<-eps*ab)) &&
       ((z>eps*cd && w<-eps*cd)||(w>eps*cd && z<-eps*cd))) return true;
    return on_segment(a,c,d)||on_segment(b,c,d)||on_segment(c,a,b)||on_segment(d,a,b);
}
bool in_face(const Scope &s,const Face &f,Vec point,bool boundary=false) {
    auto p=plane(s,f);if(std::abs(glm::dot(point-p.origin,p.normal))>Tolerances::plane) return false;
    V2 q=p.project(point);if(!inside(q,projected(s,f.outer,p),boundary)) return false;
    for(auto &h:f.holes) if(inside(q,projected(s,h,p),!boundary)) return false;
    return true;
}
const Edge *edge_between(const Scope &s,Id a,Id b) {
    for(auto &e:s.edges) if((e.a==a && e.b==b)||(e.a==b && e.b==a)) return &e;
    return nullptr;
}
Id add_vertex(Document &d,Scope &s,Vec p) {
    need(finite(p),"Position is not finite or exceeds the coordinate limit");
    for(auto &v:s.vertices) if(near(v.position,p)) return v.id;
    Id id=d.next++;s.vertices.push_back({id,p});return id;
}
Id add_edge(Document &d,Scope &s,Id a,Id b) {
    need(a!=b,"A segment is shorter than the welding tolerance");
    if(auto *e=edge_between(s,a,b)) return e->id;
    Id id=d.next++;s.edges.push_back({id,a,b});return id;
}
void boundary_edges(Document &d,Scope &s,const Loop &loop) {
    for(size_t i=0;i<loop.size();++i) add_edge(d,s,loop[i],loop[(i+1)%loop.size()]);
}
void add_face(Document &d,Scope &s,Loop loop,uint32_t front=0,uint32_t back=0) {
    boundary_edges(d,s,loop);s.faces.push_back({d.next++,std::move(loop),{},front,back});
}
tilly::vector<const Loop *> loops(const Face &f) {
    tilly::vector<const Loop *> out{&f.outer};for(auto &h:f.holes) out.push_back(&h);return out;
}
void check_loop(const Scope &s,const Loop &loop,const Plane &p,bool positive) {
    need(loop.size()>=3 && loop.size()<=max_entities,"Face boundary needs at least three distinct vertices");
    for(size_t i=0;i<loop.size();++i) {
        const auto &v=vertex(s,loop[i]);
        need(std::abs(glm::dot(v.position-p.origin,p.normal))<=Tolerances::plane,"Face vertices are not coplanar");
        need(edge_between(s,loop[i],loop[(i+1)%loop.size()]),"Face boundary is missing an editable edge");
        for(size_t j=0;j<i;++j) need(loop[i]!=loop[j],"Face boundary repeats a vertex");
    }
    auto points=projected(s,loop,p);double a=signed_area(points);
    need(positive?a>Tolerances::weld*Tolerances::weld:a<-Tolerances::weld*Tolerances::weld,"Face boundary is degenerate or has inconsistent winding");
    for(size_t i=0;i<points.size();++i) for(size_t j=i+1;j<points.size();++j) {
        if(j==i+1 || (i==0 && j+1==points.size())) continue;
        need(!segments_intersect(points[i],points[(i+1)%points.size()],points[j],points[(j+1)%points.size()]),"Face boundary self-intersects");
    }
}
void separate(const tilly::vector<V2> &a,const tilly::vector<V2> &b) {
    for(size_t i=0;i<a.size();++i) for(size_t j=0;j<b.size();++j)
        need(!segments_intersect(a[i],a[(i+1)%a.size()],b[j],b[(j+1)%b.size()]),"Face boundaries touch or intersect; move the profile away from the boundary");
}
void validate_face(const Scope &s,const Face &f) {
    auto p=plane(s,f);check_loop(s,f.outer,p,true);auto outer=projected(s,f.outer,p);
    for(size_t i=0;i<f.holes.size();++i) {
        check_loop(s,f.holes[i],p,false);auto h=projected(s,f.holes[i],p);separate(outer,h);
        need(inside(h[0],outer),"Inner boundary lies outside its face");
        for(size_t j=0;j<i;++j) {
            auto other=projected(s,f.holes[j],p);separate(h,other);
            need(!inside(h[0],other) && !inside(other[0],h),"Face holes overlap or nest");
        }
    }
}
bool same_loop(const Loop &a,const Loop &b) {
    if(a.size()!=b.size()) return false;
    for(size_t start=0;start<b.size();++start) if(a[0]==b[start]) {
        bool same=true;for(size_t i=0;i<a.size();++i) same=same && a[i]==b[(start+i)%b.size()];if(same) return true;
    }
    return false;
}
bool coplanar(const Scope &s,const Edge &e,const Plane &p) {
    return std::abs(glm::dot(vertex(s,e.a).position-p.origin,p.normal))<=Tolerances::plane &&
        std::abs(glm::dot(vertex(s,e.b).position-p.origin,p.normal))<=Tolerances::plane;
}
void split_edge(Document &d,Scope &s,Id edge_id,tilly::vector<Id> cuts) {
    auto original=find(s.edges,edge_id);Vec a=vertex(s,original.a).position,b=vertex(s,original.b).position;
    cuts.push_back(original.a);cuts.push_back(original.b);
    std::sort(cuts.begin(),cuts.end(),[&](Id x,Id y) {return glm::dot(vertex(s,x).position-a,b-a)<glm::dot(vertex(s,y).position-a,b-a);});
    cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    if(cuts.size()==2) return;
    find(s.edges,edge_id).b=cuts[1];
    for(size_t i=1;i+1<cuts.size();++i) add_edge(d,s,cuts[i],cuts[i+1]);
    for(auto &f:s.faces) {
        auto patch=[&](Loop &loop) {
            for(size_t i=0;i<loop.size();++i) {
                Id x=loop[i],y=loop[(i+1)%loop.size()];
                if((x==original.a && y==original.b)||(x==original.b && y==original.a)) {
                    Loop insert=cuts;if(x==original.b) std::reverse(insert.begin(),insert.end());
                    loop.insert(loop.begin()+static_cast<ptrdiff_t>(i+1),insert.begin()+1,insert.end()-1);break;
                }
            }
        };patch(f.outer);for(auto &h:f.holes) patch(h);
    }
}
// Resolve the planar arrangement before walking directed halfedges. There is no
// restriction on the number of incident faces in the canonical edge store.
void segment(Document &d,Scope &s,Vec a,Vec b,const Plane &p) {
    need(!near(a,b),"Line length is below the welding tolerance");
    Id va=add_vertex(d,s,a),vb=add_vertex(d,s,b);tilly::vector<Id> on_line{va,vb};
    auto existing=s.edges;V2 aa=p.project(a),bb=p.project(b),r=bb-aa;
    for(auto &e:existing) if(coplanar(s,e,p)) {
        V2 c=p.project(vertex(s,e.a).position),dd=p.project(vertex(s,e.b).position),v=dd-c;
        tilly::vector<Id> cuts;
        for(Id id:{va,vb}) if(on_segment(p.project(vertex(s,id).position),c,dd)) cuts.push_back(id);
        for(Id id:{e.a,e.b}) if(on_segment(p.project(vertex(s,id).position),aa,bb)) on_line.push_back(id);
        double denom=cross(r,v);
        if(std::abs(denom)>1e-12*glm::length(r)*glm::length(v)) {
            double t=cross(c-aa,v)/denom,u=cross(c-aa,r)/denom;
            if(t>=0 && t<=1 && u>=0 && u<=1) {
                Id id=add_vertex(d,s,p.point(aa+t*r));cuts.push_back(id);on_line.push_back(id);
            }
        }
        split_edge(d,s,e.id,std::move(cuts));
    }
    std::sort(on_line.begin(),on_line.end(),[&](Id x,Id y){return glm::dot(vertex(s,x).position-a,b-a)<glm::dot(vertex(s,y).position-a,b-a);});
    on_line.erase(std::unique(on_line.begin(),on_line.end()),on_line.end());
    for(size_t i=1;i<on_line.size();++i) add_edge(d,s,on_line[i-1],on_line[i]);
}
void regions(Document &d,Scope &s,const Plane &p) {
    struct Half { Id a,b; bool used=false; };
    tilly::vector<Half> half;
    for(auto &e:s.edges) if(coplanar(s,e,p)) {half.push_back({e.a,e.b});half.push_back({e.b,e.a});}
    tilly::vector<Loop> bounded;
    for(size_t start=0;start<half.size();++start) if(!half[start].used) {
        Loop loop;size_t current=start;bool closed=false;
        for(size_t step=0;step<=half.size();++step) {
            auto &h=half[current];if(h.used) {closed=current==start;break;}h.used=true;loop.push_back(h.a);
            V2 a=p.project(vertex(s,h.a).position),b=p.project(vertex(s,h.b).position);
            double reverse=std::atan2(a.y-b.y,a.x-b.x),best=7;size_t next=half.size();
            for(size_t j=0;j<half.size();++j) if(half[j].a==h.b) {
                V2 c=p.project(vertex(s,half[j].b).position);double angle=reverse-std::atan2(c.y-b.y,c.x-b.x);
                while(angle<=1e-12) angle+=2*3.14159265358979323846;
                if(angle<best) {best=angle;next=j;}
            }
            if(next==half.size()) break;
            current=next;
        }
        if(!closed || loop.size()<3) continue;
        // Remove bridge excursions (A B C B A) from a region boundary. Loose
        // branches must not prevent the surrounding face from being discovered.
        bool changed=true;
        while(changed && loop.size()>=3) {
            changed=false;for(size_t i=0;i<loop.size();++i) if(loop[i]==loop[(i+2)%loop.size()]) {
                size_t a=(i+1)%loop.size(),b=(i+2)%loop.size();
                if(a>b) std::swap(a,b);
                loop.erase(loop.begin()+static_cast<ptrdiff_t>(b));loop.erase(loop.begin()+static_cast<ptrdiff_t>(a));changed=true;break;
            }
        }
        if(loop.size()<3 || signed_area(projected(s,loop,p))<=Tolerances::weld*Tolerances::weld) continue;
        bool repeats=false;for(size_t i=0;i<loop.size();++i) for(size_t j=0;j<i;++j) repeats=repeats || loop[i]==loop[j];
        if(!repeats) bounded.push_back(std::move(loop));
    }
    tilly::vector<Face> old,keep;
    for(auto &f:s.faces) {
        auto fp=plane(s,f);
        if(std::abs(glm::dot(fp.origin-p.origin,p.normal))<=Tolerances::plane && std::abs(glm::dot(fp.normal,p.normal))>1-1e-8) old.push_back(f);
        else keep.push_back(f);
    }
    tilly::vector<bool> used(old.size(),false);
    for(auto &loop:bounded) {
        bool existing_hole=false;
        for(auto &f:old)for(auto h:f.holes){std::reverse(h.begin(),h.end());existing_hole=existing_hole || same_loop(h,loop);}
        if(existing_hole)continue; // unrelated lines must not refill through-cuts
        Face f;f.outer=loop;
        // Immediate contained regions become holes, while also remaining their
        // own editable face: drawing an inner ring partitions, not erases, a face.
        auto outer=projected(s,loop,p);
        for(auto &inner:bounded) if(&inner!=&loop) {
            auto q=projected(s,inner,p);if(!inside(q[0],outer)) continue;
            bool immediate=true;
            for(auto &middle:bounded) if(&middle!=&inner && &middle!=&loop) {
                auto mid=projected(s,middle,p);if(inside(mid[0],outer) && inside(q[0],mid)) immediate=false;
            }
            if(immediate) {Loop h=inner;std::reverse(h.begin(),h.end());f.holes.push_back(std::move(h));}
        }
        size_t match=old.size();
        for(size_t i=0;i<old.size();++i) if(!used[i] && same_loop(old[i].outer,loop)) {match=i;break;}
        if(match==old.size()) for(size_t i=0;i<old.size();++i) if(!used[i] && in_face(s,old[i],vertex(s,loop[0]).position,true)) {match=i;break;}
        if(match!=old.size()) {f.id=old[match].id;f.front=old[match].front;f.back=old[match].back;used[match]=true;
            if(glm::dot(plane(s,old[match]).normal,p.normal)<0){std::reverse(f.outer.begin(),f.outer.end());for(auto &h:f.holes)std::reverse(h.begin(),h.end());}
        }
        else f.id=d.next++;
        keep.push_back(std::move(f));
    }
    s.faces=std::move(keep);
}
Loop rectangle_loop(Document &d,Scope &s,const Plane &p,Vec a,Vec b) {
    V2 x=p.project(a),y=p.project(b);
    double x0=std::min(x.x,y.x),x1=std::max(x.x,y.x),y0=std::min(x.y,y.y),y1=std::max(x.y,y.y);
    need(x1-x0>Tolerances::weld && y1-y0>Tolerances::weld,"Rectangle width and height must exceed the welding tolerance");
    Loop loop;for(V2 q:{V2(x0,y0),V2(x1,y0),V2(x1,y1),V2(x0,y1)}) loop.push_back(add_vertex(d,s,p.point(q)));return loop;
}
void extrude(Document &d,Scope &s,Id id,double depth) {
    need(std::isfinite(depth) && std::abs(depth)>Tolerances::weld,"Push/Pull needs a nonzero finite distance");
    Face original=find(s.faces,id);auto p=plane(s,original);bool standalone=true;
    for(auto *loop:loops(original)) for(size_t i=0;i<loop->size();++i) {
        Id a=(*loop)[i],b=(*loop)[(i+1)%loop->size()];unsigned count=0;
        for(auto &f:s.faces) for(auto *l:loops(f)) for(size_t j=0;j<l->size();++j)
            if(((*l)[j]==a && (*l)[(j+1)%l->size()]==b)||((*l)[j]==b && (*l)[(j+1)%l->size()]==a)) ++count;
        if(count!=1) standalone=false;
    }
    if(!standalone) {
        validate_solid(s);
        Loop moved;for(auto *l:loops(original)) for(Id v:*l) if(std::find(moved.begin(),moved.end(),v)==moved.end()) moved.push_back(v);
        for(auto &v:s.vertices) if(std::find(moved.begin(),moved.end(),v.id)==moved.end())
            need(glm::dot(v.position-p.origin,p.normal)<std::min(0.0,depth)-Tolerances::plane,
                "Push/Pull contact is ambiguous or would cross another boundary; use a convex isolated shell cap");
        for(Id v:moved) find(s.vertices,v).position+=p.normal*depth;
        for(auto &f:s.faces) validate_face(s,f);
        validate_solid(s);return;
    }
    Face top=original,base=original;base.id=d.next++;
    auto make=[&](const Loop &bottom,Loop &upper) {
        upper.clear();for(Id v:bottom) upper.push_back(add_vertex(d,s,vertex(s,v).position+p.normal*depth));
        boundary_edges(d,s,upper);
        for(size_t i=0;i<bottom.size();++i) {
            size_t j=(i+1)%bottom.size();Loop side{bottom[i],bottom[j],upper[j],upper[i]};
            if(depth<0) std::reverse(side.begin(),side.end());
            add_face(d,s,std::move(side),original.front,original.back);
        }
    };
    make(original.outer,top.outer);for(size_t i=0;i<original.holes.size();++i) make(original.holes[i],top.holes[i]);
    auto reverse=[](Face &f){std::reverse(f.outer.begin(),f.outer.end());for(auto &h:f.holes) std::reverse(h.begin(),h.end());std::swap(f.front,f.back);};
    if(depth>0) reverse(base);else reverse(top);
    find(s.faces,id)=std::move(top);s.faces.push_back(std::move(base));
}
// Boundary door notches are a planar difference of an isolated constant-depth
// prism, not a general 3D Boolean. Reconstruct the prism and retain IDs wherever
// original vertices, edges and coplanar source regions survive.
void notch_prism(Document &d,Scope &s,const Face &front,Id opposite,const Plane &p,double depth,const Loop &cut) {
    using namespace Clipper2Lib;
    auto path=[&](const Loop &loop){Path64 out;for(Id v:loop){auto q=p.project(vertex(s,v).position);
        need(std::max(std::abs(q.x),std::abs(q.y))<1e8,"Opening exceeds the planar Boolean conversion range");
        out.emplace_back(int64_t(std::llround(q.x*1e6)),int64_t(std::llround(q.y*1e6)));}return out;};
    Paths64 subjects;for(auto *loop:loops(front))subjects.push_back(path(*loop));
    Paths64 difference=Difference(subjects,{path(cut)},FillRule::NonZero);
    unsigned outers=0;for(auto &ring:difference)outers+=Area(ring)>0;
    need(outers==1,"Door notch would collapse or separate the wall into multiple solids; reduce the profile");
    Scope result;result.id=s.id;result.vertices=s.vertices;
    Face cap;cap.id=front.id;cap.front=front.front;cap.back=front.back;
    auto loop=[&](const Path64 &ring){Loop out;for(auto q:ring)out.push_back(add_vertex(d,result,p.point({double(q.x)/1e6,double(q.y)/1e6})));return out;};
    for(auto &ring:difference)if(Area(ring)>0)cap.outer=loop(ring);else cap.holes.push_back(loop(ring));
    boundary_edges(d,result,cap.outer);for(auto &hole:cap.holes)boundary_edges(d,result,hole);result.faces.push_back(cap);
    extrude(d,result,cap.id,-depth);
    auto &back=find(result.faces,front.id);back.id=opposite;back.front=find(s.faces,opposite).front;back.back=find(s.faces,opposite).back;
    result.faces.back().id=front.id;result.faces.back().front=front.front;result.faces.back().back=front.back;
    Loop used{front.id,opposite};
    for(auto &f:result.faces)if(f.id!=front.id && f.id!=opposite){
        for(auto &source:s.faces)if(std::find(used.begin(),used.end(),source.id)==used.end()){
            bool fits=true;for(Id v:f.outer)fits=fits && in_face(s,source,vertex(result,v).position,true);
            if(fits){f.id=source.id;f.front=source.front;f.back=source.back;used.push_back(source.id);break;}
        }
    }
    for(auto &edge:result.edges)if(auto *source=edge_between(s,edge.a,edge.b))edge.id=source->id;
    result.vertices.erase(std::remove_if(result.vertices.begin(),result.vertices.end(),[&](const Vertex &v){for(auto &edge:result.edges)if(edge.a==v.id || edge.b==v.id)return false;return true;}),result.vertices.end());
    validate_solid(result);s=std::move(result);
}
void opening(Document &d,Scope &s,const Command &c) {
    need(c.points.size()==2,"Opening needs two opposite rectangle corners");validate_solid(s);
    Face front=find(s.faces,c.entity);auto p=plane(s,front);
    for(auto point:c.points) need(std::abs(glm::dot(point-p.origin,p.normal))<=Tolerances::plane,"Opening profile is not on the selected face");
    Loop cut=rectangle_loop(d,s,p,c.points[0],c.points[1]);
    auto points=projected(s,cut,p);
    bool notch=false;
    for(Id v:cut){Vec point=vertex(s,v).position;need(in_face(s,front,point,true),"Opening corners must lie inside or on the selected face");notch=notch || !in_face(s,front,point);}
    if(!notch)separate(points,projected(s,front.outer,p));
    for(auto &h:front.holes) separate(points,projected(s,h,p));
    Id opposite=0;double distance=1e100;
    for(auto &f:s.faces) if(f.id!=front.id) {
        auto other=plane(s,f);if(glm::dot(p.normal,other.normal)>-1+1e-8) continue;
        double t=glm::dot(p.origin-other.origin,other.normal)/glm::dot(p.normal,other.normal);
        // point - normal*t lies on the opposing plane.
        if(t<=Tolerances::plane || t>=distance) continue;
        bool fits=true;for(Id v:cut) fits=fits && in_face(s,f,vertex(s,v).position-p.normal*t,true);
        if(fits) {opposite=f.id;distance=t;}
    }
    need(opposite,"No unambiguous parallel opposing face; only straight-through prism openings are supported");
    auto back_plane=plane(s,find(s.faces,opposite));
    tilly::vector<V2> back_points;for(Id v:cut)back_points.push_back(back_plane.project(vertex(s,v).position-p.normal*distance));
    if(!notch)separate(back_points,projected(s,find(s.faces,opposite).outer,back_plane));
    for(auto &h:find(s.faces,opposite).holes)separate(back_points,projected(s,h,back_plane));
    for(auto &f:s.faces)if(f.id!=front.id && f.id!=opposite){
        need(std::abs(glm::dot(plane(s,f).normal,p.normal))<1e-8,
            "Through-opening requires a constant straight prism; sloped/stepped contacts are unavailable");
        for(Id v:f.outer){double t=glm::dot(vertex(s,v).position-p.origin,p.normal);
            need(t<=Tolerances::plane && t>=-distance-Tolerances::plane,"Solid extends beyond the target caps; isolate the wall first");}
    }
    if(notch){notch_prism(d,s,front,opposite,p,distance,cut);return;}
    Loop back;for(Id v:cut) back.push_back(add_vertex(d,s,vertex(s,v).position-p.normal*distance));
    // Reject obstructions between the two caps, including a previously cut tunnel.
    for(auto &f:s.faces) if(f.id!=front.id && f.id!=opposite) {
        for(Id v:f.outer) {
            Vec q=vertex(s,v).position;double t=glm::dot(q-p.origin,p.normal);
            need(!(t<-Tolerances::plane && t>-distance+Tolerances::plane && inside(p.project(q),points,true)),"Opening intersects another boundary; contact is ambiguous");
        }
    }
    Loop fh=cut;std::reverse(fh.begin(),fh.end());find(s.faces,front.id).holes.push_back(fh);
    find(s.faces,opposite).holes.push_back(back);boundary_edges(d,s,cut);boundary_edges(d,s,back);
    for(size_t i=0;i<cut.size();++i) {size_t j=(i+1)%cut.size();add_face(d,s,{cut[i],cut[j],back[j],back[i]},front.front,front.back);}
    validate_solid(s);
}
void offset(Document &d,Scope &s,const Command &c) {
    const Face original=find(s.faces,c.entity);auto p=plane(s,original);
    need(std::isfinite(c.value) && std::abs(c.value)>Tolerances::weld,"Offset distance must be finite and nonzero");
    // Translate first: 1e6 integer units/metre, bounded well below Clipper's
    // degradation/overflow range. Keep this independent from welding tolerance.
    Clipper2Lib::Paths64 paths;
    for(auto *l:loops(original)) {
        Clipper2Lib::Path64 path;
        for(Id v:*l) {V2 q=p.project(vertex(s,v).position);need(std::max(std::abs(q.x),std::abs(q.y))+std::abs(c.value)<1e8,"Offset exceeds the safe 1e6 conversion-scale range");
            path.emplace_back(int64_t(std::llround(q.x*1e6)),int64_t(std::llround(q.y*1e6)));}
        paths.push_back(std::move(path));
    }
    Clipper2Lib::ClipperOffset operation;operation.AddPaths(paths,Clipper2Lib::JoinType::Miter,Clipper2Lib::EndType::Polygon);
    Clipper2Lib::Paths64 result;operation.Execute(c.value*1e6,result);
    need(!result.empty(),"Offset collapsed the profile; reduce the inset distance");
    // An offset creates a separate profile, never silently replaces a shell cap.
    // Contour nesting and holes come from Clipper's oriented output.
    tilly::vector<Face> generated;
    for(auto &path:result) if(Clipper2Lib::Area(path)>0) {
        Face f;f.id=d.next++;f.front=original.front;f.back=original.back;
        for(auto q:path) f.outer.push_back(add_vertex(d,s,p.point({double(q.x)/1e6,double(q.y)/1e6})));
        generated.push_back(std::move(f));
    }
    for(auto &path:result) if(Clipper2Lib::Area(path)<0) {
        V2 q{double(path[0].x)/1e6,double(path[0].y)/1e6};bool assigned=false;
        for(auto &f:generated) if(inside(q,projected(s,f.outer,p))) {
            Loop h;for(auto x:path) h.push_back(add_vertex(d,s,p.point({double(x.x)/1e6,double(x.y)/1e6})));f.holes.push_back(std::move(h));assigned=true;break;
        }
        need(assigned,"Offset returned an unowned hole");
    }
    need(!generated.empty(),"Offset produced no valid outer contours");
    // Replace a standalone face only; shell offsets would need boundary surgery.
    for(auto &other:s.faces) if(other.id!=original.id) for(Id v:original.outer)
        need(std::find(other.outer.begin(),other.outer.end(),v)==other.outer.end(),"Offset of a shared shell face is unavailable; use an isolated profile");
    s.faces.erase(std::remove_if(s.faces.begin(),s.faces.end(),[&](auto &f){return f.id==original.id;}),s.faces.end());
    generated.front().id=original.id;
    for(auto &f:generated) {boundary_edges(d,s,f.outer);for(auto &h:f.holes) boundary_edges(d,s,h);s.faces.push_back(std::move(f));}
    // Old edges intentionally remain as construction topology until cleanup.
}
void remap_scope(Document &d,Scope &copy) {
    tilly::vector<std::pair<Id,Id>> mapping;
    auto allocate=[&](Id &id){Id old=id;id=d.next++;mapping.push_back({old,id});};
    allocate(copy.id);for(auto &v:copy.vertices) allocate(v.id);for(auto &e:copy.edges) allocate(e.id);
    for(auto &f:copy.faces) allocate(f.id);
    for(auto &i:copy.instances) allocate(i.id);
    auto remap=[&](Id &id){for(auto pair:mapping) if(pair.first==id){id=pair.second;return;}};
    for(auto &e:copy.edges){remap(e.a);remap(e.b);}for(auto &f:copy.faces){for(Id &v:f.outer) remap(v);for(auto &h:f.holes) for(Id &v:h) remap(v);}
}
void apply(Document &d,const Command &c) {
    if(c.op==Op::New) {Id next=d.next;d=Document{};d.next=next;return;}
    Scope &s=scope(d,c.scope);
    if(c.op==Op::Line || c.op==Op::Rectangle || c.op==Op::Polygon) {
        need(c.points.size()>=2 && c.points.size()<=512,"Drawing needs between 2 and 512 points");
        Plane p=make_plane(c.points[0],c.normal);
        for(Vec point:c.points) need(std::abs(glm::dot(point-p.origin,p.normal))<=Tolerances::plane,"Drawing points are not on the construction plane");
        tilly::vector<Vec> points=c.points;
        if(c.op==Op::Rectangle) {
            need(points.size()==2,"Rectangle needs two corners");auto loop=rectangle_loop(d,s,p,points[0],points[1]);points.clear();
            for(Id v:loop) points.push_back(vertex(s,v).position);
        }
        for(size_t i=1;i<points.size();++i) segment(d,s,points[i-1],points[i],p);
        if(c.op!=Op::Line) segment(d,s,points.back(),points.front(),p);
        regions(d,s,p);
    } else if(c.op==Op::Extrude) extrude(d,s,c.entity,c.value);
    else if(c.op==Op::Opening) opening(d,s,c);
    else if(c.op==Op::Offset) offset(d,s,c);
    else if(c.op==Op::Group) {
        need(!s.vertices.empty() || !s.instances.empty(),"There is no geometry to group");
        Scope grouped=std::move(s);Id original=grouped.id;grouped.id=d.next++;
        for(auto &dim:d.dimensions)for(auto *r:{&dim.a,&dim.b})if(r->scope==original && r->vertex)r->scope=grouped.id;
        s=Scope{};s.id=original;s.instances.push_back({d.next++,grouped.id,glm::dmat4(1),c.value!=0});d.scopes.push_back(std::move(grouped));
    } else if(c.op==Op::NewGroup) {
        Scope definition;definition.id=d.next++;
        s.instances.push_back({d.next++,definition.id,c.transform,c.value!=0});d.scopes.push_back(std::move(definition));
    } else if(c.op==Op::Instance) {
        auto instance=find(s.instances,c.entity);instance.id=d.next++;instance.component=true;find(s.instances,c.entity).component=true;
        need(c.points.size()==1,"Instance copy needs a translation");instance.transform[3]+=glm::dvec4(c.points[0],0);s.instances.push_back(instance);
    } else if(c.op==Op::Unique) {
        Id definition=find(s.instances,c.entity).definition;Scope copy=scope(d,definition);remap_scope(d,copy);
        find(s.instances,c.entity).definition=copy.id;d.scopes.push_back(std::move(copy));
    } else if(c.op==Op::Transform) find(s.instances,c.entity).transform=c.transform;
    else if(c.op==Op::TransformGeometry) {
        for(int col=0;col<4;++col)for(int row=0;row<4;++row)
            need(std::isfinite(c.transform[col][row]),"Transform must be finite");
        double determinant=glm::determinant(glm::dmat3(c.transform));
        need(std::abs(c.transform[0][3])+std::abs(c.transform[1][3])+std::abs(c.transform[2][3])+std::abs(c.transform[3][3]-1)<1e-12 && std::isfinite(determinant) && determinant>1e-12,
             "Transform must be affine, positive and invertible");
        for(Id id:entity_vertices(s,c.entity)) {
            auto &v=find(s.vertices,id);
            v.position=c.pivot+Vec(c.transform*glm::dvec4(v.position-c.pivot,1));
        }
    }
    else if(c.op==Op::MoveVertex) {need(c.points.size()==1,"Vertex move needs one point");find(s.vertices,c.entity).position=c.points[0];}
    else if(c.op==Op::Dimension) {Vec a,b;need(resolve(d,c.a,a) && resolve(d,c.b,b),"Dimension endpoints cannot be resolved");d.dimensions.push_back({d.next++,c.a,c.b});}
    else if(c.op==Op::Paint) {
        need(c.material<d.materials.size(),"Material does not exist");
        auto &f=find(s.faces,c.entity);(c.back_side?f.back:f.front)=c.material;
    }
    else if(c.op==Op::AddMaterial) {
        need(d.materials.size()<32,"The palette is limited to 32 materials");
        d.materials.push_back({c.color});
    }
    else if(c.op==Op::EditMaterial) {
        need(c.material<d.materials.size(),"Material does not exist");d.materials[c.material].color=c.color;
    }
    else if(c.op==Op::ReverseFace) {
        auto &f=find(s.faces,c.entity);std::reverse(f.outer.begin(),f.outer.end());
        for(auto &h:f.holes)std::reverse(h.begin(),h.end());
        std::swap(f.front,f.back);
    }
    else if(c.op==Op::Cleanup) {
        s.edges.erase(std::remove_if(s.edges.begin(),s.edges.end(),[&](const Edge &e){
            for(auto &f:s.faces)for(auto *l:loops(f))for(size_t i=0;i<l->size();++i)
                if(((*l)[i]==e.a && (*l)[(i+1)%l->size()]==e.b)||((*l)[i]==e.b && (*l)[(i+1)%l->size()]==e.a))return false;
            return true;
        }),s.edges.end());
        s.vertices.erase(std::remove_if(s.vertices.begin(),s.vertices.end(),[&](const Vertex &v){for(auto &edge:s.edges)if(edge.a==v.id || edge.b==v.id)return false;return true;}),s.vertices.end());
    }
    else if(c.op==Op::Delete) {
        auto vertex_at=std::find_if(s.vertices.begin(),s.vertices.end(),[&](auto &v){return v.id==c.entity;});
        if(vertex_at!=s.vertices.end()) {
            s.faces.erase(std::remove_if(s.faces.begin(),s.faces.end(),[&](auto &f){for(auto *l:loops(f))if(std::find(l->begin(),l->end(),c.entity)!=l->end())return true;return false;}),s.faces.end());
            s.edges.erase(std::remove_if(s.edges.begin(),s.edges.end(),[&](auto &e){return e.a==c.entity || e.b==c.entity;}),s.edges.end());s.vertices.erase(vertex_at);return;
        }
        // Edge deletion removes incident faces, not arbitrary neighboring edges.
        auto at=std::find_if(s.edges.begin(),s.edges.end(),[&](auto &e){return e.id==c.entity;});
        if(at!=s.edges.end()) {
            Edge e=*at;s.faces.erase(std::remove_if(s.faces.begin(),s.faces.end(),[&](auto &f){
                for(auto *l:loops(f)) for(size_t i=0;i<l->size();++i) if(((*l)[i]==e.a && (*l)[(i+1)%l->size()]==e.b)||((*l)[i]==e.b && (*l)[(i+1)%l->size()]==e.a)) return true;
                return false;
            }),s.faces.end());s.edges.erase(at);
        } else {
            bool removed=false;
            auto erase=[&](auto &items){auto n=items.size();items.erase(std::remove_if(items.begin(),items.end(),[&](auto &x){return x.id==c.entity;}),items.end());removed=removed || items.size()!=n;};
            erase(s.faces);erase(s.instances);erase(d.dimensions);need(removed,"Select a face, edge, instance or dimension to delete");
        }
    } else need(false,"Unavailable or unknown EZDesign operation");
}
size_t bytes(const Document &d) {
    size_t n=sizeof(d);for(auto &s:d.scopes) {n+=sizeof(s)+s.vertices.size()*sizeof(Vertex)+s.edges.size()*sizeof(Edge)+s.instances.size()*sizeof(Instance);
        for(auto &f:s.faces){n+=sizeof(f)+f.outer.size()*sizeof(Id);for(auto &h:f.holes)n+=sizeof(h)+h.size()*sizeof(Id);}}
    return n+d.dimensions.size()*sizeof(Dimension)+d.materials.size()*sizeof(Material);
}
Loop all_ids(const Document &d) {
    Loop ids;for(auto &s:d.scopes){ids.push_back(s.id);for(auto &v:s.vertices)ids.push_back(v.id);for(auto &e:s.edges)ids.push_back(e.id);for(auto &f:s.faces)ids.push_back(f.id);for(auto &i:s.instances)ids.push_back(i.id);}
    for(auto &dim:d.dimensions)ids.push_back(dim.id);
    std::sort(ids.begin(),ids.end());return ids;
}
Changes compare(const Document &before,const Document &after) {
    Changes changes;auto a=all_ids(before),b=all_ids(after);
    for(Id id:a){Change ch;ch.before=id;if(std::binary_search(b.begin(),b.end(),id)) ch.after.push_back(id);changes.entities.push_back(std::move(ch));}
    for(Id id:b)if(!std::binary_search(a.begin(),a.end(),id))changes.created.push_back(id);
    // Split edges map to all surviving child segments, determined geometrically.
    for(auto &s:before.scopes) for(auto &e:s.edges) {
        const Scope *target=nullptr;for(auto &t:after.scopes)if(t.id==s.id)target=&t;if(!target)continue;
        Vec a0=vertex(s,e.a).position,b0=vertex(s,e.b).position,delta=b0-a0;double n=glm::length(delta);
        for(auto &ch:changes.entities)if(ch.before==e.id) for(auto &part:target->edges)if(part.id!=e.id) {
            Vec x=vertex(*target,part.a).position-a0,y=vertex(*target,part.b).position-a0;
            double u=glm::dot(x,delta)/(n*n),v=glm::dot(y,delta)/(n*n);
            if(u>=0 && u<=1 && v>=0 && v<=1 && glm::length(glm::cross(x,delta))/n<Tolerances::weld && glm::length(glm::cross(y,delta))/n<Tolerances::weld)ch.after.push_back(part.id);
        }
    }
    for(auto &s:before.scopes)for(auto &f:s.faces){
        const Scope *target=nullptr;for(auto &t:after.scopes)if(t.id==s.id)target=&t;if(!target)continue;
        for(auto &ch:changes.entities)if(ch.before==f.id)for(auto &part:target->faces)if(part.id!=f.id){
            bool fits=true;for(Id v:part.outer)fits=fits && in_face(s,f,vertex(*target,v).position,true);
            if(fits)ch.after.push_back(part.id);
        }
    }
    return changes;
}
using MeshCache=tilly::vector<std::pair<Id,Mesh>>;
void mesh_scope(const Document &d,Id id,const glm::dmat4 &matrix,Loop &path,Mesh &out,unsigned depth,MeshCache &cache) {
    need(depth<32,"Component nesting exceeds 31 levels");auto &s=scope(d,id);
    Mesh *geometry=nullptr;
    for(auto &entry:cache)if(entry.first==id)geometry=&entry.second;
    if(!geometry) {
    Mesh local;
    for(auto &f:s.faces) {
        validate_face(s,f);
        auto p=plane(s,f);tilly::vector<tilly::vector<std::array<double,2>>> polygon;tilly::vector<Vec> positions;
        for(auto *loop:loops(f)) {
            tilly::vector<std::array<double,2>> ring;
            for(Id v:*loop) {Vec pos=vertex(s,v).position;V2 q=p.project(pos);ring.push_back({q.x,q.y});positions.push_back(pos);}
            polygon.push_back(std::move(ring));
        }
        auto indices=mapbox::earcut<uint32_t>(polygon);double triangle_area=0;
        for(size_t i=0;i<indices.size();i+=3) {
            Triangle t;t.face=f.id;t.scope=s.id;t.material=f.front;t.back_material=f.back;
            Vec a=positions[indices[i]],b=positions[indices[i+1]],c=positions[indices[i+2]];
            if(glm::dot(glm::cross(b-a,c-a),p.normal)<0)std::swap(b,c);
            triangle_area+=glm::length(glm::cross(b-a,c-a))*.5;
            t.points={a,b,c};local.triangles.push_back(std::move(t));
        }
        need(std::abs(triangle_area-area(s,f))<=std::max(1e-10,area(s,f)*1e-8),"Triangulation does not cover the face; inspect its boundaries");
    }
    cache.push_back({id,std::move(local)});geometry=&cache.back().second;
    }
    for(auto &source:geometry->triangles){
        Triangle t=source;t.path=path;for(auto &p:t.points)p=Vec(matrix*glm::dvec4(p,1));out.triangles.push_back(std::move(t));
        need(out.triangles.size()<=131072,"Derived mesh exceeds the 131072 triangle budget");
    }
    for(auto &instance:s.instances){path.push_back(instance.id);mesh_scope(d,instance.definition,matrix*instance.transform,path,out,depth+1,cache);path.pop_back();}
}
} // namespace

const Scope &scope(const Document &d,Id id){return find(d.scopes,id);}
Scope &scope(Document &d,Id id){return find(d.scopes,id);}
const Vertex &vertex(const Scope &s,Id id){return find(s.vertices,id);}
Loop entity_vertices(const Scope &s,Id id) {
    for(auto &v:s.vertices)if(v.id==id)return {id};
    for(auto &e:s.edges)if(e.id==id)return {e.a,e.b};
    const auto &f=face(s,id);Loop ids=f.outer;
    for(auto &h:f.holes)ids.insert(ids.end(),h.begin(),h.end());
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    return ids;
}
const Face &face(const Scope &s,Id id){return find(s.faces,id);}
Plane plane(const Scope &s,const Face &f) {
    need(f.outer.size()>=3,"Face has fewer than three vertices");Vec origin=vertex(s,f.outer[0]).position,normal(0);
    for(size_t i=1;i+1<f.outer.size();++i)normal+=glm::cross(vertex(s,f.outer[i]).position-origin,vertex(s,f.outer[i+1]).position-origin);
    need(glm::length(normal)>Tolerances::weld*Tolerances::weld,"Face area is below the geometry tolerance");return make_plane(origin,normal);
}
double area(const Scope &s,const Face &f){auto p=plane(s,f);double a=signed_area(projected(s,f.outer,p));for(auto &h:f.holes)a+=signed_area(projected(s,h,p));return a;}
void validate(const Document &d) {
    need(!d.scopes.empty() && d.scopes.size()<=128 && d.scopes[0].id==1,"Document must contain root scope 1 (maximum 128 scopes)");
    auto ids=all_ids(d);need(ids.size()<=max_entities && d.next>ids.back() && d.next<UINT64_MAX/2,"Entity budget or persistent ID range exceeded");
    need(ids.front()!=0 && std::adjacent_find(ids.begin(),ids.end())==ids.end(),"Persistent IDs must be globally unique and nonzero");
    need(d.materials.size()>=1 && d.materials.size()<=32 && d.units<=2,"Invalid materials or unit format");
    for(auto &m:d.materials)need(finite(m.color) && m.color.x>=0 && m.color.y>=0 && m.color.z>=0 && m.color.x<=1 && m.color.y<=1 && m.color.z<=1,"Invalid material color");
    need(finite(d.view.target) && std::isfinite(d.view.yaw) && std::isfinite(d.view.pitch) && std::abs(d.view.pitch)<=90 && std::isfinite(d.view.distance) && d.view.distance>=.001 && d.view.distance<=1e8,"Invalid saved camera");
    for(auto &s:d.scopes) {
        need(s.instances.size()<=512,"A scope may contain at most 512 instances");
        for(size_t i=0;i<s.vertices.size();++i){need(finite(s.vertices[i].position),"Invalid vertex coordinate");for(size_t j=0;j<i;++j)need(!near(s.vertices[i].position,s.vertices[j].position),"Coincident vertices need welding");}
        for(size_t i=0;i<s.edges.size();++i){auto &e=s.edges[i];need(!near(vertex(s,e.a).position,vertex(s,e.b).position),"Zero-length edge");
            for(size_t j=0;j<i;++j)need(!((e.a==s.edges[j].a && e.b==s.edges[j].b)||(e.a==s.edges[j].b && e.b==s.edges[j].a)),"Duplicate edge");}
        for(auto &f:s.faces){need(f.front<d.materials.size() && f.back<d.materials.size(),"Invalid face material");validate_face(s,f);}
        for(auto &i:s.instances){(void)scope(d,i.definition);for(int col=0;col<4;++col)for(int row=0;row<4;++row)need(std::isfinite(i.transform[col][row]) && std::abs(i.transform[col][row])<=Tolerances::coordinate_limit,"Instance transform exceeds the finite coordinate range");
            double determinant=glm::determinant(glm::dmat3(i.transform));
            need(std::abs(i.transform[0][3])+std::abs(i.transform[1][3])+std::abs(i.transform[2][3])+std::abs(i.transform[3][3]-1)<1e-12 && std::isfinite(determinant) && determinant>1e-12,"Instance transform must be affine, positive and invertible");}
    }
    size_t expanded=0,triangles=0;
    auto visit=[&](auto &&self,Id id,Loop &path,const glm::dmat4 &matrix)->void {
        need(++expanded<=8192,"Expanded component hierarchy exceeds the traversal budget");
        need(path.size()<32 && std::find(path.begin(),path.end(),id)==path.end(),"Cyclic or excessively nested component definition");
        auto &s=scope(d,id);
        for(auto &v:s.vertices)need(finite(Vec(matrix*glm::dvec4(v.position,1))),"Nested instance geometry exceeds the finite coordinate range");
        for(auto &edge:s.edges)need(!near(Vec(matrix*glm::dvec4(vertex(s,edge.a).position,1)),Vec(matrix*glm::dvec4(vertex(s,edge.b).position,1))),"Instance scale collapses an edge below the geometry tolerance");
        for(auto &f:s.faces){triangles+=f.outer.size()+2*f.holes.size()-2;for(auto &h:f.holes)triangles+=h.size();}
        need(triangles<=131072,"Expanded components exceed the 131072 triangle budget");
        path.push_back(id);for(auto &i:s.instances)self(self,i.definition,path,matrix*i.transform);path.pop_back();
    };
    for(auto &s:d.scopes){Loop path;triangles=0;visit(visit,s.id,path,glm::dmat4(1));}
    for(auto &dim:d.dimensions)for(auto &r:{dim.a,dim.b})need(finite(r.point),"Invalid dimension fallback point");
    need(bytes(d)<=max_bytes,"Document exceeds the 16 MiB editable geometry budget");
}
void validate_solid(const Scope &s) {
    need(!s.faces.empty(),"Solid operation needs faces");
    for(auto &f:s.faces)validate_face(s,f);
    for(auto &e:s.edges) {
        unsigned count=0;int direction=0;
        for(auto &f:s.faces)for(auto *l:loops(f))for(size_t j=0;j<l->size();++j){Id a=(*l)[j],b=(*l)[(j+1)%l->size()];if(a==e.a && b==e.b){++count;++direction;}if(a==e.b && b==e.a){++count;--direction;}}
        need(count==2 && direction==0,"Solid operation needs closed, oppositely oriented two-face edge incidence; remove stray edges first");
    }
    Vec origin=vertex(s,s.faces[0].outer[0]).position;double volume=0;
    for(auto &f:s.faces)for(auto *l:loops(f))for(size_t i=1;i+1<l->size();++i)volume+=glm::dot(vertex(s,(*l)[0]).position-origin,glm::cross(vertex(s,(*l)[i]).position-origin,vertex(s,(*l)[i+1]).position-origin))/6;
    need(volume>Tolerances::weld*Tolerances::weld*Tolerances::weld,"Solid has reversed orientation or zero volume");
}
Mesh triangulate(const Document &d,Id active_scope){Mesh out;Loop path;MeshCache cache;mesh_scope(d,active_scope,glm::dmat4(1),path,out,0,cache);return out;}
bool resolve(const Document &d,const Reference &r,Vec &p) {
    if(!r.vertex){p=r.point;return finite(p);}for(auto &s:d.scopes)if(s.id==r.scope)for(auto &v:s.vertices)if(v.id==r.vertex){p=v.position;return true;}return false;
}
bool parse_length(const char *text,unsigned units,double &metres) noexcept {
    if(!text || std::strlen(text)>128 || units>2)return false;
    char *end=nullptr;double n=std::strtod(text,&end);
    if(end==text || !std::isfinite(n))return false;
    while(*end==' ')++end;
    double factor=units==1?.001:units==2?.3048:1;
    if(*end){if(!std::strcmp(end,"m"))factor=1;else if(!std::strcmp(end,"mm"))factor=.001;else if(!std::strcmp(end,"cm"))factor=.01;else if(!std::strcmp(end,"ft") || !std::strcmp(end,"'"))factor=.3048;else if(!std::strcmp(end,"in") || !std::strcmp(end,"\""))factor=.0254;else return false;}
    n*=factor;if(!std::isfinite(n) || std::abs(n)>Tolerances::coordinate_limit)return false;metres=n;return true;
}
bool Editor::publish(Document &&next) noexcept try {
    validate(next);Changes delta=compare(doc,next);past.push_back(doc);
    size_t total=0;for(auto &d:past)total+=bytes(d);
    while(past.size()>32 || (total>32*1024*1024 && past.size()>1)){total-=bytes(past.front());past.erase(past.begin());}
    future.clear();doc=std::move(next);changes=std::move(delta);high_water=std::max(high_water,doc.next);++revision;error[0]=0;return true;
} catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::preview(const Command &c,Document &out) noexcept try {
    Document next=doc;next.next=std::max(next.next,high_water);apply(next,c);validate(next);out=std::move(next);error[0]=0;return true;
} catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::execute(const Command &c) noexcept try {Document next;if(!preview(c,next))return false;return publish(std::move(next));}
catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::undo() noexcept try {
    if(past.empty())return false;
    Changes delta=compare(doc,past.back());future.push_back(doc);doc=std::move(past.back());past.pop_back();
    changes=std::move(delta);doc.next=std::max(doc.next,high_water);++revision;error[0]=0;return true;
} catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::redo() noexcept try {
    if(future.empty())return false;
    Changes delta=compare(doc,future.back());past.push_back(doc);doc=std::move(future.back());future.pop_back();
    changes=std::move(delta);doc.next=std::max(doc.next,high_water);++revision;error[0]=0;return true;
} catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}

namespace {
tilly::string encode(const Document &d) {
    Output out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<"ezdesign 1\n"<<d.next<<' '<<d.units<<' '<<d.scopes.size()<<' '<<d.materials.size()<<' '<<d.dimensions.size()<<'\n';
    auto xyz=[&](Vec v){out<<v.x<<' '<<v.y<<' '<<v.z<<' ';};
    xyz(d.view.target);out<<d.view.yaw<<' '<<d.view.pitch<<' '<<d.view.distance<<' '<<d.view.orthographic<<'\n';
    for(auto &m:d.materials){xyz(m.color);out<<'\n';}
    for(auto &s:d.scopes){out<<s.id<<' '<<s.vertices.size()<<' '<<s.edges.size()<<' '<<s.faces.size()<<' '<<s.instances.size()<<'\n';
        for(auto &v:s.vertices){out<<v.id<<' ';xyz(v.position);out<<'\n';}
        for(auto &e:s.edges)out<<e.id<<' '<<e.a<<' '<<e.b<<'\n';
        for(auto &f:s.faces){out<<f.id<<' '<<f.front<<' '<<f.back<<' '<<f.holes.size()<<'\n';for(auto *l:loops(f)){out<<l->size();for(Id v:*l)out<<' '<<v;out<<'\n';}}
        for(auto &i:s.instances){out<<i.id<<' '<<i.definition<<' '<<i.component;for(int c=0;c<4;++c)for(int r=0;r<4;++r)out<<' '<<i.transform[c][r];out<<'\n';}
    }
    for(auto &dim:d.dimensions){out<<dim.id<<' ';for(auto &r:{dim.a,dim.b}){out<<r.scope<<' '<<r.vertex<<' ';xyz(r.point);}out<<'\n';}
    need(bool(out),"Could not serialize document");return out.str();
}
bool atomic_write(const char *path,const tilly::string &text) {
    need(path && *path && std::strlen(path)<4096,"Invalid output path");tilly::string temp=tilly::string(path)+".ezdesign-tmp";
    FILE *f=std::fopen(temp.c_str(),"wbx");need(f,"Could not create exclusive temporary file (check path and recovery file)");
    bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();if(std::fclose(f)!=0)ok=false;
#ifdef _WIN32
    if(ok)ok=MoveFileExA(temp.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok)ok=std::rename(temp.c_str(),path)==0;
#endif
    if(!ok)std::remove(temp.c_str());
    need(ok,"Could not atomically publish file; previous document is unchanged");return true;
}
Document decode(const tilly::string &text) {
    Input in(text);in.imbue(std::locale::classic());tilly::string magic;unsigned version=0;Document d;size_t ns=0,nm=0,nd=0,total=0;
    need(bool(in>>magic>>version) && magic=="ezdesign" && version==1,"Unsupported EZDesign document version");
    need(bool(in>>d.next>>d.units>>ns>>nm>>nd) && ns>0 && ns<=128 && nm>0 && nm<=32 && nd<=max_entities,"Invalid document counts");
    auto xyz=[&](Vec &p){need(bool(in>>p.x>>p.y>>p.z) && finite(p),"Invalid coordinate in document");};
    xyz(d.view.target);need(bool(in>>d.view.yaw>>d.view.pitch>>d.view.distance>>d.view.orthographic),"Invalid saved view");
    d.materials.resize(nm);for(auto &m:d.materials)xyz(m.color);d.scopes.clear();
    for(size_t index=0;index<ns;++index){Scope s;size_t nv=0,ne=0,nf=0,ni=0;
        need(bool(in>>s.id>>nv>>ne>>nf>>ni) && nv<=max_entities && ne<=max_entities && nf<=max_entities && ni<=512,"Invalid scope counts");
        total+=nv+ne+nf+ni;need(total<=max_entities,"Document entity budget exceeded");
        s.vertices.resize(nv);for(auto &v:s.vertices){need(bool(in>>v.id),"Missing vertex ID");xyz(v.position);}
        s.edges.resize(ne);for(auto &e:s.edges)need(bool(in>>e.id>>e.a>>e.b),"Invalid edge record");
        s.faces.resize(nf);for(auto &f:s.faces){size_t holes=0;need(bool(in>>f.id>>f.front>>f.back>>holes) && holes<=512,"Invalid face record");f.holes.resize(holes);
            auto read_loop=[&](Loop &l){size_t n=0;need(bool(in>>n) && n>=3 && n<=max_entities,"Invalid boundary count");total+=n;need(total<=max_entities*8,"Boundary budget exceeded");l.resize(n);for(Id &v:l)need(bool(in>>v),"Missing boundary vertex");};read_loop(f.outer);for(auto &h:f.holes)read_loop(h);
        }
        s.instances.resize(ni);for(auto &i:s.instances){need(bool(in>>i.id>>i.definition>>i.component),"Invalid instance record");for(int c=0;c<4;++c)for(int r=0;r<4;++r)need(bool(in>>i.transform[c][r]),"Invalid transform record");}
        d.scopes.push_back(std::move(s));
    }
    d.dimensions.resize(nd);for(auto &dim:d.dimensions){need(bool(in>>dim.id),"Missing dimension ID");for(auto *r:{&dim.a,&dim.b}){need(bool(in>>r->scope>>r->vertex),"Invalid dimension reference");xyz(r->point);}}
    tilly::string extra;need(!(in>>extra),"Unexpected trailing data in EZDesign document");validate(d);return d;
}
} // namespace
bool Editor::save(const char *path) noexcept try {validate(doc);bool ok=atomic_write(path,encode(doc));error[0]=0;return ok;}
catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::load(const char *path) noexcept try {
    need(path && *path,"Invalid input path");std::ifstream file(path,std::ios::binary);need(bool(file),"Could not open EZDesign project");
    file.seekg(0,std::ios::end);auto n=file.tellg();need(n>0 && n<=std::streamoff(max_bytes),"Invalid project size (maximum 16 MiB)");file.seekg(0);
    tilly::string text(size_t(n),'\0');file.read(text.data(),n);need(bool(file) && !std::memchr(text.data(),0,text.size()),"Truncated or invalid text project");
    Document next=decode(text);high_water=std::max(high_water,next.next);next.next=high_water;return publish(std::move(next));
}catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::export_obj(const char *path,Id active_scope) noexcept try {
    validate(doc);auto mesh=triangulate(doc,active_scope);Output out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<"# JoltFX EZDesign mesh; metres; editable edges remain in the native .jfx\n";
    size_t index=1;for(auto &t:mesh.triangles){out<<"g face_"<<t.face<<"\n";for(Vec p:t.points)out<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';out<<"f "<<index<<' '<<index+1<<' '<<index+2<<'\n';index+=3;}
    bool ok=atomic_write(path,out.str());error[0]=0;return ok;
}catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
bool Editor::export_ply(const char *path,Id active_scope) noexcept try {
    validate(doc);auto mesh=triangulate(doc,active_scope);Output out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"ply\nformat ascii 1.0\ncomment JoltFX EZDesign; metres; flattened mesh\nelement vertex "<<mesh.triangles.size()*3
       <<"\nproperty double x\nproperty double y\nproperty double z\nelement face "<<mesh.triangles.size()
       <<"\nproperty list uchar uint vertex_indices\nend_header\n";
    for(auto &t:mesh.triangles)for(Vec p:t.points)out<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
    size_t index=0;for(size_t i=0;i<mesh.triangles.size();++i){out<<"3 "<<index<<' '<<index+1<<' '<<index+2<<'\n';index+=3;}
    bool ok=atomic_write(path,out.str());error[0]=0;return ok;
}catch(const std::exception &e){std::snprintf(error,sizeof(error),"%s",e.what());return false;}
} // namespace jfx::ez
