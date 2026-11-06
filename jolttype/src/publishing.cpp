#include "jolttype/publishing.hpp"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace jolttype {
bool publish_pdf(const Document &, tilly::string &, Diagnostic &, Progress, void *) noexcept;
bool publish_pdf_podofo(const Document &, tilly::string &, Diagnostic &, Progress, void *) noexcept;
namespace {
void fail(Diagnostic &e,const char *s) { e={}; std::snprintf(e.message,sizeof(e.message),"%s",s); }
tilly::string num(double n) { char b[64]; auto r=std::to_chars(b,b+64,n,std::chars_format::general,10); return {b,r.ptr}; }
void ascii(const Document &d) {
    for(const auto &e:d.elements) for(char c:e.text)
        if(static_cast<unsigned char>(c)>=128) throw std::runtime_error("DVI/PostScript text currently requires ASCII; use PDF/SVG for Unicode.");
}
tilly::string ps_text(std::string_view s) {
    tilly::string out="(";
    for(char c:s) { if(c=='(' || c==')' || c=='\\')out+='\\'; out+=c; }
    return out+")";
}
tilly::string ps_image(const Element &e,double bottom) {
    tilly::string out="gsave "+num(e.x)+" "+num(bottom)+" translate "+num(e.width)+" "+num(e.height)+" scale\n"+
        num(e.image_width)+" "+num(e.image_height)+" 8 ["+num(e.image_width)+" 0 0 -"+num(e.image_height)+" 0 "+num(e.image_height)+"]\n{currentfile "+num(e.image_width*3)+" string readhexstring pop} false 3 colorimage\n";
    static constexpr char hex[]="0123456789abcdef";
    for(size_t i=0;i<e.pixels.size();i+=4){if(e.pixels[i+3]!=255)throw std::runtime_error("PostScript images must be opaque; use PDF/SVG for alpha.");
        for(size_t c=0;c<3;++c){out+=hex[e.pixels[i+c]>>4];out+=hex[e.pixels[i+c]&15];}if(i%128==0)out+='\n';}
    return out+"\ngrestore\n";
}
tilly::string postscript(const Document &d) {
    ascii(d);
    tilly::string out="%!PS-Adobe-3.0\n%%Creator: JoltType\n%%Pages: "+num(double(d.pages.size()))+"\n%%EndComments\n";
    for(size_t p=0;p<d.pages.size();++p) {
        const auto &page=d.pages[p];
        out+="%%Page: "+num(double(p+1))+" "+num(double(p+1))+"\n<< /PageSize ["+num(page.width)+" "+num(page.height)+"] >> setpagedevice\n";
        for(const auto &e:d.elements) if(e.page==p) {
            out+=num(double((e.color>>16)&255)/255)+" "+num(double((e.color>>8)&255)/255)+" "+num(double(e.color&255)/255)+" setrgbcolor\n";
            if(e.kind==Kind::Rectangle) out+=num(e.x)+" "+num(page.height-e.y-e.height)+" "+num(e.width)+" "+num(e.height)+" rectfill\n";
            else if(e.kind==Kind::Image)out+=ps_image(e,page.height-e.y-e.height);
            else {
                out+="/Courier findfont "+num(e.size)+" scalefont setfont\n";
                double y=page.height-e.y-e.size;
                for(const auto &line:e.lines) {
                    double x=e.x;
                    for(char c:line.text) { out+=num(x)+" "+num(y)+" moveto "+ps_text(std::string_view(&c,1))+" show\n"; x+=e.size*.6; }
                    y-=e.size*1.2;
                }
            }
        }
        out+="showpage\n";
    }
    return out+"%%EOF\n";
}
struct Dvi {
    tilly::string bytes;
    void byte(unsigned n) { bytes+=char(n&255u); }
    void integer(int64_t n,unsigned count=4) { for(unsigned i=count;i>0;--i) byte(unsigned(uint64_t(n)>>(8*(i-1)))); }
    int64_t unit(double pt) { return int64_t(std::llround(pt*65536*72.27/72)); }
    void special(const tilly::string &s) { byte(242); integer(int64_t(s.size())); bytes+=s; }
    void font(unsigned index,double size) {
        byte(243); byte(index); integer(0); integer(unit(size)); integer(10*65536); byte(0); byte(6); bytes+="cmtt10";
    }
};
tilly::string dvi(const Document &d) {
    for(const auto &e:d.elements)if(e.kind==Kind::Image)throw std::runtime_error("DVI has no embedded bitmap profile; use PDF, SVG or PostScript for images.");
    ascii(d); Dvi w; w.byte(247); w.byte(2); w.integer(25400000); w.integer(473628672); w.integer(1000); w.byte(8); w.bytes+="JoltType";
    tilly::vector<double> sizes;
    for(const auto &e:d.elements) if(e.kind==Kind::Text) {
        bool have=false; for(double s:sizes)if(s==e.size)have=true;
        if(!have)sizes.push_back(e.size);
    }
    if(sizes.size()>256)throw std::runtime_error("DVI supports at most 256 distinct font sizes.");
    int64_t previous=-1; double width=0,height=0;
    for(size_t p=0;p<d.pages.size();++p) {
        int64_t bop=int64_t(w.bytes.size()); w.byte(139); w.integer(int64_t(p+1)); for(int i=0;i<9;++i)w.integer(0); w.integer(previous); previous=bop;
        const auto &page=d.pages[p]; width=std::max(width,page.width); height=std::max(height,page.height);
        w.special("papersize="+num(page.width)+"bp,"+num(page.height)+"bp");
        for(const auto &e:d.elements) if(e.page==p) {
            w.byte(141); // preserve h/v for each independently positioned frame
            w.byte(146); w.integer(w.unit(e.x-72));
            w.byte(160); w.integer(w.unit(e.y-72+(e.kind==Kind::Text?e.size:e.height)));
            w.special("color push rgb "+num(double((e.color>>16)&255)/255)+" "+num(double((e.color>>8)&255)/255)+" "+num(double(e.color&255)/255));
            if(e.kind==Kind::Rectangle) {w.byte(137);w.integer(w.unit(e.height));w.integer(w.unit(e.width));}
            else {
                unsigned f=0; while(sizes[f]!=e.size)++f;
                w.font(f,e.size);w.byte(235);w.byte(f);
                for(const auto &line:e.lines) {
                    w.byte(141);
                    for(char c:line.text) {w.byte(133);w.byte(static_cast<unsigned char>(c));w.byte(146);w.integer(w.unit(e.size*.6));}
                    w.byte(142);w.byte(160);w.integer(w.unit(e.size*1.2));
                }
            }
            w.special("color pop");w.byte(142);
        }
        w.byte(140);
    }
    auto post=int64_t(w.bytes.size());w.byte(248);w.integer(previous);w.integer(25400000);w.integer(473628672);w.integer(1000);
    w.integer(w.unit(height));w.integer(w.unit(width));w.integer(2,2);w.integer(int64_t(d.pages.size()),2);
    for(unsigned i=0;i<sizes.size();++i)w.font(i,sizes[i]);
    w.byte(249);w.integer(post);w.byte(2);for(int i=0;i<4;++i)w.byte(223);while(w.bytes.size()%4)w.byte(223);
    return std::move(w.bytes);
}
}
bool publish(const Document &doc,Target target,size_t page,tilly::string &out,Diagnostic &error,Progress progress,void *user) noexcept {
    try {
        if(doc.pages.empty() || doc.pages.size()>max_pages || doc.elements.size()>max_elements) {fail(error,"Compile a valid document before publishing.");return false;}
        if(progress)progress(user,0,"Compiling output");
        if(target==Target::PDF)return publish_pdf(doc,out,error,progress,user);
        if(target==Target::PDFPoDoFo)return publish_pdf_podofo(doc,out,error,progress,user);
        tilly::string next;
        if(target==Target::SVG) {if(!export_svg(doc,page,next,error))return false;}
        else if(target==Target::PostScript)next=postscript(doc);
        else if(target==Target::DVI)next=dvi(doc);
        else {fail(error,"Unknown output target.");return false;}
        out.swap(next);error={};if(progress)progress(user,100,"Complete");return true;
    } catch(const std::exception &e) {fail(error,e.what());return false;}
}
}
