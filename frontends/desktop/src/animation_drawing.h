#ifndef JFX_DESKTOP_ANIMATION_DRAWING_H
#define JFX_DESKTOP_ANIMATION_DRAWING_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
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
enum class Kind { Brush, Line, Rectangle, Ellipse };
struct Shape {
    Kind kind=Kind::Brush;
    std::vector<Point> points;
    uint32_t stroke=0xff302820u, fill=0xffe0a050u;
    float width=3;
    bool filled=true;
};
struct Cel { int frame=1; std::vector<Shape> shapes; };
struct Layer { bool visible=true, locked=false; std::vector<Cel> cels{Cel{}}; };
struct Document { int frames=120, fps=24; std::vector<Layer> layers{Layer{}}; };
struct Editor {
    Document doc;
    std::vector<Document> past, future;
    int frame=1, layer=0, selected=-1, tool=1;
    float stroke[4]={0.12f,0.16f,0.20f,1}, fill[4]={0.31f,0.63f,0.88f,1};
    float width=3, zoom=1;
    bool filled=true, onion=true, grid=false, playing=false, dragging=false, files_requested=false;
    double elapsed=0;
    Point pan{}, last{};
    Shape pending;
    char path[512]="animation.jfxdraw", svg_path[512]="animation.svg";
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
    void checkpoint() { if(past.size()==32) past.erase(past.begin()); past.push_back(doc); future.clear(); }
    void reset_selection() { layer=std::min(layer,(int)doc.layers.size()-1); frame=std::min(frame,doc.frames); selected=-1; dragging=false; playing=false; }
    void undo() { if(past.empty()) return; future.push_back(doc); doc=past.back(); past.pop_back(); reset_selection(); }
    void redo() { if(future.empty()) return; past.push_back(doc); doc=future.back(); future.pop_back(); reset_selection(); }
    void key(bool blank) {
        if(!editable()) return;
        auto &l=doc.layers[(size_t)layer];
        for(auto &c:l.cels) if(c.frame==frame) {
            if(blank && !c.shapes.empty()) { checkpoint(); c.shapes.clear(); selected=-1; }
            return;
        }
        Cel next{frame,blank?std::vector<Shape>{}:cel().shapes}; checkpoint();
        l.cels.push_back(next); std::sort(l.cels.begin(),l.cels.end(),[](const Cel&a,const Cel&b){return a.frame<b.frame;}); selected=-1;
    }
    static float distance(Point p,Point a,Point b) {
        float dx=b.x-a.x,dy=b.y-a.y, length=dx*dx+dy*dy;
        float t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.0f,1.0f):0;
        return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
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
            }
        }
        return -1;
    }
    // Bounded, versioned text format. Load validates a temporary document before
    // replacing the current one, so malformed files never erase artwork.
    bool save(const char *name) const {
        std::string temporary=std::string(name)+".tmp";
        FILE *f=std::fopen(temporary.c_str(),"wb"); if(!f) return false;
        std::fprintf(f,"JFXDRAW1 %d %d %zu\n",doc.frames,doc.fps,doc.layers.size());
        for(const auto &l:doc.layers) {
            std::fprintf(f,"%d %d %zu\n",l.visible?1:0,l.locked?1:0,l.cels.size());
            for(const auto &c:l.cels) {
                std::fprintf(f,"%d %zu\n",c.frame,c.shapes.size());
                for(const auto &s:c.shapes) {
                    std::fprintf(f,"%d %u %u %.9g %d %zu\n",(int)s.kind,s.stroke,s.fill,(double)s.width,s.filled?1:0,s.points.size());
                    for(auto p:s.points) std::fprintf(f,"%.9g %.9g\n",(double)p.x,(double)p.y);
                }
            }
        }
        bool ok=!std::ferror(f); ok=std::fclose(f)==0 && ok;
#ifdef _WIN32
        if(ok) ok=MoveFileExA(temporary.c_str(),name,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        if(ok) ok=std::rename(temporary.c_str(),name)==0;
#endif
        if(!ok) std::remove(temporary.c_str());
        return ok;
    }
    bool load(const char *name) {
        FILE *f=std::fopen(name,"rb"); if(!f) return false;
        Document next; next.layers.clear(); char magic[16]{}; unsigned count=0; size_t total=0;
        bool ok=std::fscanf(f,"%15s %d %d %u",magic,&next.frames,&next.fps,&count)==4;
        ok=ok && std::string(magic)=="JFXDRAW1" && next.frames>=1 && next.frames<=240 && next.fps>=1 && next.fps<=60 && count>=1 && count<=8;
        for(unsigned i=0;ok && i<count;++i) {
            Layer l; l.cels.clear(); int visible=0,locked=0; unsigned keys=0;
            ok=std::fscanf(f,"%d %d %u",&visible,&locked,&keys)==3 && (visible==0 || visible==1) && (locked==0 || locked==1) && keys>=1 && keys<=(unsigned)next.frames;
            l.visible=visible!=0; l.locked=locked!=0;
            for(unsigned k=0;ok && k<keys;++k) {
                Cel c; unsigned shapes=0;
                ok=std::fscanf(f,"%d %u",&c.frame,&shapes)==2 && c.frame>=1 && c.frame<=next.frames && shapes<=256 && (k?c.frame>l.cels.back().frame:c.frame==1);
                for(unsigned j=0;ok && j<shapes;++j) {
                    Shape s; int kind=0,filled_value=0; unsigned points=0;
                    ok=std::fscanf(f,"%d %u %u %f %d %u",&kind,&s.stroke,&s.fill,&s.width,&filled_value,&points)==6;
                    ok=ok && kind>=0 && kind<=3 && std::isfinite(s.width) && s.width>=0.5f && s.width<=40 && (filled_value==0 || filled_value==1) && points>=2 && points<=2048 && (kind==0 || points==2);
                    total+=points; ok=ok && total<=1000000; s.kind=(Kind)kind; s.filled=filled_value!=0;
                    for(unsigned p=0;ok && p<points;++p) {
                        Point v; ok=std::fscanf(f,"%f %f",&v.x,&v.y)==2 && std::isfinite(v.x) && std::isfinite(v.y) && std::abs(v.x)<=100000 && std::abs(v.y)<=100000;
                        if(ok) s.points.push_back(v);
                    }
                    if(ok) c.shapes.push_back(s);
                }
                if(ok) l.cels.push_back(c);
            }
            if(ok) next.layers.push_back(l);
        }
        if(ok) { int ch; do { ch=std::fgetc(f); } while(ch==' ' || ch=='\n' || ch=='\r' || ch=='\t'); ok=ch==EOF && !std::ferror(f); }
        std::fclose(f); if(!ok) return false;
        checkpoint(); doc=next; reset_selection(); return true;
    }
    bool export_svg(const char *name) const {
        FILE *f=std::fopen(name,"wb"); if(!f) return false;
        std::fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"540\" viewBox=\"0 0 960 540\">\n",f);
        auto rgb=[](uint32_t c) {return ((c&255u)<<16)|(c&0xff00u)|((c>>16)&255u);};
        for(const auto &l:doc.layers) if(l.visible) for(const auto &s:exposure(l,frame).shapes) {
            Point a=s.points.front(),b=s.points.back();
            if(s.kind==Kind::Rectangle) std::fprintf(f,"<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"",(double)std::min(a.x,b.x),(double)std::min(a.y,b.y),(double)std::abs(b.x-a.x),(double)std::abs(b.y-a.y));
            else if(s.kind==Kind::Ellipse) std::fprintf(f,"<ellipse cx=\"%g\" cy=\"%g\" rx=\"%g\" ry=\"%g\"",(double)(a.x+b.x)/2,(double)(a.y+b.y)/2,(double)std::abs(b.x-a.x)/2,(double)std::abs(b.y-a.y)/2);
            else { std::fputs("<polyline points=\"",f); for(auto p:s.points) std::fprintf(f,"%g,%g ",(double)p.x,(double)p.y); std::fputs("\"",f); }
            std::fprintf(f," stroke=\"#%06x\" stroke-opacity=\"%g\" stroke-width=\"%g\" stroke-linecap=\"round\" stroke-linejoin=\"round\"",rgb(s.stroke),(double)(s.stroke>>24)/255,(double)s.width);
            if(s.filled && (s.kind==Kind::Rectangle || s.kind==Kind::Ellipse)) std::fprintf(f," fill=\"#%06x\" fill-opacity=\"%g\"",rgb(s.fill),(double)(s.fill>>24)/255);
            else std::fputs(" fill=\"none\"",f);
            std::fputs("/>\n",f);
        }
        std::fputs("</svg>\n",f); bool ok=!std::ferror(f); return std::fclose(f)==0 && ok;
    }
};
} // namespace jfx_drawing
#endif
