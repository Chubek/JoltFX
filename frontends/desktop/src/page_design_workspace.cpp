#include "page_design_workspace.h"
#include "jolttype/pagelets.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "jfx/jfx_scheduler.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace jfx::page {
struct FileJob {
    FileJob(const jolttype::Session &snapshot,const jolttype::Typelet &package) : session(snapshot),project(package) {}
    jolttype::Session session;
    jolttype::Typelet project;
    jolttype::Diagnostic error;
    char path[1024]{};
    int action = 0, page = 0;
    jolttype::Target target=jolttype::Target::PDF;
    int output_format=0;
    jolttype::Asset imported;
    double now = 0;
    bool success = false;
    std::atomic<bool> done{false};
};
struct PreviewJob {
    explicit PreviewJob(const jolttype::Document &source):doc(source){}
    jolttype::Document doc;
    tilly::string pdf;
    jolttype::Bitmap bitmap;
    jolttype::Diagnostic error;
    uint64_t revision=0;
    int page=0;
    bool success=false,rasterized=false;
    std::atomic<unsigned> progress{0};
    std::atomic<bool> done{false};
};
namespace {
void preview_task(void *userdata) {
    auto &job=*static_cast<PreviewJob *>(userdata);
    job.success=jolttype::publish(job.doc,jolttype::Target::PDFPoDoFo,0,job.pdf,job.error,
        [](void *p,unsigned n,const char *){static_cast<PreviewJob *>(p)->progress.store(n*8/10,std::memory_order_relaxed);},&job);
    if(job.success)job.rasterized=jolttype::render_pdf(job.pdf,size_t(job.page),1000,job.bitmap,job.error);
    job.progress.store(100,std::memory_order_relaxed);job.done.store(true,std::memory_order_release);
}
void file_task(void *userdata) {
    auto &job = *static_cast<FileJob *>(userdata);
    try {
    if (job.action == 0) job.success = job.session.load(job.path, job.now);
    else if (job.action == 1) job.success = job.session.save(job.path);
    else if(job.action==2 || job.action==3) {
        tilly::string svg;
        job.success = (job.action==3 && job.output_format==5?jolttype::compile_object(job.session.committed(),svg,job.error):
            job.action==3 && job.output_format==6?jolttype::export_source(job.session.committed(),svg,job.error):
            jolttype::publish(job.session.committed(),job.action==2?jolttype::Target::SVG:job.target,size_t(job.page), svg, job.error)) &&
            jolttype::write_file(job.path, svg, job.error);
    } else if(job.action==4) {
        tilly::string bytes;
        job.success=jolttype::build_typelet(job.project,job.error) && jolttype::pack_typelet(job.project,bytes,job.error) && jolttype::write_file(job.path,bytes,job.error);
    } else if(job.action==5) {
        struct File {FILE *p;~File(){if(p)std::fclose(p);}} file{std::fopen(job.path,"rb")};
        try {
            if(!file.p)throw std::runtime_error("Cannot open Typelet.");
            tilly::string bytes(70*1024*1024+1,'\0');size_t n=std::fread(bytes.data(),1,bytes.size(),file.p);
            if(std::ferror(file.p) || n==bytes.size())throw std::runtime_error("Typelet read failed or exceeds 70 MiB.");
            bytes.resize(n);
            job.success=jolttype::unpack_typelet(bytes,job.project,job.error);
            if(job.success && job.project.manifest.kind!="module") {
                const auto *entry=job.project.inventory.find(job.project.manifest.entry);
                job.success=entry && job.session.set_inventory(job.project.inventory,job.error) && job.session.stage(entry->bytes,job.now) && job.session.commit(job.now);
                if(!job.success && !job.error.message[0])job.error=job.session.error();
            }
        }catch(const std::exception &e){job.success=false;std::snprintf(job.error.message,sizeof(job.error.message),"%s",e.what());}
    } else if(job.action==6) {
        job.success=job.project.inventory.import_file(job.path,job.imported.id,job.imported.kind,job.imported.tags,job.error);
    } else if(job.action==7) {
        job.success=jolttype::build_typelet(job.project,job.error);
    }
    if (job.action < 2) job.error = job.session.error();
    }catch(const std::exception &e){job.success=false;std::snprintf(job.error.message,sizeof(job.error.message),"%s",e.what());}
    job.done.store(true, std::memory_order_release);
}
tilly::vector<tilly::string> split_tags(const char *text) {
    tilly::vector<tilly::string> tags;std::string_view s(text);size_t at=0;
    while(at<s.size()){auto end=s.find(',',at);if(end==s.npos)end=s.size();auto tag=s.substr(at,end-at);
        while(!tag.empty() && tag.front()==' ')tag.remove_prefix(1);
        while(!tag.empty() && tag.back()==' ')tag.remove_suffix(1);
        if(!tag.empty())tags.emplace_back(tag);
        at=end+1;}return tags;
}
void invalidate(unsigned id) {
    auto *g = ImGui::GetCurrentContext(); if (!g || !id) return;
    if (g->ActiveId == id) ImGui::ClearActiveID();
    if (g->InputTextState.ID == id) g->InputTextState.ID = 0;
    if (g->InputTextDeactivatedState.ID == id) g->InputTextDeactivatedState.ID = 0;
}
const jolttype::Element *find(const jolttype::Document &doc, const char *id) {
    for (const auto &e : doc.elements) if (e.id == id) return &e;
    return nullptr;
}
const jolttype::Entity *find_entity(const jolttype::Document &doc,const char *id) {
    for(const auto &e:doc.entities)if(e.id==id)return &e;
    return nullptr;
}
const char *asset_kind_name(jolttype::AssetKind kind) {
    static const char *names[]={"image","text","code","source","compiled","module","other","font","pagelet","kliche"};
    return names[unsigned(kind)];
}
tilly::string quote_argument(std::string_view text) {
    tilly::string out;
    for(char c:text){if(c=='\\' || c=='{' || c=='}' || c=='%')out+='\\';out+=c;}
    return out;
}
const jolttype::BundledPagelet *bundled(std::string_view name) {
    size_t count=0;const auto *catalog=jolttype::bundled_pagelets(count);
    for(size_t i=0;i<count;++i)if(name==catalog[i].name)return &catalog[i];
    return nullptr;
}
}
Workspace::Workspace() {
    if (!session.stage(jolttype::example_source(), 0) || !session.commit(0)) throw std::bad_alloc();
    if(!sync_project())throw std::bad_alloc();
    jolttype::Diagnostic error;
    if(!session.set_inventory(project.inventory,error) || !session.commit(0))throw std::bad_alloc();
    sync();
}
Workspace::~Workspace() {
    if (file_job || preview_job) scheduler_wait_idle();
    tilly::destroy(file_job);tilly::destroy(preview_job);
}
bool Workspace::sync_project() {
    try {
        if(project.manifest.kind=="module")return true;
        jolttype::Asset asset;if(const auto *old=project.inventory.find(project.manifest.entry))asset=*old;
        else {asset.id=project.manifest.entry;asset.path="main.jtype";asset.kind=jolttype::AssetKind::Source;}
        asset.bytes=session.source();jolttype::Diagnostic error;
        if(!project.inventory.put(std::move(asset),error)){std::snprintf(message,sizeof(message),"%s",error.message);return false;}
        return true;
    }catch(...){std::snprintf(message,sizeof(message),"Not enough memory to snapshot package source.");return false;}
}
void Workspace::queue_file(int action) {
    if (busy() || dirty()) return;
    if(action>=4 && !sync_project())return;
    auto *job = tilly::create<FileJob>(session,project);
    if (!job) { std::snprintf(message, sizeof(message), "Not enough memory for file operation."); return; }
    job->action = action; job->page = page; job->now = ImGui::GetTime();
    job->output_format=output_target;job->target=jolttype::Target(std::min(output_target,4));
    std::snprintf(job->path, sizeof(job->path), "%s", action==6?asset_path:action==4 || action==5?package_path:action==3?output_path:action == 2 ? export_path : path);
    if(action==6) {
        try {job->imported.id=asset_id;job->imported.kind=jolttype::AssetKind(asset_kind);job->imported.tags=split_tags(asset_tags);}
        catch(...){tilly::destroy(job);std::snprintf(message,sizeof(message),"Not enough memory for inventory import.");return;}
    }
    if (!scheduler_submit(file_task, job, 0)) {
        tilly::destroy(job); std::snprintf(message, sizeof(message), "File operation could not be queued."); return;
    }
    file_job = job; std::snprintf(message, sizeof(message), "Working...");
}
void Workspace::poll_file() {
    if (!file_job || !file_job->done.load(std::memory_order_acquire)) return;
    if (file_job->success && (file_job->action == 0 || file_job->action==5)) {
        session = std::move(file_job->session); text_pending = false; deactivate(); sync();
        hatch.accept();published();
    }
    if(file_job->success && file_job->action>=4) {
        project=std::move(file_job->project);select_asset(nullptr);
        if(file_job->action==6 || file_job->action==7){jolttype::Diagnostic error;session.set_inventory(project.inventory,error);}
        std::snprintf(package_name,sizeof(package_name),"%s",project.manifest.name.c_str());
        std::snprintf(package_version,sizeof(package_version),"%s",project.manifest.version.c_str());
        std::snprintf(package_author,sizeof(package_author),"%s",project.manifest.author.c_str());
        std::snprintf(package_description,sizeof(package_description),"%s",project.manifest.description.c_str());
        if(project.manifest.kind=="module")ide_mode=true;
    }
    std::snprintf(message, sizeof(message), "%s", file_job->success ?
        (file_job->action == 0 ? "Opened JoltType source." : file_job->action == 1 ? "Saved committed source." : file_job->action==4?"Built and saved Typelet.":file_job->action==5?"Opened Typelet.":file_job->action==6?"Imported dependency.":file_job->action==7?"Dependencies built.":"Export complete.") : file_job->error.message);
    tilly::destroy(file_job); file_job = nullptr;
}
void Workspace::published() {++revision;preview_requested=true;}
void Workspace::preview_tick() {
    if(preview_job && preview_job->done.load(std::memory_order_acquire)) {
        if(preview_job->revision==revision && preview_job->page==page) {
            if(preview_job->success){pdf=std::move(preview_job->pdf);pdf_revision=revision;}
            if(preview_job->rasterized){bitmap=std::move(preview_job->bitmap);pdf_page=page;pdf_texture=nullptr;}
            std::snprintf(pdf_message,sizeof(pdf_message),"%s",preview_job->rasterized?"PDF ready - PoDoFo / qpdf / PDFium":preview_job->error.message);
        }
        tilly::destroy(preview_job);preview_job=nullptr;
    }
    if(preview_requested && !preview_job && jolttype::pdf_available()) {
        preview_requested=false;auto *job=tilly::create<PreviewJob>(session.committed());
        if(job){job->revision=revision;job->page=page;if(scheduler_submit(preview_task,job,0)){preview_job=job;return;}tilly::destroy(job);}
        std::snprintf(pdf_message,sizeof(pdf_message),"PDF preview could not be queued.");
    }
}
void Workspace::progress_display(jfx_desktop_window_t *window) {
    if(!ImGui::CollapsingHeader("Progress Display",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(preview_job) {unsigned n=preview_job->progress.load(std::memory_order_relaxed);ImGui::ProgressBar(float(n)/100);ImGui::TextUnformatted(n<56?"PoDoFo: compiling PDF":n<80?"qpdf: validating output":"PDFium: rendering page");}
    ImGui::TextWrapped("%s",pdf_message);
    if(!jolttype::pdf_available())ImGui::TextWrapped("PDF publishing is disabled in this build.");
    if(dirty())ImGui::TextWrapped("Draft changes await Commit. This display shows the published revision.");
    if(pdf_revision!=revision || pdf_page!=page)ImGui::TextDisabled("Waiting for current page output...");
    if(!bitmap.rgba.empty()) {
        if(!pdf_texture && window)pdf_texture=jfx_desktop_window_upload_rgba8(window,bitmap.width,bitmap.height,bitmap.rgba.data());
        float width=std::max(40.f,ImGui::GetContentRegionAvail().x),height=width*float(bitmap.height)/float(bitmap.width);
        if(pdf_texture)ImGui::Image((ImTextureID)(intptr_t)pdf_texture,{width,height});
        else ImGui::Text("PDF raster: %u x %u",bitmap.width,bitmap.height);
    }
}
void Workspace::sync() {
    if (session.draft() != source) {
        invalidate(source_id);
        std::memcpy(source, session.draft().c_str(), session.draft().size() + 1);
    }
    page = std::clamp(page, 0, std::max(0, int(session.preview().pages.size()) - 1));
    if (!text_pending) {
        const auto *e = find(session.preview(), selected);
        if (e && e->kind == jolttype::Kind::Text) {
            if (e->text != text) { invalidate(text_id); std::memcpy(text, e->text.c_str(), e->text.size() + 1); }
        } else { invalidate(text_id); text[0] = 0; if (!e && !find_entity(session.preview(),selected)) selected[0] = 0; }
    }
}
void Workspace::select(const jolttype::Element *e) {
    if (text_pending) return;
    invalidate(text_id);
    std::snprintf(selected, sizeof(selected), "%s", e ? (e->entity.empty()?e->id.c_str():e->entity.c_str()) : ""); sync();
}
bool Workspace::commit(double now) {
    if (busy()) return false;
    jolttype::Diagnostic error;
    if(!session.set_inventory(project.inventory,error)){std::snprintf(message,sizeof(message),"%s",error.message);return false;}
    if (text_pending) {
        if (!session.set_text(selected, text, now)) return false;
        text_pending = false;
    }
    bool ok = session.commit(now); sync();
    if (ok) { was_dirty = false; message[0] = 0; published(); }
    return ok;
}
void Workspace::tick(double now, int interval, bool busy) {
    poll_file();
    preview_tick();
    if (file_job) return;
    if(ide_mode && auto_update && !dragging && !text_pending) {
        try {
            if(live_source!=session.draft() || live_inventory!=session.inventory()) {
                live_source=session.draft();live_inventory=session.inventory();live_pending=true;live_deadline=now+.2;
            }
            if(live_pending && now>=live_deadline) {live_pending=false;session.refresh();sync();}
        }catch(...){std::snprintf(message,sizeof(message),"Not enough memory for live preview.");}
    }
    if (!dirty()) { was_dirty = false; return; }
    if (!was_dirty) { was_dirty = true; dirty_since = now; }
    if (now < dirty_since) dirty_since = now;
    if (interval > 0 && !busy && !dragging && now - dirty_since >= interval) {
        dirty_since = now; commit(now);
    }
}
void Workspace::deactivate() { dragging = false; pdf_texture=nullptr; }
bool Workspace::history(bool redo) {
    if (busy() || !can_history(redo)) return false;
    try {
        auto next=session;
        if(!next.history(redo))return false;
        auto inventory=next.inventory()?*next.inventory():jolttype::Inventory{};
        session=std::move(next);project.inventory=std::move(inventory);
    }catch(...){std::snprintf(message,sizeof(message),"Cannot restore inventory view.");return false;}
    deactivate(); sync(); published(); return true;
}
bool Workspace::load(const char *name) {
    if (busy() || !name || std::strlen(name) >= sizeof(path)) return false;
    if (!session.load(name, ImGui::GetTime())) return false;
    std::memmove(path, name, std::strlen(name) + 1);
    text_pending = false; deactivate(); sync(); hatch.accept(); published(); message[0] = 0; return true;
}
bool Workspace::save(const char *name) {
    if (busy() || !name || std::strlen(name) >= sizeof(path)) return false;
    if (dirty()) { std::snprintf(message, sizeof(message), "Commit or revert the draft before saving."); return false; }
    if (!session.save(name)) return false;
    std::memmove(path, name, std::strlen(name) + 1);
    std::snprintf(message, sizeof(message), "Saved committed JoltType source."); return true;
}
bool Workspace::reset() {
    if (busy()) return false;
    if (!session.stage("\\jolttype{1}\n\\page{210mm}{297mm}\n", ImGui::GetTime())) return false;
    text_pending = false; deactivate(); sync(); return true;
}
void Workspace::append(const char *kind) {
    try {
        tilly::string next = session.draft();
        if (!std::strcmp(kind, "page")) next += "\n\\page{210mm}{297mm}\n";
        else {
            unsigned id = 1; char name[64];
            do { std::snprintf(name, sizeof(name), "frame%u", id++); } while (find(session.preview(), name));
            // Append to the final page, as defined by the language's source order.
            char command[256];
            std::snprintf(command, sizeof(command), !std::strcmp(kind, "text") ?
                "\n\\text{%s}{20}{20}{120}{12}{#202020}{New text}\n" :
                "\n\\rect{%s}{20}{20}{100}{40}{#e46b36}\n", name);
            next += command;
        }
        if (session.stage(next, ImGui::GetTime())) {session.refresh();sync();}
    } catch (...) { std::snprintf(message, sizeof(message), "Not enough memory to add a frame."); }
}
void Workspace::insert_pagelet(const char *type) {
    try {
        auto next=session;auto inventory=project.inventory;jolttype::Diagnostic error;
        tilly::string draft=next.draft();bool found=false;
        for(const auto &p:next.preview().pagelets)if(p.id==type)found=true;
        const auto *entry=bundled(type);
        if(!found && entry) {
            jolttype::Asset asset;asset.id=entry->asset_id;asset.path="pagelets/"+asset.id+".jtype";
            asset.kind=jolttype::AssetKind::Pagelet;asset.tags={"design",entry->category};asset.bytes=entry->source;
            const auto *old=inventory.find(asset.id);
            // Existing inventory modules belong to the document, including edits
            // made to a shipped template. Never replace them with factory source.
            if(old && old->kind!=jolttype::AssetKind::Pagelet){std::snprintf(message,sizeof(message),"The '%s' inventory ID is already in use by another kind.",entry->asset_id);return;}
            if(!old && !inventory.put(std::move(asset),error)){std::snprintf(message,sizeof(message),"%s",error.message);return;}
            draft+="\n\\use{"+tilly::string(entry->asset_id)+"}\n";
        }
        unsigned n=1;char id[64];do{std::snprintf(id,sizeof(id),"element%u",n++);}while(find(next.preview(),id) || find_entity(next.preview(),id));
        draft+="\n\\place{"+tilly::string(id)+"}{"+quote_argument(type)+"}{20}{20}{}\n";
        if(!next.set_inventory(inventory,error) || !next.stage(draft,ImGui::GetTime()) || !next.refresh()) {
            std::snprintf(message,sizeof(message),"%s",error.message[0]?error.message:next.error().message);return;
        }
        session=std::move(next);project.inventory=std::move(inventory);std::snprintf(selected,sizeof(selected),"%s",id);
        page=int(session.preview().pages.size())-1;message[0]=0;sync();
    }catch(...){std::snprintf(message,sizeof(message),"Not enough memory to insert Pagelet.");}
}
void Workspace::pagelet_panel() {
    if(!ImGui::CollapsingHeader("Elements / Pagelets",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(ImGui::BeginCombo("Pagelet / Kliche",pagelet_type)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Pagelet filter","Search name, category or description",pagelet_filter,sizeof(pagelet_filter));
        size_t count=0;const auto *catalog=jolttype::bundled_pagelets(count);
        for(size_t i=0;i<count;++i) {
            const auto &entry=catalog[i];
            if(pagelet_filter[0] && !ImStristr(entry.name,nullptr,pagelet_filter,nullptr) && !ImStristr(entry.category,nullptr,pagelet_filter,nullptr) && !ImStristr(entry.description,nullptr,pagelet_filter,nullptr))continue;
            if(ImGui::Selectable(entry.name,!std::strcmp(pagelet_type,entry.name)))std::snprintf(pagelet_type,sizeof(pagelet_type),"%s",entry.name);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s: %s",entry.category,entry.description);
        }
        for(const auto &p:session.preview().pagelets) {
            if(bundled(p.id) || (pagelet_filter[0] && !ImStristr(p.id.c_str(),nullptr,pagelet_filter,nullptr)))continue;
            if(ImGui::Selectable(p.id.c_str(),p.id==pagelet_type))std::snprintf(pagelet_type,sizeof(pagelet_type),"%s",p.id.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!session.visual_ready() || text_pending);
    if(ImGui::Button("Insert Pagelet"))insert_pagelet(pagelet_type);
    ImGui::EndDisabled();
    if(const auto *entry=bundled(pagelet_type))ImGui::TextWrapped("%s: %s",entry->category,entry->description);
    ImGui::TextWrapped("Pagelets are JoltType elements. Kliches reuse configuration; contents belong to each instance.");
}
void Workspace::capture_kliche() {
    try {
        const auto *entity=find_entity(session.preview(),selected);if(!entity)return;
        if(entity->inline_pagelet){std::snprintf(message,sizeof(message),"Name the inline Pagelet in source before creating a Kliche.");return;}
        const jolttype::Pagelet *base=nullptr;
        for(const auto &p:session.preview().pagelets)if(p.id==entity->type)base=&p;
        if(!base)return;
        jolttype::Asset asset;asset.id=kliche_name;asset.path="kliches/"+asset.id+".jtype";asset.kind=jolttype::AssetKind::Kliche;asset.tags={"design","kliche"};
        if(project.inventory.find(asset.id)){std::snprintf(message,sizeof(message),"Choose an unused Kliche inventory ID.");return;}
        auto base_id=asset.id+"-Base";
        asset.bytes="\\jolttype{1}\n\\pagelet{"+base_id+"}{\n";
        // Serialize the definition's defaults, never the selected entity's data.
        for(const auto &c:base->components)asset.bytes+=(c.configuration?"\\config{":"\\component{")+c.name+"}{"+quote_argument(c.value)+"}\n";
        asset.bytes+=base->body+"\n}\n\\kliche{"+asset.id+"}{"+base_id+"}{\n";
        for(const auto &c:entity->components)if(c.configuration)asset.bytes+="\\config{"+c.name+"}{"+quote_argument(c.value)+"}\n";
        asset.bytes+="}\n";
        auto inventory=project.inventory;auto next=session;jolttype::Diagnostic error;
        auto id=asset.id;
        if(!inventory.put(std::move(asset),error) || !next.set_inventory(inventory,error) ||
            !next.stage(next.draft()+"\n\\use{"+id+"}\n",ImGui::GetTime()) || !next.refresh()) {
            std::snprintf(message,sizeof(message),"%s",error.message[0]?error.message:next.error().message);return;
        }
        session=std::move(next);project.inventory=std::move(inventory);select_asset(project.inventory.find(id));
        std::snprintf(pagelet_type,sizeof(pagelet_type),"%s",id.c_str());sync();
        std::snprintf(message,sizeof(message),"Kliche saved to inventory with configuration only.");
    }catch(...){std::snprintf(message,sizeof(message),"Not enough memory to create Kliche.");}
}
void Workspace::component_panel() {
    const auto *entity=find_entity(session.preview(),selected);if(!entity)return;
    ImGui::SeparatorText("Pagelet entity");
    ImGui::TextWrapped("%s | %s",entity->id.c_str(),entity->inline_pagelet?"inline Pagelet":entity->type.c_str());
    if(std::strcmp(component_owner,selected)) {std::snprintf(component_owner,sizeof(component_owner),"%s",selected);component_name[0]=component_value[0]=component_model_value[0]=0;}
    if(ImGui::BeginCombo("Component / configuration",component_name)) {
        for(const auto &c:entity->components)if(ImGui::Selectable(c.name.c_str(),c.name==component_name)) {
            std::snprintf(component_name,sizeof(component_name),"%s",c.name.c_str());std::snprintf(component_value,sizeof(component_value),"%s",c.value.c_str());
            std::snprintf(component_model_value,sizeof(component_model_value),"%s",c.value.c_str());
        }
        ImGui::EndCombo();
    }
    for(const auto &c:entity->components)if(c.name==component_name) {
        if(c.value!=component_model_value) {
            invalidate(ImGui::GetID("##Component value"));
            std::snprintf(component_value,sizeof(component_value),"%s",c.value.c_str());
            std::snprintf(component_model_value,sizeof(component_model_value),"%s",c.value.c_str());
        }
        ImGui::TextDisabled("%s",c.configuration?"Configuration (included in Kliches)":"Data component (instance contents)");
    }
    ImGui::BeginDisabled(!session.visual_ready() || !component_name[0]);
    ImGui::InputTextMultiline("##Component value",component_value,sizeof(component_value),{-1,80});
    if(ImGui::Button("Apply component")){session.set_component(selected,component_name,component_value,ImGui::GetTime());sync();}
    ImGui::EndDisabled();
    ImGui::InputText("Kliche name",kliche_name,sizeof(kliche_name));
    ImGui::BeginDisabled(!session.visual_ready());
    if(ImGui::Button("Create Kliche"))capture_kliche();
    ImGui::EndDisabled();
}
void Workspace::files() {
    if (files_requested) { ImGui::OpenPopup("Page Design files"); files_requested = false; }
    if (ImGui::BeginPopupModal("Page Design files", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("JoltType source (.jtype)");
        ImGui::SetNextItemWidth(480); ImGui::InputText("Source path", path, sizeof(path));
        ImGui::BeginDisabled(dirty());
        if (ImGui::Button("Open source")) queue_file(0);
        ImGui::SameLine(); if (ImGui::Button("Save source")) queue_file(1);
        ImGui::EndDisabled();
        ImGui::SeparatorText("Publishing targets");
        ImGui::Combo("Target",&output_target,"PDF (libharu)\0SVG\0PostScript\0DVI\0PDF (PoDoFo alternative)\0JoltType bytecode (frozen)\0JoltType source (frozen)\0");
        ImGui::SetNextItemWidth(480);ImGui::InputText("Output path",output_path,sizeof(output_path));
        ImGui::BeginDisabled(dirty());if(ImGui::Button("Export document"))queue_file(3);ImGui::EndDisabled();
        if (dirty()) ImGui::TextWrapped("Commit or revert pending changes before opening or saving.");
        ImGui::Separator(); ImGui::SetNextItemWidth(480);
        ImGui::InputText("SVG path", export_path, sizeof(export_path));
        ImGui::BeginDisabled(dirty());
        if (ImGui::Button("Export page SVG")) queue_file(2);
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", session.error().message); ImGui::TextWrapped("%s", message);
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void Workspace::select_asset(const jolttype::Asset *asset) {
    try {
        std::snprintf(inventory_selection,sizeof(inventory_selection),"%s",asset?asset->id.c_str():"");
        asset_editor[0]=asset_tags[0]=asset_dependencies[0]=0;
        if(!asset)return;
        if(asset->bytes.size()<sizeof(asset_editor) && asset->bytes.find('\0')==asset->bytes.npos)
            std::memcpy(asset_editor,asset->bytes.c_str(),asset->bytes.size()+1);
        tilly::string tags,deps;for(const auto &s:asset->tags){if(!tags.empty())tags+=", ";tags+=s;}
        for(const auto &s:asset->dependencies){if(!deps.empty())deps+=", ";deps+=s;}
        std::snprintf(asset_tags,sizeof(asset_tags),"%s",tags.c_str());std::snprintf(asset_dependencies,sizeof(asset_dependencies),"%s",deps.c_str());
    }catch(...){std::snprintf(message,sizeof(message),"Not enough memory to select inventory item.");}
}
void Workspace::inventory_action(int action) {
    try {
        jolttype::Diagnostic error;
        const auto *asset=project.inventory.find(inventory_selection);
        bool ok=false;
        if(action==6) {
            jolttype::Asset code;code.id=asset_id[0]?asset_id:"MyPagelet";code.path="pagelets/"+code.id+".jtype";
            code.kind=jolttype::AssetKind::Pagelet;code.tags={"design","source"};
            code.bytes="\\jolttype{1}\n\\pagelet{"+code.id+"}{\n\\component{content}{New element}\n\\config{ink}{#182840}\n\\text{label}{0}{0}{200}{18}{@ink}{@content}\n}\n";
            if(project.inventory.find(code.id)){std::snprintf(message,sizeof(message),"Choose an unused dependency ID.");return;}
            auto id=code.id;auto inventory=project.inventory;auto next=session;
            if(inventory.put(std::move(code),error) && next.set_inventory(inventory,error) &&
                next.stage(next.draft()+"\n\\use{"+id+"}\n",ImGui::GetTime()) && next.refresh()) {
                session=std::move(next);project.inventory=std::move(inventory);select_asset(project.inventory.find(id));
                std::snprintf(pagelet_type,sizeof(pagelet_type),"%s",id.c_str());sync();ok=true;
            } else if(!error.message[0])error=next.error();
        } else if(action==3) {
            jolttype::Typelet next=project;jolttype::Asset code;
            code.id=asset_id[0]?asset_id:"tools";code.path="modules/"+code.id+".jtm";code.kind=jolttype::AssetKind::Code;code.tags={"module","source"};
            code.bytes="\\module{"+code.id+"}\n\\export{area}{width,height}{width*height}\n\\export{diagonal}{width,height}{sqrt(width*width+height*height)}\n";
            auto id=code.id;auto wasm=id+".wasm";
            if(next.inventory.find(id)){std::snprintf(message,sizeof(message),"Choose an unused dependency ID for the new module.");return;}
            if(next.inventory.put(std::move(code),error)){
                next.manifest.kind="mixed";next.manifest.build.push_back({id,wasm,"wasm"});
                next.manifest.interfaces.push_back({id+".area",wasm,"area","Compute area",{"width","height"}});
                tilly::string json;if(jolttype::manifest_text(next,json,error)){project=std::move(next);select_asset(project.inventory.find(id));ok=true;}
            }
        } else if(!asset)std::snprintf(error.message,sizeof(error.message),"Select an inventory item.");
        else if(action==0 || action==2) {
            auto next=*asset;
            if(action==0){next.tags=split_tags(asset_tags);next.dependencies=split_tags(asset_dependencies);}
            else {if(asset->id==project.manifest.entry && project.manifest.kind!="module"){std::snprintf(message,sizeof(message),"Use the JoltType source editor for the document entry.");return;}next.bytes=asset_editor;}
            ok=project.inventory.put(std::move(next),error);
        } else if(action==1) {
            if(asset->id==project.manifest.entry){std::snprintf(message,sizeof(message),"The package entry cannot be removed.");return;}
            ok=project.inventory.remove(inventory_selection,error);if(ok)select_asset(nullptr);
        } else if(action==5) {
            auto draft=session.draft()+"\n\\use{"+quote_argument(asset->id)+"}\n";
            auto next=session;
            if(next.set_inventory(project.inventory,error) && next.stage(draft,ImGui::GetTime()) && next.refresh()) {session=std::move(next);sync();ok=true;}
            else if(!error.message[0])error=next.error();
        } else if(action==4) {
            unsigned n=1;char id[64];do{std::snprintf(id,sizeof(id),"resource%u",n++);}while(find(session.preview(),id));
            char command[512];
            std::snprintf(command,sizeof(command),asset->kind==jolttype::AssetKind::Image?
                "\n\\image{%s}{20}{20}{120}{80}{%s}\n":"\n\\textfile{%s}{20}{20}{200}{12}{#202020}{%s}\n",id,asset->id.c_str());
            if(session.set_inventory(project.inventory,error)){auto draft=session.draft()+command;ok=session.stage(draft,ImGui::GetTime());if(ok){session.refresh();sync();}else error=session.error();}
        }
        if(ok)ok=session.set_inventory(project.inventory,error);
        std::snprintf(message,sizeof(message),"%s",ok?"Inventory updated. Commit to rebuild the page.":error.message);
    }catch(const std::exception &e){std::snprintf(message,sizeof(message),"%s",e.what());}
}
void Workspace::inventory_panel() {
    if(!ImGui::CollapsingHeader("Dependency inventory"))return;
    ImGui::Text("%zu dependencies (shared by Edit and IDE)",project.inventory.assets().size());
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##Inventory filter","Filter names, paths or tags",asset_filter,sizeof(asset_filter));
    if(ImGui::BeginChild("Inventory list",{0,130},ImGuiChildFlags_Borders)) {
        for(const auto &a:project.inventory.assets()) {
            bool matches=!asset_filter[0] || a.id.find(asset_filter)!=a.id.npos || a.path.find(asset_filter)!=a.path.npos || std::strstr(asset_kind_name(a.kind),asset_filter);
            for(const auto &tag:a.tags)if(tag.find(asset_filter)!=tag.npos)matches=true;
            if(matches && ImGui::Selectable(a.id.c_str(),a.id==inventory_selection))select_asset(&a);
        }
    }ImGui::EndChild();
    if(const auto *asset=project.inventory.find(inventory_selection)) {
        ImGui::TextWrapped("%s | %s | %zu bytes",asset->path.c_str(),asset_kind_name(asset->kind),asset->bytes.size());
        ImGui::InputText("Tags (comma-separated)",asset_tags,sizeof(asset_tags));
        ImGui::InputText("Requires IDs",asset_dependencies,sizeof(asset_dependencies));
        if(ImGui::Button("Apply tags / dependencies"))inventory_action(0);
        ImGui::SameLine();if(ImGui::Button("Remove asset"))inventory_action(1);
    }
    if(const auto *asset=project.inventory.find(inventory_selection)) {
        if(asset->kind==jolttype::AssetKind::Image || asset->kind==jolttype::AssetKind::Text){
            ImGui::BeginDisabled(!session.visual_ready() || text_pending);
            if(ImGui::Button("Insert into page"))inventory_action(4);
            ImGui::EndDisabled();
        }
        bool design=asset->kind==jolttype::AssetKind::Pagelet || asset->kind==jolttype::AssetKind::Kliche;
        if(design && ImGui::Button("Load design library"))inventory_action(5);
        if(asset->kind==jolttype::AssetKind::Font)ImGui::TextWrapped("Font resource stored in Typelets. The current fixed-em typesetter uses the publishing target's font.");
        bool editable=design || asset->kind==jolttype::AssetKind::Text || asset->kind==jolttype::AssetKind::Code || asset->kind==jolttype::AssetKind::Source;
        if(editable && asset->bytes.size()<sizeof(asset_editor) && asset->bytes.find('\0')==asset->bytes.npos && ImGui::TreeNode("Dependency editor")) {
            ImGui::InputTextMultiline("##Asset source",asset_editor,sizeof(asset_editor),{-1,160},ImGuiInputTextFlags_AllowTabInput);
            if(ImGui::Button("Apply asset edits"))inventory_action(2);
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    ImGui::InputText("Import path",asset_path,sizeof(asset_path));
    ImGui::InputText("Dependency ID",asset_id,sizeof(asset_id));
    ImGui::Combo("Kind",&asset_kind,"Image\0Text\0Code\0JoltType source\0Compiled document\0WASM module\0Other\0Font\0Pagelet library\0Kliche library\0");
    ImGui::BeginDisabled(dirty());
    if(ImGui::Button("Import dependency"))queue_file(6);
    ImGui::SameLine();if(ImGui::Button("Build dependencies"))queue_file(7);
    ImGui::EndDisabled();
    if(ImGui::Button("New code module"))inventory_action(3);
    ImGui::SameLine();if(ImGui::Button("New Pagelet"))inventory_action(6);
    ImGui::SameLine();if(ImGui::Button("Typelet tools")) {
        if(sync_project()){jolttype::Diagnostic d;tilly::string value;if(jolttype::manifest_text(project,value,d)){std::snprintf(manifest_editor,sizeof(manifest_editor),"%s",value.c_str());ImGui::OpenPopup("Typelet package");}}
    }
}
void Workspace::package_panel() {
    ImGui::SetNextWindowSize({720,820},ImGuiCond_FirstUseEver);
    if(!ImGui::BeginPopupModal("Typelet package",nullptr,ImGuiWindowFlags_NoCollapse))return;
    ImGui::InputText("Package name",package_name,sizeof(package_name));
    ImGui::InputText("Version",package_version,sizeof(package_version));
    ImGui::InputText("Author",package_author,sizeof(package_author));
    ImGui::InputText("Description",package_description,sizeof(package_description));
    auto update=[&](int action){
        try {
            jolttype::Typelet next=project;jolttype::Diagnostic error;tilly::string json;
            if(action==0){next.manifest.name=package_name;next.manifest.version=package_version;next.manifest.author=package_author;next.manifest.description=package_description;}
            else if(action==1){static const char *targets[]={"pdf","svg","ps","dvi","jtbc","wasm","pdf-podofo"};next.manifest.build.push_back({build_input,build_output,targets[build_target]});}
            else {
                jolttype::Manifest manifest;tilly::vector<jolttype::Asset> assets;
                if(!jolttype::parse_manifest(manifest_editor,manifest,assets,error)){std::snprintf(message,sizeof(message),"%s",error.message);return;}
                for(auto &a:assets){const auto *old=project.inventory.find(a.id);if(!old)throw std::runtime_error("Import new dependencies before listing them in MANIFEST.");a.bytes=old->bytes;}
                if(!next.inventory.replace(std::move(assets),error)){std::snprintf(message,sizeof(message),"%s",error.message);return;}next.manifest=std::move(manifest);
            }
            if(!jolttype::manifest_text(next,json,error)){std::snprintf(message,sizeof(message),"%s",error.message);return;}
            if(action==2 && !session.set_inventory(next.inventory,error)){std::snprintf(message,sizeof(message),"%s",error.message);return;}
            project=std::move(next);std::snprintf(manifest_editor,sizeof(manifest_editor),"%s",json.c_str());
            std::snprintf(package_name,sizeof(package_name),"%s",project.manifest.name.c_str());std::snprintf(package_version,sizeof(package_version),"%s",project.manifest.version.c_str());
            std::snprintf(message,sizeof(message),"Package manifest updated.");
        }catch(const std::exception &e){std::snprintf(message,sizeof(message),"%s",e.what());}
    };
    if(ImGui::Button("Apply metadata"))update(0);
    ImGui::SeparatorText("Build steps");
    ImGui::InputText("Input ID",build_input,sizeof(build_input));ImGui::InputText("Output ID / path",build_output,sizeof(build_output));
    ImGui::Combo("Build target",&build_target,"PDF (libharu)\0SVG\0PostScript\0DVI\0Compiled JoltType\0WASM module\0PDF (PoDoFo alternative)\0");
    if(ImGui::Button("Add build step"))update(1);
    ImGui::TextUnformatted("MANIFEST (KSON / JSON profile)");
    ImGui::InputTextMultiline("##Typelet manifest",manifest_editor,sizeof(manifest_editor),{-1,200},ImGuiInputTextFlags_AllowTabInput);
    if(ImGui::Button("Apply MANIFEST"))update(2);
    ImGui::InputText("Typelet path",package_path,sizeof(package_path));
    ImGui::BeginDisabled(dirty());
    if(ImGui::Button("Build + Pack Typelet"))queue_file(4);
    ImGui::SameLine();if(ImGui::Button("Open Typelet"))queue_file(5);
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",message);
    if(ImGui::Button("Close package tools"))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
void Workspace::module_panel() {
    if(project.manifest.interfaces.empty() || !ImGui::CollapsingHeader("Typelet module actions"))return;
    for(size_t i=0;i<project.manifest.interfaces.size() && i<64;++i){const auto &ui=project.manifest.interfaces[i];ImGui::PushID(int(i));
        ImGui::TextUnformatted(ui.label.c_str());
        for(size_t j=0;j<ui.parameters.size() && j<16;++j)ImGui::InputDouble(ui.parameters[j].c_str(),&module_arguments[i][j]);
        if(ImGui::Button("Run action")) {
            auto *asset=project.inventory.find(ui.module);jolttype::Diagnostic error;jolttype::Module module;double value=0;
            if(!asset)std::snprintf(message,sizeof(message),"Build the '%s' dependency first.",ui.module.c_str());
            else if(module.load(asset->bytes,error) && module.call(ui.entry.c_str(),module_arguments[i],ui.parameters.size(),value,error))std::snprintf(message,sizeof(message),"%s = %.12g",ui.label.c_str(),value);
            else std::snprintf(message,sizeof(message),"%s",error.message);
        }
        ImGui::PopID();
    }
}
void Workspace::canvas() {
    const auto &doc = session.preview();
    if (doc.pages.empty()) return;
    const auto &p = doc.pages[size_t(page)];
    ImVec2 origin = ImGui::GetCursorScreenPos();
    // Fit the page to the entire assigned width; scrolling retains its physical
    // aspect ratio. Zoom is relative to this fit, including after a resize.
    canvas_scale=std::max(1.f,ImGui::GetContentRegionAvail().x)/float(p.width)*zoom;
    const float scale=canvas_scale;
    ImVec2 size(float(p.width) * scale, float(p.height) * scale);
    ImGui::InvisibleButton("Page canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered(); auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32_WHITE);
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    auto &io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(0) && session.visual_ready() && !text_pending) {
        const jolttype::Element *hit = nullptr;
        double x = double(io.MousePos.x - origin.x) / double(scale), y = double(io.MousePos.y - origin.y) / double(scale);
        for (const auto &e : doc.elements) if (e.page == size_t(page) && x >= e.x && x <= e.x + e.width && y >= e.y && y <= e.y + e.height) hit = &e;
        select(hit);
        if (hit) {
            const auto *entity=find_entity(doc,selected);
            dragging = true; drag_x = entity?entity->x:hit->x; drag_y = entity?entity->y:hit->y; mouse_x = io.MousePos.x; mouse_y = io.MousePos.y;
        }
    }
    if (dragging && ImGui::IsKeyPressed(ImGuiKey_Escape)) dragging = false;
    double next_x = drag_x, next_y = drag_y;
    if (dragging) {
        const auto *e = find(doc, selected);
        const auto *entity=find_entity(doc,selected);
        if (e || entity) {
            next_x = std::clamp(drag_x + double(io.MousePos.x - mouse_x) / double(scale), 0.0, std::max(0.0, p.width - (entity?entity->width:e->width)));
            next_y = std::clamp(drag_y + double(io.MousePos.y - mouse_y) / double(scale), 0.0, std::max(0.0, p.height - (entity?entity->height:e->height)));
        } else dragging = false;
    }
    for (const auto &e : doc.elements) {
        if (e.page != size_t(page)) continue;
        bool active = e.id == selected || (!e.entity.empty() && e.entity==selected);
        double x = active && dragging ? e.x+next_x-drag_x : e.x, y = active && dragging ? e.y+next_y-drag_y : e.y;
        ImVec2 a(origin.x + float(x) * scale, origin.y + float(y) * scale);
        ImVec2 b(a.x + float(e.width) * scale, a.y + float(e.height) * scale);
        auto color = IM_COL32((e.color >> 16) & 255, (e.color >> 8) & 255, e.color & 255, 255);
        if (e.kind == jolttype::Kind::Rectangle) draw->AddRectFilled(a, b, color);
        else if(e.kind==jolttype::Kind::Image) {
            // A bounded thumbnail keeps dragging independent of the PDF worker.
            unsigned nx=std::min(32u,e.image_width),ny=std::min(32u,e.image_height);
            for(unsigned iy=0;iy<ny;++iy)for(unsigned ix=0;ix<nx;++ix){size_t index=(size_t(iy*e.image_height/ny)*e.image_width+ix*e.image_width/nx)*4;
                auto c=IM_COL32(e.pixels[index],e.pixels[index+1],e.pixels[index+2],e.pixels[index+3]);
                draw->AddRectFilled({a.x+(b.x-a.x)*float(ix)/float(nx),a.y+(b.y-a.y)*float(iy)/float(ny)},
                    {a.x+(b.x-a.x)*float(ix+1)/float(nx),a.y+(b.y-a.y)*float(iy+1)/float(ny)},c);}
        }
        else {
            float ty = a.y;
            for (const auto &line : e.lines) {
                // Fixed-em layout uses one cell per Unicode scalar. Render each
                // glyph at the same advance so wrapping matches the compiler.
                float tx = a.x;
                for (size_t i = 0; i < line.text.size();) {
                    size_t end = i + 1;
                    while (end < line.text.size() && (static_cast<unsigned char>(line.text[end]) & 0xc0) == 0x80) ++end;
                    draw->AddText(ImGui::GetFont(), float(e.size) * scale, {tx, ty}, color, line.text.data() + i, line.text.data() + end);
                    tx += float(e.size * .6) * scale; i = end;
                }
                ty += float(e.size * 1.2) * scale;
            }
        }
        if (active) draw->AddRect(a, b, IM_COL32(38, 140, 240, 255), 0.f, 2.f);
    }
    draw->PopClipRect();
    if (dragging && !ImGui::IsMouseDown(0)) {
        dragging = false;
        if (std::abs(next_x - drag_x) > .001 || std::abs(next_y - drag_y) > .001) {
            session.move(selected, next_x, next_y, ImGui::GetTime()); sync();
        }
    }
}
void Workspace::draw(jfx_desktop_window_t *window) {
    sync();
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Commit")) commit(ImGui::GetTime());
    ImGui::SameLine(); if (ImGui::Button("Revert draft")) {
        try {
            auto next=session;
            if(next.revert()) {
                auto inventory=next.inventory()?*next.inventory():jolttype::Inventory{};
                session=std::move(next);project.inventory=std::move(inventory);
                text_pending = false; deactivate(); sync();
            }
        }catch(...){std::snprintf(message,sizeof(message),"Cannot restore inventory view.");}
    }
    ImGui::SameLine(); if (ImGui::Button("Files...")) files_requested = true;
    ImGui::SameLine(); ImGui::TextUnformatted(dirty() ? "Draft - uncommitted" : "Committed");
    ImGui::SameLine();if(ImGui::Button(show_progress?"Hide Progress Display":"Show Progress Display")){show_progress=!show_progress;if(show_progress)preview_requested=true;}
    if(ImGui::BeginTabBar("Page Design modes")) {
        if(ImGui::BeginTabItem("Edit")){ide_mode=false;ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("IDE Mode")){ide_mode=true;ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
    if(ide_mode) {
        if(ImGui::Button(show_page_preview?"Hide page preview":"Show page preview")){show_page_preview=!show_page_preview;dragging=false;}
        ImGui::SameLine();if(ImGui::Checkbox("Auto-update preview",&auto_update) && auto_update){live_pending=true;live_deadline=ImGui::GetTime();}
        ImGui::SameLine();if(ImGui::Button("Update preview")){session.refresh();sync();}
    }
    ImGui::BeginDisabled(dirty());
    if (ImGui::Button("New document")) reset();
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!session.visual_ready() || text_pending);
    if (ImGui::Button("Add text")) append("text");
    ImGui::SameLine(); if (ImGui::Button("Add rectangle")) append("rect");
    ImGui::SameLine(); if (ImGui::Button("Add page")) append("page");
    ImGui::SameLine(); if (ImGui::Button("Insert Pagelet")) insert_pagelet(pagelet_type);
    ImGui::EndDisabled();
    const auto &error = session.error();
    if (error.message[0]) ImGui::TextWrapped("Line %zu, column %zu: %s", error.line, error.column, error.message);
    if (message[0]) ImGui::TextWrapped("%s", message);
    bool page_visible=!ide_mode || show_page_preview;
    if (ImGui::BeginTable(ide_mode?(page_visible?"Page Design IDE layout":"Page Design IDE code layout"):"Page Design layout", 1+int(page_visible)+int(show_progress),
        ImGuiTableFlags_NoSavedSettings | (ide_mode?ImGuiTableFlags_SizingStretchProp:ImGuiTableFlags_Resizable))) {
        ImGui::TableSetupColumn("Source", ide_mode?ImGuiTableColumnFlags_WidthStretch:ImGuiTableColumnFlags_WidthFixed, ide_mode?2.f:420.f);
        if(page_visible)ImGui::TableSetupColumn("Page", ImGuiTableColumnFlags_WidthStretch,1.f);
        if(show_progress)ImGui::TableSetupColumn("Progress",ImGuiTableColumnFlags_WidthFixed,330);
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("JoltType editor", ImVec2(0, 0))) {
            ImGui::TextUnformatted("JoltType source");
            if(ide_mode) {
                if(ImGui::Button("Compile"))commit(ImGui::GetTime());
                ImGui::SameLine();if(ImGui::Button("Package tools")) {if(sync_project()){jolttype::Diagnostic d;tilly::string value;if(jolttype::manifest_text(project,value,d)){std::snprintf(manifest_editor,sizeof(manifest_editor),"%s",value.c_str());ImGui::OpenPopup("Typelet package");}}}
            } else {
                if(ImGui::Button("Hatch into JoltType")){jolttype::Diagnostic d;if(!hatch.begin(session,d))std::snprintf(message,sizeof(message),"%s",d.message);}
            }
            if(hatch.active()){
                ImGui::SameLine();if(ImGui::Button("Restore hatch")){jolttype::Diagnostic d;if(hatch.restore(session,ImGui::GetTime(),d)){text_pending=false;sync();}else std::snprintf(message,sizeof(message),"%s",d.message);}
                if(ImGui::Button("Keep manual code"))hatch.accept();
            }
            ImGui::TextWrapped("Commit publishes changes; Save writes the committed revision. Preview updates do not commit.");
            ImGui::BeginDisabled(text_pending);
            source_id = ImGui::GetID("##JoltType source");
            if(ide_mode || hatch.active() || ImGui::CollapsingHeader("Source preview")) {
            if (ImGui::InputTextMultiline("##JoltType source", source, sizeof(source), ImVec2(-1, 230), ImGuiInputTextFlags_AllowTabInput | ((!ide_mode && !hatch.active())?ImGuiInputTextFlags_ReadOnly:0)))
                session.stage(source, ImGui::GetTime());
            }
            ImGui::EndDisabled();
            if (!session.visual_ready()) ImGui::TextWrapped("Preview is stale. Update preview or Commit to compile the draft.");
            if (const auto *e = find(session.preview(), selected)) {
                ImGui::SeparatorText("Selected frame");
                ImGui::Text("%s  |  %.2f, %.2f pt", e->id.c_str(), e->x, e->y);
                if (e->kind == jolttype::Kind::Text && e->asset.empty()) {
                    ImGui::BeginDisabled(!session.visual_ready());
                    ImGui::TextUnformatted("Frame text");
                    text_id = ImGui::GetID("##Frame text");
                    if (ImGui::InputTextMultiline("##Frame text", text, sizeof(text), ImVec2(-1, 120))) text_pending = true;
                    ImGui::EndDisabled();
                }
            }
            component_panel();
            pagelet_panel();
            if(ImGui::CollapsingHeader("Current page code")) {
                const auto &pages=session.committed().pages;
                if(size_t(page)<pages.size()){const auto &p=pages[size_t(page)];if(p.source_end<=session.source().size())ImGui::TextUnformatted(session.source().data()+p.source_begin,session.source().data()+p.source_end);}
            }
            inventory_panel();
            module_panel();
            package_panel();
            if (ImGui::CollapsingHeader("Language reference")) {
                ImGui::TextWrapped("\\jolttype{1}\n\\page{width}{height}\n\\text{id}{x}{y}{width}{size}{#RRGGBB}{text}\n\\rect{id}{x}{y}{width}{height}{#RRGGBB}\nUnits: pt, mm, cm, in. Escape \\, {, }, %% with a backslash. %% comments outside arguments. Newlines in text create lines. New frames append to the last page.");
                ImGui::TextUnformatted("\\pagelet{name}{body}\n\\component{name}{default}\n\\config{name}{default}\n\\kliche{name}{base}{config assignments}\n\\place{id}{type}{x}{y}{assignments}\n\\inline{id}{x}{y}{body}\n\\use{inventory-id}\nUse {@name} for a whole-argument component reference.");
            }
        }
        ImGui::EndChild();
        if(page_visible) {
        ImGui::TableNextColumn();
        int count = int(session.preview().pages.size());
        ImGui::SetNextItemWidth(70);
        int shown = page + 1;
        if (ImGui::SliderInt("Page", &shown, 1, std::max(1, count))) { page = shown - 1; deactivate(); select(nullptr); preview_requested=true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        ImGui::SliderFloat("Zoom", &zoom, .25f, 2, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::BeginChild("Page preview", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) canvas();
        ImGui::EndChild();
        }
        if(show_progress){ImGui::TableNextColumn();if(ImGui::BeginChild("PDF progress pane",{0,0},ImGuiChildFlags_Borders))progress_display(window);ImGui::EndChild();}
        ImGui::EndTable();
    }
    files();
    ImGui::EndDisabled();
}
}
