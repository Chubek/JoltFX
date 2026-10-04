#include "jfx/jfx_plugin_sdk.h"
#include "jfx/jfx_export.h"
#include "jfx/jfx_color.h"
#include "jfx_test_backend.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const jfx_plugin_api_t *services;
static int finalized,events,fail_init,fail_process,fail_action;
static jfx_plugin_host_t *callback_host;
static uint32_t callback_plugin;
static jfx_result_t callback_unload;
static jfx_result_t process(void *userdata,const jfx_plugin_image_t *image) {
    (void)userdata;
    size_t samples=(size_t)image->width*image->height*4;
    for (size_t i=0;i<(fail_process==4?1:samples);++i)
        image->output[i]=(i%4==3)?image->input[i]:image->input[i]*image->parameters[0];
    if (fail_process==1) return JFX_ERROR_PLUGIN_FAILURE;
    if (fail_process==2) image->output[0]=NAN;
    if (fail_process==3) image->output[3]=2;
    return JFX_SUCCESS;
}
static void event_handler(jfx_event_type_t type,void *data,void *userdata) {
    (void)type; (void)data;
    if (userdata) ++*(int *)userdata; else ++events;
    if (callback_host) callback_unload=jfx_plugin_host_unload(callback_host,callback_plugin);
}
static jfx_result_t peer_initialize(const jfx_plugin_api_t *api,void **out) {
    static int peer_events; *out=&peer_events;
    return api->subscribe(api->context,JFX_EVENT_UI_INPUT,event_handler,&peer_events);
}
static jfx_result_t graph_action(void *userdata,const jfx_plugin_action_context_t *context) {
    (void)userdata; char json[4096];
    assert(services->editor_kind(context->editor)==JFX_PROJECT_KIND_GRAPH);
    assert(services->graph_state(context->editor,json,sizeof(json))==JFX_SUCCESS && strstr(json,"solid"));
    jfx_result_t r=services->editor_command(context->editor,"node.add",0,0,0,0,"color");
    if (r==JFX_SUCCESS) r=services->editor_command(context->editor,"node.param",1,0,0,.4,"r");
    if (r==JFX_SUCCESS) r=services->editor_command(context->editor,"node.param",1,0,0,.2,"g");
    if (r==JFX_SUCCESS) r=services->editor_command(context->editor,"node.connect",1,context->node,0,0,"");
    return r;
}
static jfx_result_t action(void *userdata,const jfx_plugin_action_context_t *context) {
    (void)userdata;
    jfx_result_t r=services->editor_command(context->editor,"effect.add",context->track,context->clip,0,0,"org.test.scale");
    if (r!=JFX_SUCCESS) return r;
    r=services->editor_command(context->editor,"effect.param",context->track,context->clip,0,.75,"scale");
    if (r==JFX_SUCCESS) r=services->editor_command(context->editor,"effect.param",context->track,context->clip,0,.5,"scale");
    if (r!=JFX_SUCCESS) return r;
    return fail_action?JFX_ERROR_PLUGIN_FAILURE:JFX_SUCCESS;
}
static jfx_result_t initialize(const jfx_plugin_api_t *api,void **out) {
    services=api; *out=NULL;
    assert(api->size>=sizeof(*api) && api->sdk_major==JFX_PLUGIN_SDK_MAJOR);
    assert(!api->allocate(api->context,16,3));
    void *memory=api->allocate(api->context,32,16); assert(memory); api->deallocate(api->context,memory);
    char param_name[]="scale",param_label[]="Scale \"quoted\"\n";
    const jfx_param_desc_t params[]={ {param_name,param_label,0,2,.5f,.01f,false} };
    jfx_plugin_effect_desc_t desc={sizeof(desc),"org.test.scale","Scale \"Test\"\n","Color Grading",1,params,process,NULL};
    assert(api->register_effect(api->context,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    if (fail_init==2) return api->register_effect(api->context,&desc);
    jfx_result_t r=api->register_effect(api->context,&desc); if (r!=JFX_SUCCESS) return r;
    assert(api->register_effect(api->context,&desc)==JFX_ERROR_ALREADY_EXISTS);
    desc.name="bad name"; assert(api->register_effect(api->context,&desc)==JFX_ERROR_INVALID_ARGUMENT);
    /* Project tokens are bounded too: registration cannot accept names that
     * would make a later save/history/transaction round-trip fail. */
    char oversized[JFX_PLUGIN_MAX_ID_LENGTH+2]; memset(oversized,'a',sizeof(oversized)-1); oversized[sizeof(oversized)-1]=0;
    desc.name=oversized; assert(api->register_effect(api->context,&desc)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_param_desc_t too_long={oversized,"Too long",0,1,0,.01f,false};
    desc.name="org.test.too_long"; desc.parameters=&too_long;
    assert(api->register_effect(api->context,&desc)==JFX_ERROR_INVALID_ARGUMENT);
    memset(param_name,'x',sizeof(param_name)-1); memset(param_label,'x',sizeof(param_label)-1);
    const jfx_plugin_kernel_desc_t bad={sizeof(bad),"org.test.bad","Bad","Effects","","(defkernel bad [x y c] (unknown x))"};
    assert(api->register_kernel(api->context,&bad)==JFX_ERROR_INVALID_ARGUMENT);
    const jfx_plugin_action_desc_t a={sizeof(a),"org.test.apply","Apply Scale",JFX_PROJECT_KIND_SEQUENCE,action,NULL};
    assert(api->register_action(api->context,&a)==JFX_SUCCESS);
    const jfx_plugin_action_desc_t graph={sizeof(graph),"org.test.graph","Graph Edit",JFX_PROJECT_KIND_GRAPH,graph_action,NULL};
    assert(api->register_action(api->context,&graph)==JFX_SUCCESS);
    assert(api->subscribe(api->context,JFX_EVENT_UI_INPUT,event_handler,NULL)==JFX_SUCCESS);
    assert(api->subscribe(api->context,(jfx_event_type_t)999,event_handler,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    return fail_init?JFX_ERROR_PLUGIN_FAILURE:JFX_SUCCESS;
}
static void shutdown(void *userdata) { (void)userdata; ++finalized; }
static jfx_plugin_definition_t definition(void) {
    jfx_plugin_definition_t d={sizeof(d),JFX_PLUGIN_SDK_MAJOR,0,
        {sizeof(jfx_plugin_desc_t),"org.test.plugin","Test Plugin","Tests",JFX_PLUGIN_VERSION(1,0,0),1,
            JFX_PLUGIN_CAP_KERNELS|JFX_PLUGIN_CAP_EDITOR|JFX_PLUGIN_CAP_EVENTS},initialize,shutdown};
    return d;
}
int main(int argc,char **argv) {
    assert(argc==3);
    jfx_engine_config_t config={.backend_name=jfx_test_backend()}; jfx_engine_t *engine=NULL;
    assert(jfx_engine_init(&config,&engine)==JFX_SUCCESS);
    jfx_plugin_host_t *host=NULL; assert(jfx_plugin_host_create(engine,&host)==JFX_SUCCESS);
    jfx_plugin_definition_t d=definition(); uint32_t id=999;
    assert(jfx_plugin_host_attach(NULL,&d,&id)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_plugin_host_attach(host,NULL,&id)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_plugin_host_attach(host,&d,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    d.sdk_major++; assert(jfx_plugin_host_attach(host,&d,&id)==JFX_ERROR_VERSION_MISMATCH); d=definition();
    d.minimum_sdk_minor++; assert(jfx_plugin_host_attach(host,&d,&id)==JFX_ERROR_VERSION_MISMATCH); d=definition();
    d.info.capabilities=JFX_PLUGIN_CAP_TYPES; assert(jfx_plugin_host_attach(host,&d,&id)==JFX_ERROR_NOT_IMPLEMENTED); d=definition();
    fail_init=1; assert(jfx_plugin_host_attach(host,&d,&id)==JFX_ERROR_PLUGIN_FAILURE && !id);
    assert(!jfx_node_kind_find("org.test.scale") && finalized==1 && !jfx_plugin_host_count(host));
    event_publish(JFX_EVENT_UI_INPUT,NULL); assert(!events);
    fail_init=2; d.info.capabilities=JFX_PLUGIN_CAP_EVENTS;
    assert(jfx_plugin_host_attach(host,&d,&id)==JFX_ERROR_PLUGIN_FAILURE);
    fail_init=0; d=definition();
    assert(jfx_plugin_host_attach(host,&d,&id)==JFX_SUCCESS);
    assert(services->register_action(services->context,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_plugin_info_t info={.size=sizeof(info)}; uint32_t found=0;
    assert(jfx_plugin_host_info_at(host,0,&found,&info)==JFX_SUCCESS && found==id);
    assert(!strcmp(info.identifier,"org.test.plugin"));
    assert(jfx_plugin_host_info_at(host,1,&found,&info)==JFX_ERROR_NOT_FOUND);
    assert(jfx_plugin_host_info_at(NULL,0,&found,&info)==JFX_ERROR_INVALID_ARGUMENT);
    uint32_t duplicate=1;
    assert(jfx_plugin_host_attach(host,&d,&duplicate)==JFX_ERROR_ALREADY_EXISTS && !duplicate);
    assert(jfx_plugin_host_action_count(host)==2 && !jfx_plugin_host_action_count(NULL));
    jfx_plugin_action_info_t a={.size=sizeof(a)};
    assert(jfx_plugin_host_action_info(host,0,&a)==JFX_SUCCESS && a.plugin_id==id);
    assert(jfx_plugin_host_action_info(host,2,&a)==JFX_ERROR_NOT_FOUND);
    assert(jfx_plugin_host_action_info(host,0,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    callback_host=host; callback_plugin=id;
    event_publish(JFX_EVENT_UI_INPUT,NULL); assert(events==1 && callback_unload==JFX_ERROR_BUSY);
    callback_host=NULL;
    /* Identical event relay functions are removed by exact subscription owner. */
    jfx_plugin_definition_t peer=definition(); peer.info.identifier="org.test.peer"; peer.info.capabilities=JFX_PLUGIN_CAP_EVENTS;
    peer.initialize=peer_initialize; peer.shutdown=NULL; uint32_t peer_id=0;
    assert(jfx_plugin_host_attach(host,&peer,&peer_id)==JFX_SUCCESS);
    assert(jfx_plugin_host_unload(host,peer_id)==JFX_SUCCESS);
    event_publish(JFX_EVENT_UI_INPUT,NULL); assert(events==2);
    const jfx_node_kind_t *kind=jfx_node_kind_find("org.test.scale");
    assert(kind && kind->params[0].default_value==.5f && !strcmp(kind->params[0].name,"scale"));
    assert(!strcmp(kind->params[0].label,"Scale \"quoted\"\n"));
    char catalog[65536]; assert(jfx_color_catalog(catalog,sizeof(catalog))==JFX_SUCCESS);
    assert(strstr(catalog,"Scale \\\"Test\\\"\\u000a") && strstr(catalog,"Scale \\\"quoted\\\"\\u000a"));
    jfx_node_value_t value; jfx_node_value_init(&value,kind);
    float input[]={.2f,.4f,.6f,.5f},output[4];
    assert(jfx_color_apply(kind,&value,NULL,input,1,1,output)==JFX_SUCCESS && output[0]==.1f && output[3]==.5f);
    assert(jfx_color_apply(kind,&value,NULL,input,1,1,input)==JFX_SUCCESS && input[1]==.2f);
    jfx_node_value_release(&value);
    jfx_editor_t *editor=jfx_editor_create(4,2); assert(editor);
    jfx_plugin_action_context_t context={sizeof(context),editor,0,0,0};
    assert(jfx_plugin_host_invoke(host,"org.test.graph",&context)==JFX_SUCCESS);
    assert(jfx_editor_kind(editor)==JFX_PROJECT_KIND_GRAPH && jfx_graph_node_value(jfx_editor_graph(editor),1)->scalars[0]==.4f);
    assert(jfx_editor_command(editor,"undo",0,0,0,0,"")==JFX_SUCCESS && jfx_editor_kind(editor)==JFX_PROJECT_KIND_SEQUENCE);
    assert(!jfx_editor_can_undo(editor)); jfx_editor_clear_history(editor);
    assert(jfx_plugin_host_invoke(host,"missing",&context)==JFX_ERROR_NOT_FOUND);
    assert(jfx_plugin_host_invoke(host,"org.test.apply",NULL)==JFX_ERROR_INVALID_ARGUMENT);
    fail_action=1;
    assert(jfx_plugin_host_invoke(host,"org.test.apply",&context)==JFX_ERROR_PLUGIN_FAILURE);
    assert(!jfx_timeline_effect_count(jfx_editor_timeline(editor),0,0) && !jfx_editor_can_undo(editor));
    fail_action=0;
    assert(jfx_plugin_host_invoke(host,"org.test.apply",&context)==JFX_SUCCESS);
    unsigned char pixels[32],baseline[32];
    assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))==JFX_SUCCESS);
    assert(pixels[0]>=76 && pixels[0]<=77 && pixels[1]==38 && pixels[2]==19 && pixels[3]==255);
    assert(jfx_plugin_host_unload(host,id)==JFX_ERROR_BUSY);
    assert(jfx_editor_command(editor,"undo",0,0,0,0,"")==JFX_SUCCESS);
    /* Redo history pins even when the live document no longer uses the effect. */
    assert(jfx_plugin_host_unload(host,id)==JFX_ERROR_BUSY);
    assert(jfx_editor_command(editor,"redo",0,0,0,0,"")==JFX_SUCCESS);
    fail_process=1; memset(pixels,0xAB,sizeof(pixels)); memcpy(baseline,pixels,sizeof(pixels));
    assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))!=JFX_SUCCESS && !memcmp(pixels,baseline,sizeof(pixels)));
    fail_process=2; assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))!=JFX_SUCCESS && !memcmp(pixels,baseline,sizeof(pixels)));
    fail_process=3; assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))!=JFX_SUCCESS && !memcmp(pixels,baseline,sizeof(pixels)));
    fail_process=4; assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))!=JFX_SUCCESS && !memcmp(pixels,baseline,sizeof(pixels)));
    fail_process=0;
    jfx_timeline_t *t=jfx_editor_timeline(editor);
    assert(jfx_timeline_duplicate_clip(t,0,0,0,20)==JFX_SUCCESS);
    assert(jfx_timeline_split_clip(t,0,0,10)==JFX_SUCCESS);
    char doc[32768]; size_t n=0; assert(jfx_editor_save(editor,doc,sizeof(doc),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(editor,doc,n,NULL,0)==JFX_SUCCESS);
    jfx_graph_t *graph=jfx_graph_create(); uint32_t source,fx,copy;
    assert(jfx_graph_add_node(graph,"solid","Source",&source)==JFX_SUCCESS);
    assert(jfx_graph_add_node(graph,"org.test.scale","Custom",&fx)==JFX_SUCCESS);
    assert(jfx_graph_connect(graph,source,0,fx,0)==JFX_SUCCESS);
    assert(jfx_graph_duplicate_node(graph,fx,&copy)==JFX_SUCCESS);
    assert(jfx_graph_render(graph,fx,4,2,0,pixels)==JFX_SUCCESS);
    jfx_export_job_t *job=NULL;
    if (jfx_export_available() && jfx_export_codec_available("ffv1",false)) {
        jfx_export_options_t options={.size=sizeof(options),.path=argv[2],.frame_count=1};
        assert(jfx_export_begin(editor,&options,&job)==JFX_SUCCESS);
    }
    jfx_editor_destroy(editor);
    assert(jfx_plugin_host_unload(host,id)==JFX_ERROR_BUSY);
    jfx_graph_destroy(graph);
    if (job) {
        assert(jfx_plugin_host_unload(host,id)==JFX_ERROR_BUSY);
        assert(jfx_export_step(job,1)==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_COMPLETE);
        assert(jfx_plugin_host_unload(host,id)==JFX_ERROR_BUSY);
        jfx_export_destroy(job); remove(argv[2]);
    }
    assert(jfx_plugin_host_unload(host,id)==JFX_SUCCESS);
    assert(!jfx_node_kind_find("org.test.scale") && !jfx_plugin_host_action_count(host));
    event_publish(JFX_EVENT_UI_INPUT,NULL); assert(events==2);
    /* A real shared module uses no host-exported linker symbols. */
    assert(jfx_plugin_host_load(host,argv[1],&id)==JFX_SUCCESS);
    editor=jfx_editor_create(4,2); context.editor=editor;
    assert(jfx_plugin_host_invoke(host,"org.joltfx.example.apply",&context)==JFX_SUCCESS);
    assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))==JFX_SUCCESS);
    assert(pixels[0]>153 && pixels[2]<38);
    assert(jfx_editor_command(editor,"effect.add",0,0,0,0,"org.joltfx.example.invert")==JFX_SUCCESS);
    assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))==JFX_SUCCESS);
    jfx_plugin_host_destroy(host); /* Module remains usable until editor/history release. */
    assert(jfx_editor_render_frame(editor,0,4,2,pixels,sizeof(pixels))==JFX_SUCCESS);
    jfx_editor_destroy(editor);
    assert(!jfx_node_kind_find("org.joltfx.example.tint"));
    assert(jfx_plugin_host_error(NULL)==NULL);
    jfx_engine_shutdown(engine);
    return 0;
}
