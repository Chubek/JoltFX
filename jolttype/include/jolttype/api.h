#ifndef JOLTTYPE_API_H
#define JOLTTYPE_API_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JOLTTYPE_API_MAJOR 1u
#define JOLTTYPE_API_MINOR 1u
typedef struct jt_project jt_project;
typedef struct jt_document jt_document;
typedef struct jt_module jt_module;
typedef struct {size_t line,column;char message[256];} jt_error;
typedef enum {JT_OK=0,JT_ERROR=-1,JT_UNAVAILABLE=-2} jt_result;
typedef enum {JT_IMAGE,JT_TEXT,JT_CODE,JT_SOURCE,JT_COMPILED,JT_MODULE,JT_OTHER} jt_asset_kind;
typedef struct {void *user;bool (*write)(void *user,const void *bytes,size_t length);} jt_sink;
typedef struct {
    size_t size;const char *id,*path;jt_asset_kind kind;
    const char *const *tags;size_t tag_count;
    const char *const *dependencies;size_t dependency_count;
    const void *bytes;size_t length;
} jt_asset_desc;
typedef struct {
    const char *id,*path;jt_asset_kind kind;size_t length,tag_count,dependency_count;
} jt_asset_info;
typedef struct {double width,height;} jt_page_info;
typedef enum {JT_DRAW_TEXT,JT_DRAW_RECTANGLE,JT_DRAW_IMAGE} jt_draw_kind;
typedef struct {
    jt_draw_kind kind;const char *id;size_t page;
    double x,y,width,height,font_size;uint32_t rgb;size_t line_count;
    const uint8_t *rgba;unsigned image_width,image_height;
} jt_element_info;
typedef struct {const char *utf8;double width;} jt_line_info;
// Targets are synchronous; callback/userdata remain valid until project destroy.
// Emit into the supplied sink and return failure if any sink write fails.
typedef struct {
    size_t size;unsigned abi_major;const char *name,*extension;void *user;
    jt_result (*emit)(void *,const jt_document *,size_t selected_page,jt_sink,jt_error *);
} jt_target_desc;
typedef struct {
    const char *id,*module,*entry,*label;size_t parameter_count;
} jt_interface_info;
// A frontend can use the built-in WAMR executor or supply its own WASM executor
// (e.g. the browser's WebAssembly host). No GUI/vendor types cross this boundary.
typedef struct {
    size_t size;void *user;
    jt_result (*execute)(void *,const void *wasm,size_t length,const char *entry,
        const double *args,size_t count,double *result,jt_error *);
    void (*action_result)(void *,const char *interface_id,double result);
} jt_frontend_host;
bool jt_has_pdf(void);
bool jt_has_pdfium(void);
bool jt_has_modules(void);
jt_project *jt_project_create(void);
void jt_project_destroy(jt_project *);
// Borrowed views last until the next successful mutation of this project.
const char *jt_project_source(const jt_project *,bool committed);
jt_result jt_project_stage(jt_project *,const char *,size_t length,double now,jt_error *);
jt_result jt_project_commit(jt_project *,double now,jt_error *);
jt_result jt_project_revert(jt_project *,jt_error *);
jt_result jt_project_history(jt_project *,bool redo,jt_error *);
jt_result jt_project_hatch(jt_project *,jt_error *);
jt_result jt_project_restore_hatch(jt_project *,double now,jt_error *);
jt_result jt_project_put_asset(jt_project *,const jt_asset_desc *,jt_error *);
jt_result jt_project_remove_asset(jt_project *,const char *id,jt_error *);
size_t jt_project_asset_count(const jt_project *);
jt_result jt_project_asset(const jt_project *,size_t index,jt_asset_info *);
const char *jt_project_asset_tag(const jt_project *,size_t index,size_t tag);
const char *jt_project_asset_dependency(const jt_project *,size_t index,size_t dependency);
jt_result jt_project_manifest(const jt_project *,jt_sink,jt_error *);
jt_result jt_project_set_manifest(jt_project *,const char *,size_t length,jt_error *);
jt_result jt_project_build(jt_project *,jt_error *);
jt_result jt_project_pack(jt_project *,jt_sink,jt_error *);
jt_result jt_project_unpack(jt_project *,const void *,size_t length,double now,jt_error *);
jt_document *jt_project_document(const jt_project *);
void jt_document_destroy(jt_document *);
size_t jt_document_page_count(const jt_document *);
size_t jt_document_element_count(const jt_document *);
jt_result jt_document_page(const jt_document *,size_t,jt_page_info *);
jt_result jt_document_element(const jt_document *,size_t,jt_element_info *);
jt_result jt_document_line(const jt_document *,size_t element,size_t line,jt_line_info *);
jt_result jt_project_register_target(jt_project *,const jt_target_desc *,jt_error *);
// Built-ins: pdf (libharu final output), pdf-podofo (alternative), svg, ps, dvi.
jt_result jt_project_export(const jt_project *,const char *target,size_t page,jt_sink,jt_error *);
jt_result jt_compile_module(const char *,size_t,jt_sink,jt_error *);
jt_module *jt_module_create(void);
void jt_module_destroy(jt_module *);
jt_result jt_module_load(jt_module *,const void *,size_t,jt_error *);
jt_result jt_module_call(jt_module *,const char *,const double *,size_t,double *,jt_error *);
size_t jt_project_interface_count(const jt_project *);
jt_result jt_project_interface(const jt_project *,size_t,jt_interface_info *);
const char *jt_project_interface_parameter(const jt_project *,size_t,size_t);
jt_result jt_project_invoke(jt_project *,size_t interface_index,const double *,size_t,
    const jt_frontend_host *,double *result,jt_error *);
#ifdef __cplusplus
}
#endif
#endif
