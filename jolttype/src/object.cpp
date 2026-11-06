#include "project_internal.hpp"
#include <algorithm>
namespace jolttype {
using namespace detail;
bool compile_object(const Document &doc,tilly::string &out,Diagnostic &error) noexcept {
    try {
        Writer w;w.data="JTBC1";w.u32(uint32_t(doc.pages.size()));
        for(const auto &p:doc.pages){w.real(p.width);w.real(p.height);}
        w.u32(uint32_t(doc.elements.size()));
        for(const auto &e:doc.elements){w.u32(unsigned(e.kind));w.text(e.id);w.u32(uint32_t(e.page));w.real(e.x);w.real(e.y);w.real(e.width);w.real(e.height);w.real(e.size);w.u32(e.color);w.text(e.text);w.text(e.asset);w.text(e.image);w.u32(uint32_t(e.lines.size()));for(const auto &line:e.lines){w.text(line.text);w.real(line.width);}}
        Document check;if(!load_object(w.data,check,error))return false;out.swap(w.data);return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
bool load_object(std::string_view bytes,Document &out,Diagnostic &error) noexcept {
    try {
        need(bytes.size()<=max_inventory_bytes,"Compiled document exceeds 64 MiB.");Reader r{bytes};need(r.take(5)=="JTBC1","Unsupported compiled JoltType version.");Document doc;
        size_t pages=r.u32();need(pages>0 && pages<=max_pages,"Invalid compiled page count.");
        for(size_t i=0;i<pages;++i){Page p;p.width=r.real();p.height=r.real();need(p.width>=10 && p.height>=10 && p.width<=14400 && p.height<=14400,"Invalid compiled page dimensions.");doc.pages.push_back(p);}
        size_t elements=r.u32();need(elements<=max_elements,"Too many compiled frames.");size_t text_bytes=0,image_bytes=0;
        for(size_t i=0;i<elements;++i){Element e;auto kind=r.u32();need(kind<=unsigned(Kind::Image),"Invalid compiled frame kind.");e.kind=Kind(kind);e.id=r.text(63);need(id(e.id),"Invalid compiled frame ID.");for(const auto &other:doc.elements)need(other.id!=e.id,"Duplicate compiled frame ID.");
            e.page=r.u32();need(e.page<pages,"Invalid compiled page index.");e.x=r.real();e.y=r.real();e.width=r.real();e.height=r.real();e.size=r.real();e.color=r.u32();e.text=r.text(max_source_bytes);e.asset=r.text(63);e.image=r.text(max_asset_bytes);
            if(e.kind==Kind::Image){if(!decode_image(e,error))return false;image_bytes+=e.pixels.size();need(image_bytes<=max_inventory_bytes,"Compiled image budget exceeded.");}else need(e.image.empty(),"Unexpected compiled image bytes.");
            text_bytes+=e.text.size();const auto &p=doc.pages[e.page];need(e.x>=0 && e.y>=0 && e.width>0 && e.height>0 && e.x+e.width<=p.width+1e-6 && e.y+e.height<=p.height+1e-6 && e.color<=0xffffff,"Invalid compiled frame bounds/color.");
            need(utf8(e.text),"Invalid UTF-8 in compiled frame.");
            size_t lines=r.u32();need(lines<=4096 && (e.kind==Kind::Text || lines==0),"Invalid compiled line count.");
            if(e.kind==Kind::Text)need(e.size>=4 && e.size<=240 && lines>0 && std::abs(e.height-double(lines)*e.size*1.2)<1e-6,"Invalid compiled text metrics.");
            for(size_t j=0;j<lines;++j){Line line;line.text=r.text(max_source_bytes);line.width=r.real();text_bytes+=line.text.size();need(line.width>=0 && line.width<=e.width+1e-6 && utf8(line.text),"Invalid compiled text line.");e.lines.push_back(std::move(line));}
            need(text_bytes<=2*max_source_bytes,"Compiled text exceeds its budget.");doc.elements.push_back(std::move(e));
        }
        r.end();out=std::move(doc);error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
}
