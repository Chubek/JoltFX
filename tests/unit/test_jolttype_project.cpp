#include "jolttype/project.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using namespace jolttype;
    Diagnostic error;Typelet p;
    Asset a;a.id="main";a.path="main.jtype";a.kind=AssetKind::Source;a.bytes=example_source();a.tags={"layout","sample"};
    assert(p.inventory.put(a,error));
    p.manifest.name="Test typelet";p.manifest.build.push_back({"main","layout.jtbc","jtbc"});
    assert(build_typelet(p,error));assert(p.inventory.find("layout.jtbc"));
    if(pdf_available()) {
        p.manifest.build.push_back({"main","final.pdf","pdf"});
        p.manifest.build.push_back({"main","alternative.pdf","pdf-podofo"});
        assert(build_typelet(p,error));
        assert(p.inventory.find("final.pdf")->bytes.find("JoltType / libharu")!=tilly::string::npos);
        assert(p.inventory.find("alternative.pdf")->bytes.find("JoltType / PoDoFo")!=tilly::string::npos);
    }
    Document loaded;assert(load_object(p.inventory.find("layout.jtbc")->bytes,loaded,error));assert(loaded.elements.size()==3);
    tilly::string packed;assert(pack_typelet(p,packed,error));
    Typelet q;assert(unpack_typelet(packed,q,error));assert(q.manifest.name==p.manifest.name);
    assert(q.inventory.find("main")->tags.size()==2 && q.inventory.find("main")->bytes==a.bytes);
    Asset library;library.id="library";library.path="pagelets/library.jtype";library.kind=AssetKind::Pagelet;
    library.bytes="\\jolttype{1}\\pagelet{Heading}{\\component{content}{Default}\\config{color}{#000000}\\text{label}{0}{0}{200}{12}{@color}{@content}}";
    assert(q.inventory.put(library,error));
    Asset style;style.id="style";style.path="kliches/style.jtype";style.kind=AssetKind::Kliche;style.dependencies={"library"};
    style.bytes="\\jolttype{1}\\use{library}\\kliche{Red}{Heading}{\\config{color}{#ff0000}}";
    assert(q.inventory.put(style,error));
    Asset font;font.id="font";font.path="fonts/example.ttf";font.kind=AssetKind::Font;font.bytes="font inventory payload";
    assert(q.inventory.put(font,error));
    auto entry=*q.inventory.find("main");entry.bytes="\\jolttype{1}\\use{style}\\page{400}{500}\\place{title}{Red}{20}{20}{\\component{content}{Package title}}";
    entry.dependencies={"style","font"};assert(q.inventory.put(entry,error));
    const bool design_built=build_typelet(q,error);
    if(!design_built)std::fprintf(stderr,"Pagelet package: %s\n",error.message);
    assert(design_built);tilly::string design_package;
    assert(pack_typelet(q,design_package,error));Typelet restored;
    assert(unpack_typelet(design_package,restored,error));
    assert(restored.inventory.find("font")->kind==AssetKind::Font && restored.inventory.find("style")->kind==AssetKind::Kliche);
    Document design;assert(compile(restored.inventory.find("main")->bytes,design,error,&restored.inventory));
    assert(design.elements[0].text=="Package title" && design.elements[0].color==0xff0000);
    assert(load_object(restored.inventory.find("layout.jtbc")->bytes,design,error) && design.elements[0].text=="Package title");
    Asset image;image.id="logo";image.path="logo.png";image.kind=AssetKind::Image;
    static const char png[]="\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x0d\x49\x44\x41\x54\x78\x9c\x63\xf8\xcf\xc0\xf0\x1f\x00\x05\x00\x01\xff\x89\x99\x3d\x1d\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
    image.bytes.assign(png,sizeof(png)-1);assert(p.inventory.put(image,error));
    Document pictured;assert(compile("\\jolttype{1}\\page{100}{100}\\image{logo}{0}{0}{100}{100}{logo}",pictured,error,&p.inventory));
    assert(pictured.elements[0].pixels.size()==4 && pictured.elements[0].pixels[0]==255);
    tilly::string rendered;assert(publish(pictured,Target::SVG,0,rendered,error));assert(rendered.find("data:image/png;base64,")!=rendered.npos);
    assert(publish(pictured,Target::PostScript,0,rendered,error));assert(rendered.find("colorimage")!=rendered.npos);
    if(pdf_available()) {
        assert(publish(pictured,Target::PDF,0,rendered,error));
        if(pdf_preview_available()){Bitmap bitmap;assert(render_pdf(rendered,0,64,bitmap,error));assert(bitmap.rgba[0]>240 && bitmap.rgba[1]<10);}
    }
    assert(compile_object(pictured,rendered,error));assert(load_object(rendered,loaded,error));assert(loaded.elements[0].image==image.bytes);
    image.bytes="bad png";assert(p.inventory.put(image,error));
    assert(!compile("\\jolttype{1}\\page{100}{100}\\image{logo}{0}{0}{100}{100}{logo}",pictured,error,&p.inventory));
    assert(pictured.elements[0].pixels.size()==4);
    auto old=q.manifest.name;assert(!unpack_typelet(packed.substr(0,packed.size()/2),q,error));assert(q.manifest.name==old);
    a.path="../escape";assert(!p.inventory.put(a,error));
    Asset b;b.id="dependent";b.path="notes.txt";b.kind=AssetKind::Text;b.dependencies={"main"};b.bytes="Notes";assert(p.inventory.put(b,error));
    assert(!p.inventory.remove("main",error));
    a.path="main.jtype";a.dependencies={"dependent"};assert(!p.inventory.put(a,error));
    Manifest m;tilly::vector<Asset> descriptors;
    const char *bad[]={"{", "{} trailing", "{\"name\":\"unterminated\\", "{\"schema\":1,\"schema\":1}", "{\"schema\":1,\"dependencies\":[{}]}"};
    for(auto s:bad)assert(!parse_manifest(s,m,descriptors,error));
    Session session;assert(session.stage(example_source(),0) && session.commit(0));
    Hatch hatch;assert(hatch.begin(session,error));assert(session.stage("invalid",1));assert(!session.commit(2));
    assert(hatch.restore(session,3,error));assert(session.commit(4));assert(session.source()==example_source());
    if(modules_available()) {
        tilly::string wasm;assert(compile_module("\\module{measure}\n\\export{area}{w,h}{w*h}\n\\export{diagonal}{w,h}{sqrt(w*w+h*h)}",wasm,error));
        Module module;assert(module.load(wasm,error));double result=0,args[]={3,4};
        assert(module.call("area",args,2,result,error) && result==12);
        assert(module.call("diagonal",args,2,result,error) && result==5);
        assert(!module.call("missing",args,2,result,error) && result==5);
        assert(!module.load("invalid",error));assert(module.call("area",args,2,result,error) && result==12);
        assert(!compile_module("\\module{x}\\export{bad}{x}{unknown(x)}",wasm,error));
    }
    std::puts("Inventory, Typelet and module checks passed");
}
