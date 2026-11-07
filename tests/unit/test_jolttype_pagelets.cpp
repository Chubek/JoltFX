#include "jolttype/pagelets.hpp"
#include "jolttype/project.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc,char **argv) {
    using namespace jolttype;
    assert(argc==2);
    size_t count=0;const auto *catalog=bundled_pagelets(count);
    assert(catalog && count==33); // Original two Pagelets/Kliche plus 30 Pagelets.
    Diagnostic error;Typelet package;
    tilly::string source="\\jolttype{1}\n";
    size_t added=0;
    for(size_t i=0;i<count;++i) {
        const auto &entry=catalog[i];
        assert(*entry.name && *entry.category && *entry.description && *entry.asset_id);
        for(size_t j=0;j<i;++j)assert(std::strcmp(entry.name,catalog[j].name));
        auto path=tilly::string(argv[1])+"/"+entry.asset_id+".jtype";
        FILE *file=std::fopen(path.c_str(),"rb");assert(file);
        tilly::string bytes(max_source_bytes+1,'\0');auto n=std::fread(bytes.data(),1,bytes.size(),file);
        assert(!std::ferror(file) && n<bytes.size());std::fclose(file);bytes.resize(n);
        assert(bytes==entry.source); // Installed and embedded source have one owner.
        if(!package.inventory.find(entry.asset_id)) {
            Asset asset;asset.id=entry.asset_id;asset.path="pagelets/"+asset.id+".jtype";
            asset.kind=AssetKind::Pagelet;asset.bytes=entry.source;asset.tags={entry.category};
            assert(package.inventory.put(std::move(asset),error));
            source+="\\use{"+tilly::string(entry.asset_id)+"}\n";
        }
        auto use="\\jolttype{1}\\use{"+tilly::string(entry.asset_id)+"}";
        auto place="\\place{sample}{"+tilly::string(entry.name)+"}{20}{20}{}";
        Session session;assert(session.set_inventory(package.inventory,error));
        assert(session.stage(use+"\\page{210mm}{297mm}"+place,0));
        if(!session.commit(0))std::fprintf(stderr,"%s: %s\n",entry.name,session.error().message);
        assert(!session.dirty());
        const auto &doc=session.committed();assert(doc.entities.size()==1 && !doc.elements.empty());
        // Default copy must fit the designed slots, not overlap adjacent text.
        for(size_t a=0;a<doc.elements.size();++a)for(size_t b=a+1;b<doc.elements.size();++b) {
            const auto &x=doc.elements[a],&y=doc.elements[b];
            if(x.kind!=Kind::Text || y.kind!=Kind::Text)continue;
            bool overlap=x.x<y.x+y.width && y.x<x.x+x.width && x.y<y.y+y.height && y.y<x.y+x.height;
            if(overlap)std::fprintf(stderr,"Overlapping default text: %s / %s / %s\n",entry.name,x.id.c_str(),y.id.c_str());
            assert(!overlap);
        }
        bool definition=false;
        for(const auto &p:doc.pagelets)if(p.id==entry.name){definition=true;if(i>=3){assert(p.base.empty());++added;}}
        assert(definition);
        auto components=doc.entities[0].components;bool edited=false,configured=false;
        for(const auto &c:components) {
            if(!c.configuration) {assert(session.set_component("sample",c.name,c.value.size()<=3?"1":"Edited",1));edited=true;}
            else if(!configured && c.value.size()==7 && c.value[0]=='#') {
                assert(session.set_component("sample",c.name,"#446688",1));configured=true;
            }
        }
        assert(edited && configured);
        assert(session.move("sample",36,42,2) && session.commit(2));
        assert(session.history(false) && session.committed().entities[0].x==20);
        Document letter;assert(compile(use+"\\page{8.5in}{11in}"+place,letter,error,&package.inventory));
        tilly::string svg;assert(export_svg(letter,0,svg,error) && svg.find("<text")!=svg.npos);
        char entity[64];std::snprintf(entity,sizeof(entity),"item%zu",i);
        source+="\\page{210mm}{297mm}\\place{"+tilly::string(entity)+"}{"+entry.name+"}{20}{20}{}\n";
    }
    assert(added==30);
    Asset main;main.id="main";main.path="main.jtype";main.kind=AssetKind::Source;main.bytes=source;
    assert(package.inventory.put(main,error));package.manifest.build.push_back({"main","catalog.jtbc","jtbc"});
    if(!build_typelet(package,error))std::fprintf(stderr,"Catalog package: %s\n",error.message);
    assert(package.inventory.find("catalog.jtbc"));
    tilly::string packed;assert(pack_typelet(package,packed,error));Typelet restored;
    assert(unpack_typelet(packed,restored,error));Document doc,frozen;
    assert(compile(restored.inventory.find("main")->bytes,doc,error,&restored.inventory));
    assert(doc.pages.size()==33 && doc.entities.size()==33);
    assert(load_object(restored.inventory.find("catalog.jtbc")->bytes,frozen,error));
    assert(frozen.elements.size()==doc.elements.size());
    if(pdf_available()){tilly::string pdf;assert(publish(doc,Target::PDF,0,pdf,error));assert(pdf.find("%PDF-")==0);}
    std::puts("All 33 bundled entries compile, edit, export and round-trip; 30 new Pagelets verified.");
}
