#include "jolttype/api.h"
#include "project_internal.hpp"
using namespace jolttype;
struct jt_target {tilly::string name,extension;jt_target_desc desc{};};
struct jt_project {Session session;Typelet typelet;Hatch hatch;tilly::vector<jt_target> targets;};
struct jt_document {Document doc;};
struct jt_module {Module module;};
namespace {
jt_result result(bool ok,const Diagnostic &error,jt_error *out){if(out){out->line=error.line;out->column=error.column;std::snprintf(out->message,sizeof(out->message),"%s",error.message);}return ok?JT_OK:JT_ERROR;}
jt_result invalid(jt_error *e,const char *message="Invalid JoltType API argument."){Diagnostic d;detail::fail(d,message);return result(false,d,e);}
jt_result write(jt_sink sink,std::string_view bytes,jt_error *e){if(!sink.write || !sink.write(sink.user,bytes.data(),bytes.size()))return invalid(e,"Output sink rejected JoltType data.");return result(true,{},e);}
bool sync(jt_project &p,Diagnostic &e){if(p.typelet.manifest.kind=="module")return p.typelet.inventory.validate(e);Asset main;if(const auto *a=p.typelet.inventory.find(p.typelet.manifest.entry))main=*a;else{main.id=p.typelet.manifest.entry;main.path="main.jtype";main.kind=AssetKind::Source;}main.bytes=p.session.source();return p.typelet.inventory.put(std::move(main),e);}
template<class F> jt_result protect(jt_error *e,F &&f){try{return f();}catch(const std::exception &x){return invalid(e,x.what());}catch(...){return invalid(e,"JoltType API operation failed.");}}
}
extern "C" {
bool jt_has_pdf(void){return pdf_available();}bool jt_has_pdfium(void){return pdf_preview_available();}bool jt_has_modules(void){return modules_available();}
jt_project *jt_project_create(void){auto *p=tilly::create<jt_project>();if(!p)return nullptr;Diagnostic e;
    if(!p->session.stage(example_source(),0) || !p->session.commit(0)){tilly::destroy(p);return nullptr;}
    try{if(!sync(*p,e) || !p->session.set_inventory(p->typelet.inventory,e) || !p->session.commit(0)){tilly::destroy(p);return nullptr;}}catch(...){tilly::destroy(p);return nullptr;}return p;}
void jt_project_destroy(jt_project *p){tilly::destroy(p);}
const char *jt_project_source(const jt_project *p,bool committed){return p?(committed?p->session.source():p->session.draft()).c_str():nullptr;}
jt_result jt_project_stage(jt_project *p,const char *s,size_t n,double now,jt_error *e){if(!p || (!s && n))return invalid(e);return result(p->session.stage(std::string_view(s?s:"",n),now),p->session.error(),e);}
jt_result jt_project_commit(jt_project *p,double now,jt_error *e){if(!p)return invalid(e);return protect(e,[&]{Session next=p->session;Diagnostic d;if(!next.set_inventory(p->typelet.inventory,d) || !next.commit(now))return result(false,d.message[0]?d:next.error(),e);p->session=std::move(next);return result(true,{},e);});}
jt_result jt_project_revert(jt_project *p,jt_error *e){if(!p)return invalid(e);return protect(e,[&]{auto next=*p;if(!next.session.revert())return result(false,next.session.error(),e);if(next.session.inventory())next.typelet.inventory=*next.session.inventory();*p=std::move(next);return result(true,{},e);});}
jt_result jt_project_history(jt_project *p,bool redo,jt_error *e){if(!p)return invalid(e);return protect(e,[&]{auto next=*p;if(!next.session.history(redo))return result(false,next.session.error(),e);if(next.session.inventory())next.typelet.inventory=*next.session.inventory();*p=std::move(next);return result(true,{},e);});}
jt_result jt_project_hatch(jt_project *p,jt_error *e){if(!p)return invalid(e);Diagnostic d;return result(p->hatch.begin(p->session,d),d,e);}
jt_result jt_project_restore_hatch(jt_project *p,double now,jt_error *e){if(!p)return invalid(e);Diagnostic d;return result(p->hatch.restore(p->session,now,d),d,e);}
jt_result jt_project_put_asset(jt_project *p,const jt_asset_desc *a,jt_error *e){
    if(!p || !a || a->size<sizeof(*a) || !a->id || !a->path || a->length>max_asset_bytes || a->tag_count>32 || a->dependency_count>32 || (a->tag_count && !a->tags) || (a->dependency_count && !a->dependencies) || (a->length && !a->bytes))return invalid(e);
    return protect(e,[&]{Asset next;next.id=a->id;next.path=a->path;next.kind=AssetKind(a->kind);next.bytes.assign(static_cast<const char *>(a->bytes?a->bytes:""),a->length);
        for(size_t i=0;i<a->tag_count;++i){detail::need(a->tags[i],"Missing asset tag.");next.tags.emplace_back(a->tags[i]);}
        for(size_t i=0;i<a->dependency_count;++i){detail::need(a->dependencies[i],"Missing dependency ID.");next.dependencies.emplace_back(a->dependencies[i]);}
        Diagnostic d;auto inventory=p->typelet.inventory;if(!inventory.put(std::move(next),d) || !p->session.set_inventory(inventory,d))return result(false,d,e);p->typelet.inventory=std::move(inventory);return result(true,{},e);});}
jt_result jt_project_remove_asset(jt_project *p,const char *id,jt_error *e){if(!p || !id || p->typelet.manifest.entry==id)return invalid(e);return protect(e,[&]{Diagnostic d;auto next=p->typelet.inventory;if(!next.remove(id,d) || !p->session.set_inventory(next,d))return result(false,d,e);p->typelet.inventory=std::move(next);return result(true,{},e);});}
size_t jt_project_asset_count(const jt_project *p){return p?p->typelet.inventory.assets().size():0;}
jt_result jt_project_asset(const jt_project *p,size_t i,jt_asset_info *out){if(!p || !out || i>=p->typelet.inventory.assets().size())return JT_ERROR;const auto &a=p->typelet.inventory.assets()[i];*out={a.id.c_str(),a.path.c_str(),jt_asset_kind(a.kind),a.bytes.size(),a.tags.size(),a.dependencies.size()};return JT_OK;}
const char *jt_project_asset_tag(const jt_project *p,size_t i,size_t j){if(!p || i>=p->typelet.inventory.assets().size())return nullptr;const auto &a=p->typelet.inventory.assets()[i];return j<a.tags.size()?a.tags[j].c_str():nullptr;}
const char *jt_project_asset_dependency(const jt_project *p,size_t i,size_t j){if(!p || i>=p->typelet.inventory.assets().size())return nullptr;const auto &a=p->typelet.inventory.assets()[i];return j<a.dependencies.size()?a.dependencies[j].c_str():nullptr;}
jt_result jt_project_manifest(const jt_project *p,jt_sink sink,jt_error *e){if(!p)return invalid(e);tilly::string text;Diagnostic d;if(!manifest_text(p->typelet,text,d))return result(false,d,e);return write(sink,text,e);}
jt_result jt_project_set_manifest(jt_project *p,const char *s,size_t n,jt_error *e){if(!p || !s)return invalid(e);return protect(e,[&]{Typelet next;tilly::vector<Asset> assets;Diagnostic d;
    if(!parse_manifest({s,n},next.manifest,assets,d))return result(false,d,e);
    for(auto &a:assets){auto *old=p->typelet.inventory.find(a.id);detail::need(old,"New manifest dependencies must be imported first.");a.bytes=old->bytes;}
    if(!next.inventory.replace(std::move(assets),d) || !p->session.set_inventory(next.inventory,d))return result(false,d,e);
    p->typelet=std::move(next);return result(true,{},e);});}
jt_result jt_project_build(jt_project *p,jt_error *e){if(!p || p->session.dirty())return invalid(e,"Commit source before building dependencies.");return protect(e,[&]{jt_project next=*p;Diagnostic d;if(!sync(next,d) || !build_typelet(next.typelet,d) || !next.session.set_inventory(next.typelet.inventory,d))return result(false,d,e);*p=std::move(next);return result(true,{},e);});}
jt_result jt_project_pack(jt_project *p,jt_sink sink,jt_error *e){if(!p || p->session.dirty())return invalid(e,"Commit source before packaging.");return protect(e,[&]{jt_project next=*p;Diagnostic d;tilly::string bytes;if(!sync(next,d) || !pack_typelet(next.typelet,bytes,d))return result(false,d,e);return write(sink,bytes,e);});}
jt_result jt_project_unpack(jt_project *p,const void *data,size_t n,double now,jt_error *e){if(!p || !data)return invalid(e);return protect(e,[&]{Typelet next;Diagnostic d;if(!unpack_typelet({static_cast<const char *>(data),n},next,d))return result(false,d,e);
    Session session=p->session;auto *entry=next.inventory.find(next.manifest.entry);detail::need(entry,"Missing package entry.");
    if(next.manifest.kind!="module") {if(!session.set_inventory(next.inventory,d) || !session.stage(entry->bytes,now) || !session.commit(now))return result(false,d.message[0]?d:session.error(),e);}
    p->typelet=std::move(next);p->session=std::move(session);p->hatch.accept();return result(true,{},e);});}
jt_document *jt_project_document(const jt_project *p){if(!p)return nullptr;auto *d=tilly::create<jt_document>();if(!d)return nullptr;try{d->doc=p->session.committed();return d;}catch(...){tilly::destroy(d);return nullptr;}}
void jt_document_destroy(jt_document *d){tilly::destroy(d);}
size_t jt_document_page_count(const jt_document *d){return d?d->doc.pages.size():0;}
size_t jt_document_element_count(const jt_document *d){return d?d->doc.elements.size():0;}
jt_result jt_document_page(const jt_document *d,size_t i,jt_page_info *out){if(!d || !out || i>=d->doc.pages.size())return JT_ERROR;const auto &p=d->doc.pages[i];*out={p.width,p.height};return JT_OK;}
jt_result jt_document_element(const jt_document *d,size_t i,jt_element_info *out){if(!d || !out || i>=d->doc.elements.size())return JT_ERROR;const auto &a=d->doc.elements[i];*out={jt_draw_kind(a.kind),a.id.c_str(),a.page,a.x,a.y,a.width,a.height,a.size,a.color,a.lines.size(),a.pixels.data(),a.image_width,a.image_height};return JT_OK;}
jt_result jt_document_line(const jt_document *d,size_t i,size_t j,jt_line_info *out){if(!d || !out || i>=d->doc.elements.size() || j>=d->doc.elements[i].lines.size())return JT_ERROR;const auto &line=d->doc.elements[i].lines[j];*out={line.text.c_str(),line.width};return JT_OK;}
jt_result jt_project_register_target(jt_project *p,const jt_target_desc *t,jt_error *e){if(!p || !t || t->size<sizeof(*t) || t->abi_major!=JOLTTYPE_API_MAJOR || !t->name || !t->extension || !t->emit)return invalid(e);return protect(e,[&]{
    detail::need(detail::id(t->name) && detail::id(t->extension) && p->targets.size()<32,"Invalid target descriptor.");for(auto builtin:{"pdf","pdf-podofo","svg","ps","dvi"})detail::need(std::strcmp(t->name,builtin)!=0,"Cannot replace a built-in target.");for(const auto &old:p->targets)detail::need(old.name!=t->name,"Duplicate target name.");jt_target target;target.name=t->name;target.extension=t->extension;target.desc=*t;p->targets.push_back(std::move(target));return result(true,{},e);});}
jt_result jt_project_export(const jt_project *p,const char *target,size_t page,jt_sink sink,jt_error *e){if(!p || !target || !sink.write || p->session.dirty())return invalid(e,"Commit source before export.");return protect(e,[&]{
    for(const auto &t:p->targets)if(t.name==target){jt_document snapshot{p->session.committed()};return t.desc.emit(t.desc.user,&snapshot,page,sink,e);}
    Target kind;if(!std::strcmp(target,"pdf"))kind=Target::PDF;else if(!std::strcmp(target,"pdf-podofo"))kind=Target::PDFPoDoFo;else if(!std::strcmp(target,"svg"))kind=Target::SVG;else if(!std::strcmp(target,"ps"))kind=Target::PostScript;else if(!std::strcmp(target,"dvi"))kind=Target::DVI;else return invalid(e,"Output target is not registered.");
    tilly::string bytes;Diagnostic d;if(!publish(p->session.committed(),kind,page,bytes,d))return result(false,d,e);return write(sink,bytes,e);});}
jt_result jt_compile_module(const char *s,size_t n,jt_sink sink,jt_error *e){if(!s)return invalid(e);tilly::string wasm;Diagnostic d;if(!compile_module({s,n},wasm,d))return result(false,d,e);return write(sink,wasm,e);}
jt_module *jt_module_create(void){return tilly::create<jt_module>();}void jt_module_destroy(jt_module *m){tilly::destroy(m);}
jt_result jt_module_load(jt_module *m,const void *data,size_t n,jt_error *e){if(!m || !data)return invalid(e);Diagnostic d;return result(m->module.load({static_cast<const char *>(data),n},d),d,e);}
jt_result jt_module_call(jt_module *m,const char *name,const double *args,size_t n,double *out,jt_error *e){if(!m || !out)return invalid(e);Diagnostic d;return result(m->module.call(name,args,n,*out,d),d,e);}
size_t jt_project_interface_count(const jt_project *p){return p?p->typelet.manifest.interfaces.size():0;}
jt_result jt_project_interface(const jt_project *p,size_t i,jt_interface_info *out){if(!p || !out || i>=p->typelet.manifest.interfaces.size())return JT_ERROR;const auto &a=p->typelet.manifest.interfaces[i];*out={a.id.c_str(),a.module.c_str(),a.entry.c_str(),a.label.c_str(),a.parameters.size()};return JT_OK;}
const char *jt_project_interface_parameter(const jt_project *p,size_t i,size_t j){if(!p || i>=p->typelet.manifest.interfaces.size())return nullptr;const auto &params=p->typelet.manifest.interfaces[i].parameters;return j<params.size()?params[j].c_str():nullptr;}
jt_result jt_project_invoke(jt_project *p,size_t index,const double *args,size_t n,const jt_frontend_host *host,double *out,jt_error *e){
    if(!p || !out || index>=p->typelet.manifest.interfaces.size() || (n && !args) || (host && host->size<sizeof(*host)))return invalid(e);
    return protect(e,[&]{const auto &ui=p->typelet.manifest.interfaces[index];detail::need(n==ui.parameters.size(),"Interface argument count mismatch.");auto *asset=p->typelet.inventory.find(ui.module);detail::need(asset && asset->kind==AssetKind::Module,"Interface module is not built.");double value=0;
        if(host && host->execute){auto status=host->execute(host->user,asset->bytes.data(),asset->bytes.size(),ui.entry.c_str(),args,n,&value,e);if(status!=JT_OK)return status;}
        else {Module module;Diagnostic d;if(!module.load(asset->bytes,d) || !module.call(ui.entry.c_str(),args,n,value,d))return result(false,d,e);}
        detail::need(std::isfinite(value),"Interface returned a non-finite value.");*out=value;if(host && host->action_result)host->action_result(host->user,ui.id.c_str(),value);return result(true,{},e);});}
}
