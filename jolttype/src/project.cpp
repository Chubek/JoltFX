#include "project_internal.hpp"
#include <algorithm>
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

namespace jolttype {
using namespace detail;
const Asset *Inventory::find(std::string_view name) const {for(const auto &a:assets_)if(a.id==name)return &a;return nullptr;}
bool Inventory::validate(Diagnostic &error) const noexcept {
    try {
        need(assets_.size()<=max_assets,"Inventory exceeds 128 assets.");size_t total=0;
        for(size_t i=0;i<assets_.size();++i){const auto &a=assets_[i];
            need(id(a.id) && path(a.path) && a.path!="MANIFEST" && unsigned(a.kind)<=unsigned(AssetKind::Kliche),"Invalid asset ID, kind or package path.");
            need(a.bytes.size()<=max_asset_bytes && a.bytes.size()<=max_inventory_bytes-total,"Inventory exceeds its byte budget.");total+=a.bytes.size();
            need(a.tags.size()<=32 && a.dependencies.size()<=32,"Too many asset tags/dependencies.");
            for(const auto &tag:a.tags){need(!tag.empty() && tag.size()<=63,"Invalid asset tag.");for(char c:tag)need(static_cast<unsigned char>(c)>=32,"Invalid tag character.");}
            for(size_t j=0;j<i;++j)need(assets_[j].id!=a.id && assets_[j].path!=a.path,"Duplicate asset ID or package path.");
            for(const auto &dep:a.dependencies)need(id(dep) && find(dep),"Missing inventory dependency.");
        }
        unsigned char marks[max_assets]{};
        auto visit=[&](auto &&self,size_t i)->void {
            need(marks[i]!=1,"Cyclic inventory dependency.");if(marks[i]==2)return;marks[i]=1;
            for(const auto &dep:assets_[i].dependencies)for(size_t j=0;j<assets_.size();++j)if(assets_[j].id==dep)self(self,j);
            marks[i]=2;
        };
        for(size_t i=0;i<assets_.size();++i)visit(visit,i);
        error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
bool Inventory::replace(tilly::vector<Asset> assets,Diagnostic &error) noexcept {
    Inventory next;next.assets_=std::move(assets);if(!next.validate(error))return false;assets_.swap(next.assets_);return true;
}
bool Inventory::put(Asset asset,Diagnostic &error) noexcept {
    try {auto next=assets_;bool replaced=false;for(auto &a:next)if(a.id==asset.id){a=std::move(asset);replaced=true;break;}if(!replaced)next.push_back(std::move(asset));return replace(std::move(next),error);}
    catch(const std::exception &e){fail(error,e.what());return false;}
}
bool Inventory::remove(std::string_view name,Diagnostic &error) noexcept {
    try {auto next=assets_;auto i=std::find_if(next.begin(),next.end(),[&](const Asset &a){return a.id==name;});need(i!=next.end(),"Asset not found.");next.erase(i);return replace(std::move(next),error);}
    catch(const std::exception &e){fail(error,e.what());return false;}
}
bool Inventory::tag(std::string_view name,tilly::vector<tilly::string> tags,Diagnostic &error) noexcept {
    try {const auto *a=find(name);need(a!=nullptr,"Asset not found.");auto next=*a;next.tags=std::move(tags);return put(std::move(next),error);}
    catch(const std::exception &e){fail(error,e.what());return false;}
}
bool Inventory::import_file(const char *filename,std::string_view name,AssetKind kind,tilly::vector<tilly::string> tags,Diagnostic &error) noexcept {
    struct File {FILE *p;~File(){if(p)std::fclose(p);}} file{filename?std::fopen(filename,"rb"):nullptr};
    try {
        need(file.p!=nullptr,"Cannot open dependency file.");Asset a;a.id=name;a.kind=kind;a.tags=std::move(tags);
        const char *base=filename;for(const char *p=filename;*p;++p)if(*p=='/' || *p=='\\')base=p+1;a.path=base;
        a.bytes.resize(max_asset_bytes+1);size_t n=std::fread(a.bytes.data(),1,a.bytes.size(),file.p);
        need(!std::ferror(file.p) && n<=max_asset_bytes,"Dependency read failed or file exceeds 16 MiB.");a.bytes.resize(n);return put(std::move(a),error);
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
namespace {
void *zalloc(void *,size_t n){return tilly_alloc(const_cast<tilly_allocator_t *>(tilly_default_allocator()),n,alignof(max_align_t));}
void zfree(void *,void *p){tilly_free(const_cast<tilly_allocator_t *>(tilly_default_allocator()),p);}
constexpr size_t max_archive=70*1024*1024;
}
bool pack_typelet(const Typelet &p,tilly::string &out,Diagnostic &error) noexcept {
    try {
        if(!p.inventory.validate(error))return false;
        tilly::string manifest;if(!manifest_text(p,manifest,error))return false;
        Writer w;w.data.append("JTLET1\0\0",8);w.u32(uint32_t(p.inventory.assets().size()+1));w.text("MANIFEST");w.text(manifest);
        tilly::vector<const Asset *> ordered;for(const auto &a:p.inventory.assets())ordered.push_back(&a);
        std::sort(ordered.begin(),ordered.end(),[](const Asset *a,const Asset *b){return a->path<b->path;});
        for(const auto *a:ordered){w.text(a->path);w.text(a->bytes);}
        need(w.data.size()<=max_archive,"Typelet exceeds its archive budget.");
        struct Context{ZSTD_CCtx *p;~Context(){ZSTD_freeCCtx(p);}} ctx{ZSTD_createCCtx_advanced({zalloc,zfree,nullptr})};need(ctx.p!=nullptr,"zstd allocation failed.");
        tilly::string next(ZSTD_compressBound(w.data.size()),'\0');
        size_t n=ZSTD_compressCCtx(ctx.p,next.data(),next.size(),w.data.data(),w.data.size(),6);need(!ZSTD_isError(n),"zstd compression failed.");next.resize(n);out.swap(next);error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
bool unpack_typelet(std::string_view bytes,Typelet &out,Diagnostic &error) noexcept {
    try {
        need(!bytes.empty() && bytes.size()<=max_archive,"Invalid Typelet size.");
        auto size=ZSTD_getFrameContentSize(bytes.data(),bytes.size());need(size!=ZSTD_CONTENTSIZE_ERROR && size!=ZSTD_CONTENTSIZE_UNKNOWN && size<=max_archive,"Invalid or oversized zstd Typelet frame.");
        auto compressed=ZSTD_findFrameCompressedSize(bytes.data(),bytes.size());need(!ZSTD_isError(compressed) && compressed==bytes.size(),"Trailing or concatenated Typelet frames.");
        struct Context{ZSTD_DCtx *p;~Context(){ZSTD_freeDCtx(p);}} ctx{ZSTD_createDCtx_advanced({zalloc,zfree,nullptr})};need(ctx.p!=nullptr,"zstd allocation failed.");
        need(!ZSTD_isError(ZSTD_DCtx_setMaxWindowSize(ctx.p,max_archive)),"Invalid zstd window limit.");
        tilly::string raw(size_t(size),'\0');auto n=ZSTD_decompressDCtx(ctx.p,raw.data(),raw.size(),bytes.data(),bytes.size());need(!ZSTD_isError(n) && n==raw.size(),"Corrupt Typelet payload.");
        Reader r{raw};need(r.take(8)==std::string_view("JTLET1\0\0",8),"Unsupported Typelet version.");size_t count=r.u32();need(count>=2 && count<=max_assets+1,"Invalid Typelet entry count.");
        need(r.text(255)=="MANIFEST","Typelet must start with MANIFEST.");auto text=r.text(max_source_bytes);
        Typelet next;tilly::vector<Asset> descriptors;if(!parse_manifest(text,next.manifest,descriptors,error))return false;
        need(descriptors.size()==count-1,"MANIFEST does not describe every archive entry.");
        bool seen[max_assets]{};for(size_t i=1;i<count;++i){auto name=r.text(255);need(path(name) && name!="MANIFEST","Invalid Typelet entry path.");auto data=r.text(max_asset_bytes);size_t found=descriptors.size();
            for(size_t j=0;j<descriptors.size();++j)if(descriptors[j].path==name)found=j;
            need(found<descriptors.size() && !seen[found],"Unknown or duplicate Typelet entry.");seen[found]=true;descriptors[found].bytes=std::move(data);}
        r.end();if(!next.inventory.replace(std::move(descriptors),error))return false;
        // Check compiled documents before publication; WASM is validated by WAMR
        // when explicitly instantiated, never executed merely by opening a package.
        for(const auto &a:next.inventory.assets())if(a.kind==AssetKind::Compiled){Document doc;if(!load_object(a.bytes,doc,error))return false;}
        out=std::move(next);error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
bool build_typelet(Typelet &p,Diagnostic &error,Progress progress,void *user) noexcept {
    try {
        Typelet next=p;tilly::string manifest;if(!manifest_text(next,manifest,error))return false;
        for(size_t i=0;i<next.manifest.build.size();++i){const auto &step=next.manifest.build[i];const auto *input=next.inventory.find(step.input);need(input!=nullptr,"Compile step input is missing.");
            Asset out;out.id=step.output;out.path=step.output;out.dependencies={step.input};out.tags={"generated"};
            if(progress)progress(user,unsigned(i*100/next.manifest.build.size()),step.target.c_str());
            if(step.target=="wasm"){out.kind=AssetKind::Module;if(!compile_module(input->bytes,out.bytes,error))return false;}
            else {Document doc;if(input->kind==AssetKind::Compiled){if(!load_object(input->bytes,doc,error))return false;}else if(!compile(input->bytes,doc,error,&next.inventory))return false;
                if(step.target=="jtbc"){out.kind=AssetKind::Compiled;if(!compile_object(doc,out.bytes,error))return false;}
                else {out.kind=AssetKind::Other;Target target=step.target=="pdf"?Target::PDF:step.target=="pdf-podofo"?Target::PDFPoDoFo:step.target=="ps"?Target::PostScript:step.target=="dvi"?Target::DVI:Target::SVG;if(!publish(doc,target,0,out.bytes,error))return false;}}
            if(!next.inventory.put(std::move(out),error))return false;
        }
        for(const auto &ui:next.manifest.interfaces){const auto *asset=next.inventory.find(ui.module);need(asset && asset->kind==AssetKind::Module,"Interface references a missing WASM module.");}
        p=std::move(next);error={};if(progress)progress(user,100,"Typelet build complete");return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
}
bool Hatch::begin(const Session &session,Diagnostic &error) noexcept {
    try {if(active_)return true;auto next=session.draft();source_.swap(next);active_=true;error={};return true;}
    catch(const std::exception &e){fail(error,e.what());return false;}
}
bool Hatch::restore(Session &session,double now,Diagnostic &error) noexcept {
    if(!active_){fail(error,"There is no source hatch checkpoint.");return false;}
    if(!session.stage(source_,now)){error=session.error();return false;}
    if(!session.dirty() && !session.revert()){error=session.error();return false;}
    accept();error={};return true;
}
}
