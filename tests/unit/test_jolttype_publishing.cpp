#include "jolttype/publishing.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    jolttype::Document doc; jolttype::Diagnostic error;
    assert(jolttype::compile(jolttype::example_source(),doc,error));
    tilly::string out;
    assert(jolttype::publish(doc,jolttype::Target::PostScript,0,out,error));
    assert(out.find("%!PS-Adobe-3.0") == 0 && out.find("showpage") != out.npos);
    assert(jolttype::write_file("jolttype-output.ps",out,error));
    assert(jolttype::publish(doc,jolttype::Target::DVI,0,out,error));
    assert(static_cast<unsigned char>(out[0]) == 247 && out[1] == 2);
    assert(jolttype::write_file("jolttype-output.dvi",out,error));
    assert(jolttype::publish(doc,jolttype::Target::SVG,0,out,error));
    assert(out.find("<svg") == 0);
    if (jolttype::pdf_available()) {
        unsigned progress = 0;
        assert(jolttype::publish(doc,jolttype::Target::PDF,0,out,error,
            [](void *p,unsigned n,const char *) { auto &last=*static_cast<unsigned *>(p); assert(n>=last); last=n; },&progress));
        assert(progress == 100 && out.find("%PDF-") == 0);
        assert(out.find("JoltType / libharu")!=out.npos);
        size_t pages=0; assert(jolttype::inspect_pdf(out,pages,error) && pages==1);
        assert(jolttype::write_file("jolttype-output.pdf",out,error));
        if (jolttype::pdf_preview_available()) {
            jolttype::Bitmap image; assert(jolttype::render_pdf(out,0,640,image,error));
            assert(image.height==640 && image.width>400 && image.rgba.size()==image.width*image.height*4);
            size_t ink=0;
            for(size_t i=0;i<image.rgba.size();i+=4) if(image.rgba[i]<240 || image.rgba[i+1]<240 || image.rgba[i+2]<240) ++ink;
            assert(ink>1000);
            auto width=image.width;
            assert(!jolttype::render_pdf("not pdf",0,640,image,error) && image.width==width);
            assert(!jolttype::render_pdf(out,1,640,image,error));
        }
        tilly::string alternative;
        assert(jolttype::publish(doc,jolttype::Target::PDFPoDoFo,0,alternative,error));
        assert(alternative.find("JoltType / PoDoFo")!=alternative.npos);
        assert(jolttype::inspect_pdf(alternative,pages,error) && pages==1);
        assert(jolttype::write_file("jolttype-podofo.pdf",alternative,error));
        jolttype::Document multi;
        assert(jolttype::compile("\\jolttype{1}\\page{100}{100}\\text{unicode}{5}{5}{90}{12}{#123456}{Café Ω}\\page{100}{120}\\rect{box}{10}{10}{40}{50}{#ff0000}",multi,error));
        assert(jolttype::publish(multi,jolttype::Target::PDF,0,out,error));
        assert(jolttype::inspect_pdf(out,pages,error) && pages==2);
        if(jolttype::pdf_preview_available()) {
            jolttype::Bitmap bitmap;assert(jolttype::render_pdf(out,1,120,bitmap,error));
            auto index=(size_t(20)*bitmap.width+20)*4;
            assert(bitmap.rgba[index]>240 && bitmap.rgba[index+1]<10);
        }
        const auto published=out;
        assert(jolttype::compile("\\jolttype{1}\\page{100}{100}\\text{emoji}{5}{5}{90}{12}{#000000}{\xf0\x9f\x98\x80}",multi,error));
        assert(!jolttype::publish(multi,jolttype::Target::PDF,0,out,error));
        assert(out==published && std::strstr(error.message,"PoDoFo"));
    }
    std::puts("Publishing checks passed");
}
