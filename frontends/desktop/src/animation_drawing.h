#ifndef JFX_DESKTOP_ANIMATION_DRAWING_H
#define JFX_DESKTOP_ANIMATION_DRAWING_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <charconv>
#include <string_view>
#include <cctype>
#include <stdexcept>
#include "tilly/memory.hpp"
#include "../../../ingagi/animation/geometry_bridge.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Desktop drawing documents are independent of skeletal JFA1 bytecode and NLE
// projects. Coordinates are in a 960 x 540 stage; cels hold until the next key.
namespace jfx_drawing {
struct Point { float x=0, y=0; };
enum class Kind { Brush, Line, Rectangle, Ellipse, Polygon };
struct Shape {
    Kind kind=Kind::Brush;
    tilly::vector<Point> points;
    uint32_t stroke=0xff302820u, fill=0xffe0a050u;
    float width=3;
    bool filled=true;
};
struct Cel { int frame=1; tilly::vector<Shape> shapes; };
struct Layer { bool visible=true, locked=false; tilly::vector<Cel> cels{Cel{}}; };
struct Document { int frames=120, fps=24; tilly::vector<Layer> layers{Layer{}}; };
struct Reader {
    std::string_view remaining;
    std::string_view token() {
        while(!remaining.empty() && std::isspace(static_cast<unsigned char>(remaining.front()))) remaining.remove_prefix(1);
        size_t n=0; while(n<remaining.size() && !std::isspace(static_cast<unsigned char>(remaining[n]))) ++n;
        auto t=remaining.substr(0,n); remaining.remove_prefix(n); return t;
    }
    template<class T> bool number(T &out) {
        auto t=token(); if(t.empty()) return false;
        auto r=std::from_chars(t.data(),t.data()+t.size(),out);
        return r.ec==std::errc{} && r.ptr==t.data()+t.size();
    }
};
struct Editor {
    Document doc;
    tilly::vector<Document> past, future;
    int frame=1, layer=0, selected=-1, tool=1;
    float stroke[4]={0.12f,0.16f,0.20f,1}, fill[4]={0.31f,0.63f,0.88f,1};
    float width=3, zoom=1;
    bool filled=true, onion=true, grid=false, playing=false, dragging=false, files_requested=false, allocation_failed=false;
    double elapsed=0;
    Point pan{}, last{};
    Shape pending;
    char path[512]="animation.jfxdraw", svg_path[512]="animation.svg";
    char script[16384]="import std::animation2d as a;\nfn frame(t: f64) {\n    let x = a::tween(120, 840, a::pingpong(t, 2.5), \"smooth\");\n    return [a::ellipse(vec2(x-40,230), vec2(x+40,310),\n        a::rgba(0.2,0.6,0.95,1), a::rgba(0.1,0.15,0.2,1), 3)];\n}\n";
    char script_path[512]="animation.ing", html_path[512]="animation.html";
    const Cel &exposure(const Layer &l,int at) const {
        const Cel *out=&l.cels.front();
        for (const auto &c:l.cels) if (c.frame<=at && c.frame>=out->frame) out=&c;
        return *out;
    }
    Cel &cel() {
        auto &l=doc.layers[(size_t)layer];
        return l.cels[(size_t)(&exposure(l,frame)-l.cels.data())];
    }
    bool editable() const { const auto &l=doc.layers[(size_t)layer]; return l.visible && !l.locked && !playing; }
    bool checkpoint() noexcept {
        try { past.push_back(doc); if(past.size()>32) past.erase(past.begin()); future.clear(); return true; }
        catch (...) { allocation_failed=true; return false; }
    }
    void reset_selection() { layer=std::min(layer,(int)doc.layers.size()-1); frame=std::min(frame,doc.frames); selected=-1; dragging=false; playing=false; }
    void undo() noexcept { try { if(past.empty()) return; future.push_back(doc); doc=std::move(past.back()); past.pop_back(); reset_selection(); } catch (...) { allocation_failed=true; } }
    void redo() noexcept { try { if(future.empty()) return; past.push_back(doc); doc=std::move(future.back()); future.pop_back(); reset_selection(); } catch (...) { allocation_failed=true; } }
    void key(bool blank) noexcept { try {
        if(!editable()) return;
        auto &l=doc.layers[(size_t)layer];
        for(auto &c:l.cels) if(c.frame==frame) {
            if(blank && !c.shapes.empty() && checkpoint()) { c.shapes.clear(); selected=-1; }
            return;
        }
        Cel next{frame,blank?tilly::vector<Shape>{}:cel().shapes};
        l.cels.reserve(l.cels.size()+1); if(!checkpoint()) return;
        l.cels.push_back(std::move(next)); std::sort(l.cels.begin(),l.cels.end(),[](const Cel&a,const Cel&b){return a.frame<b.frame;}); selected=-1;
    } catch (...) { allocation_failed=true; } }
    void add_layer() noexcept { try {
        Layer next; doc.layers.reserve(doc.layers.size()+1);
        if(!checkpoint()) return;
        doc.layers.push_back(std::move(next)); layer=(int)doc.layers.size()-1; selected=-1;
    } catch (...) { allocation_failed=true; } }
    bool begin_shape(Point p) noexcept { try {
        // Reserve the entire bounded stroke before opening a gesture. UI input
        // can then append points without throwing through ImGui scopes.
        pending.points.reserve(2048); pending.points={p,p}; dragging=true; return true;
    } catch (...) { allocation_failed=true; dragging=false; return false; } }
    void commit_shape() noexcept { try {
        auto &shapes=cel().shapes; shapes.reserve(shapes.size()+1);
        if(!checkpoint()) return;
        shapes.push_back(std::move(pending)); selected=(int)shapes.size()-1;
    } catch (...) { allocation_failed=true; } }
    static float distance(Point p,Point a,Point b) {
        return static_cast<float>(ig_anim_segment_distance(double(p.x),double(p.y),double(a.x),double(a.y),double(b.x),double(b.y)));
    }
    int hit(Point p) {
        auto &shapes=cel().shapes;
        for(size_t n=shapes.size();n>0;--n) {
            auto &s=shapes[n-1]; if(s.points.empty()) continue;
            float tolerance=std::max(5.0f,s.width/2);
            if(s.kind==Kind::Rectangle || s.kind==Kind::Ellipse) {
                Point a=s.points.front(),b=s.points.back();
                float x0=std::min(a.x,b.x),x1=std::max(a.x,b.x),y0=std::min(a.y,b.y),y1=std::max(a.y,b.y);
                if(s.kind==Kind::Rectangle) {
                    if(p.x>=x0-tolerance && p.x<=x1+tolerance && p.y>=y0-tolerance && p.y<=y1+tolerance &&
                       (s.filled || std::min({std::abs(p.x-x0),std::abs(p.x-x1),std::abs(p.y-y0),std::abs(p.y-y1)})<=tolerance)) return (int)n-1;
                } else {
                    float rx=std::max(0.1f,(x1-x0)/2),ry=std::max(0.1f,(y1-y0)/2);
                    float r=std::hypot((p.x-(x0+x1)/2)/rx,(p.y-(y0+y1)/2)/ry);
                    float t=tolerance/std::min(rx,ry);
                    if(r<=1+t && (s.filled || r>=1-t)) return (int)n-1;
                }
            } else {
                for(size_t i=1;i<s.points.size();++i) if(distance(p,s.points[i-1],s.points[i])<=tolerance) return (int)n-1;
                if(s.kind==Kind::Polygon) {
                    if(distance(p,s.points.back(),s.points.front())<=tolerance) return (int)n-1;
                    bool inside=false;
                    for(size_t i=0,j=s.points.size()-1;i<s.points.size();j=i++) {
                        auto a=s.points[i],b=s.points[j];
                        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
                    }
                    if(s.filled && inside) return (int)n-1;
                }
            }
        }
        return -1;
    }
    // Bounded, versioned text format. Load validates a temporary document before
    // replacing the current one, so malformed files never erase artwork.
    tilly::string serialize() const {
        tilly::string out;char line[256];
        auto append=[&](int n) {if(n<=0 || size_t(n)>=sizeof(line)) throw std::runtime_error("drawing serialization failed");out.append(line,size_t(n));};
        append(std::snprintf(line,sizeof(line),"JFXDRAW2 %d %d %zu\n",doc.frames,doc.fps,doc.layers.size()));
        for(const auto &l:doc.layers) {
            append(std::snprintf(line,sizeof(line),"%d %d %zu\n",l.visible?1:0,l.locked?1:0,l.cels.size()));
            for(const auto &c:l.cels) {
                append(std::snprintf(line,sizeof(line),"%d %zu\n",c.frame,c.shapes.size()));
                for(const auto &s:c.shapes) {
                    append(std::snprintf(line,sizeof(line),"%d %u %u %.9g %d %zu\n",(int)s.kind,s.stroke,s.fill,(double)s.width,s.filled?1:0,s.points.size()));
                    for(auto p:s.points) append(std::snprintf(line,sizeof(line),"%.9g %.9g\n",(double)p.x,(double)p.y));
                }
            }
        }
        return out;
    }
    static bool write_file(const char *name,const char *data,size_t size) noexcept try {
        if(!name) return false;
        tilly::string temporary=tilly::string(name)+".tmp";
        FILE *f=std::fopen(temporary.c_str(),"wb"); if(!f) return false;
        bool ok=std::fwrite(data,1,size,f)==size; ok=std::fclose(f)==0 && ok;
#ifdef _WIN32
        if(ok) ok=MoveFileExA(temporary.c_str(),name,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        if(ok) ok=std::rename(temporary.c_str(),name)==0;
#endif
        if(!ok) std::remove(temporary.c_str());
        return ok;
    } catch (...) { return false; }
    bool save(const char *name) const try {auto data=serialize();return write_file(name,data.data(),data.size());} catch (...) {return false;}
    static bool read_file(const char *name,tilly::string &out,size_t limit) {
        FILE *f=std::fopen(name,"rb");if(!f)return false;
        auto close=[](FILE *p){std::fclose(p);};std::unique_ptr<FILE,decltype(close)> file(f,close);
        tilly::string data;char block[4096];size_t n;
        while((n=std::fread(block,1,sizeof(block),f))) {if(n>limit-data.size())return false;data.append(block,n);}
        if(std::ferror(f)||data.find('\0')!=tilly::string::npos)return false;
        out=std::move(data);return true;
    }
    bool load(const char *name) try {
        if(!name) return false;
        tilly::string data;if(!read_file(name,data,64*1024*1024))return false;
        return load_data(data.data(),data.size());
    } catch (...) { return false; }
    bool load_data(const char *data,size_t size) noexcept try {
        if(!data||size>64*1024*1024)return false;
        Reader r{std::string_view(data,size)};auto magic=r.token();
        Document next; next.layers.clear(); unsigned count=0; size_t total=0;
        bool ok=r.number(next.frames)&&r.number(next.fps)&&r.number(count);
        ok=ok && (magic=="JFXDRAW1"||magic=="JFXDRAW2") && next.frames>=1 && next.frames<=240 && next.fps>=1 && next.fps<=60 && count>=1 && count<=8;
        for(unsigned i=0;ok && i<count;++i) {
            Layer l; l.cels.clear(); int visible=0,locked=0; unsigned keys=0;
            ok=r.number(visible)&&r.number(locked)&&r.number(keys) && (visible==0 || visible==1) && (locked==0 || locked==1) && keys>=1 && keys<=(unsigned)next.frames;
            l.visible=visible!=0; l.locked=locked!=0;
            for(unsigned k=0;ok && k<keys;++k) {
                Cel c; unsigned shapes=0;
                ok=r.number(c.frame)&&r.number(shapes) && c.frame>=1 && c.frame<=next.frames && shapes<=256 && (k?c.frame>l.cels.back().frame:c.frame==1);
                for(unsigned j=0;ok && j<shapes;++j) {
                    Shape s; int kind=0,filled_value=0; unsigned points=0;
                    ok=r.number(kind)&&r.number(s.stroke)&&r.number(s.fill)&&r.number(s.width)&&r.number(filled_value)&&r.number(points);
                    ok=ok && kind>=0 && kind<=(magic=="JFXDRAW2"?4:3) && std::isfinite(s.width) && s.width>=0.5f && s.width<=40 && (filled_value==0 || filled_value==1) && points>=2 && points<=2048 && (kind==0 || (kind==4?points>=3:points==2));
                    total+=points; ok=ok && total<=1000000; s.kind=(Kind)kind; s.filled=filled_value!=0;
                    for(unsigned p=0;ok && p<points;++p) {
                        Point v; ok=r.number(v.x)&&r.number(v.y) && std::isfinite(v.x) && std::isfinite(v.y) && std::abs(v.x)<=100000 && std::abs(v.y)<=100000;
                        if(ok) s.points.push_back(v);
                    }
                    if(ok) c.shapes.push_back(s);
                }
                if(ok) l.cels.push_back(c);
            }
            if(ok) next.layers.push_back(l);
        }
        if(ok) ok=r.token().empty();
        if(!ok) return false;
        if(!checkpoint()) return false;
        doc=std::move(next); reset_selection(); return true;
    } catch (...) { return false; }
    bool export_svg(const char *name) const {
        if(!name) return false;
        FILE *f=std::fopen(name,"wb"); if(!f) return false;
        std::fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"540\" viewBox=\"0 0 960 540\">\n",f);
        auto rgb=[](uint32_t c) {return ((c&255u)<<16)|(c&0xff00u)|((c>>16)&255u);};
        for(const auto &l:doc.layers) if(l.visible) for(const auto &s:exposure(l,frame).shapes) {
            if(s.points.size()<2) continue;
            Point a=s.points.front(),b=s.points.back();
            if(s.kind==Kind::Rectangle) std::fprintf(f,"<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"",(double)std::min(a.x,b.x),(double)std::min(a.y,b.y),(double)std::abs(b.x-a.x),(double)std::abs(b.y-a.y));
            else if(s.kind==Kind::Ellipse) std::fprintf(f,"<ellipse cx=\"%g\" cy=\"%g\" rx=\"%g\" ry=\"%g\"",(double)(a.x+b.x)/2,(double)(a.y+b.y)/2,(double)std::abs(b.x-a.x)/2,(double)std::abs(b.y-a.y)/2);
            else { std::fputs(s.kind==Kind::Polygon?"<polygon points=\"":"<polyline points=\"",f); for(auto p:s.points) std::fprintf(f,"%g,%g ",(double)p.x,(double)p.y); std::fputs("\"",f); }
            std::fprintf(f," stroke=\"#%06x\" stroke-opacity=\"%g\" stroke-width=\"%g\" stroke-linecap=\"round\" stroke-linejoin=\"round\"",rgb(s.stroke),(double)(s.stroke>>24)/255,(double)s.width);
            if(s.filled && (s.kind==Kind::Rectangle || s.kind==Kind::Ellipse || s.kind==Kind::Polygon)) std::fprintf(f," fill=\"#%06x\" fill-opacity=\"%g\"",rgb(s.fill),(double)(s.fill>>24)/255);
            else std::fputs(" fill=\"none\"",f);
            std::fputs("/>\n",f);
        }
        std::fputs("</svg>\n",f); bool ok=!std::ferror(f); return std::fclose(f)==0 && ok;
    }
};
} // namespace jfx_drawing
#endif
