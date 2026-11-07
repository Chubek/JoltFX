#include "jolttype/api.h"
#include "jolttype/project.hpp"
#include <cassert>
#include <cstring>
static bool collect(void *p,const void *bytes,size_t n) {
    static_cast<tilly::string *>(p)->append(static_cast<const char *>(bytes),n);return true;
}
static jt_result target(void *,const jt_document *doc,size_t page,jt_sink sink,jt_error *) {
    jt_page_info p{};assert(jt_document_page(doc,page,&p)==JT_OK && p.width>500);
    jt_element_info e{};assert(jt_document_element(doc,0,&e)==JT_OK);
    jt_line_info line{};assert(jt_document_line(doc,0,0,&line)==JT_OK);
    return sink.write(sink.user,line.utf8,std::strlen(line.utf8))?JT_OK:JT_ERROR;
}
int main() {
    jt_error error{};auto *p=jt_project_create();assert(p);
    tilly::string bytes;jt_sink sink{&bytes,collect};
    jt_target_desc t{sizeof(t),JOLTTYPE_API_MAJOR,"text","txt",nullptr,target};
    assert(jt_project_register_target(p,&t,&error)==JT_OK);
    assert(jt_project_export(p,"text",0,sink,&error)==JT_OK && bytes=="Page Design");
    if(jt_has_pdf()) {
        bytes.clear();assert(jt_project_export(p,"pdf",0,sink,&error)==JT_OK);
        assert(bytes.find("JoltType / libharu")!=bytes.npos);
        bytes.clear();assert(jt_project_export(p,"pdf-podofo",0,sink,&error)==JT_OK);
        assert(bytes.find("JoltType / PoDoFo")!=bytes.npos);
    }
    const char *tags[]={"copy"};jt_asset_desc asset{sizeof(asset),"copy","copy.txt",JT_TEXT,tags,1,nullptr,0,"First version",13};
    assert(jt_project_put_asset(p,&asset,&error)==JT_OK);
    assert(jt_project_export(p,"pdf",0,sink,&error)==JT_ERROR);
    const char *source="\\jolttype{1}\\page{600}{800}\\textfile{body}{20}{20}{300}{12}{#202020}{copy}";
    assert(jt_project_stage(p,source,std::strlen(source),1,&error)==JT_OK);
    assert(jt_project_commit(p,2,&error)==JT_OK);
    asset.bytes="Second version";asset.length=14;
    assert(jt_project_put_asset(p,&asset,&error)==JT_OK);
    assert(jt_project_commit(p,3,&error)==JT_OK);
    assert(jt_project_history(p,false,&error)==JT_OK);
    auto *doc=jt_project_document(p);jt_line_info line{};
    assert(jt_document_line(doc,0,0,&line)==JT_OK && std::strcmp(line.utf8,"First version")==0);jt_document_destroy(doc);
    assert(jt_project_history(p,true,&error)==JT_OK);
    bytes.clear();assert(jt_project_pack(p,sink,&error)==JT_OK);
    auto *other=jt_project_create();assert(other);
    assert(jt_project_unpack(other,bytes.data(),bytes.size(),4,&error)==JT_OK);
    doc=jt_project_document(other);assert(jt_document_line(doc,0,0,&line)==JT_OK && std::strcmp(line.utf8,"Second version")==0);jt_document_destroy(doc);
    assert(jt_project_asset_count(other)==2);
    const char *pagelet="\\jolttype{1}\\pagelet{Title}{\\component{content}{Default}\\text{label}{0}{0}{200}{12}{#123456}{@content}}\\page{600}{800}\\place{heading}{Title}{20}{20}{}";
    assert(jt_project_stage(other,pagelet,std::strlen(pagelet),5,&error)==JT_OK);
    assert(jt_project_refresh(other,&error)==JT_OK);
    doc=jt_project_preview_document(other);assert(doc && jt_document_entity_count(doc)==1);
    jt_entity_info entity{};jt_component_info component{};
    assert(jt_document_entity(doc,0,&entity)==JT_OK && std::strcmp(entity.id,"heading")==0);
    assert(jt_document_component(doc,0,0,&component)==JT_OK && !component.configuration && std::strcmp(component.value,"Default")==0);
    assert(std::strcmp(jt_document_element_entity(doc,0),"heading")==0);
    jt_document_destroy(doc);
    assert(jt_project_set_component(other,"heading","content","New contents",6,&error)==JT_OK);
    assert(jt_project_export(other,"jtbc",0,sink,&error)==JT_ERROR);
    assert(jt_project_commit(other,7,&error)==JT_OK);
    bytes.clear();assert(jt_project_export(other,"jtbc",0,sink,&error)==JT_OK);
    jolttype::Document frozen;jolttype::Diagnostic d;
    assert(jolttype::load_object(bytes,frozen,d) && frozen.elements[0].text=="New contents");
    bytes.clear();assert(jt_project_export(other,"jtype",0,sink,&error)==JT_OK);
    assert(jolttype::compile(bytes,frozen,d) && frozen.entities.empty());
    jt_project_destroy(other);jt_project_destroy(p);
}
