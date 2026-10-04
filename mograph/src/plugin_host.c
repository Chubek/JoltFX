/* Native/static plugin lifecycle and the header-only host-service SDK. */
#include "jfx/jfx_plugin_sdk.h"
#include "plugin_internal.h"
#include "joltscript/image_program.h"
#include "joltscript/image_task.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__) || defined(__wasi__)
typedef void *jfx_module_t;
static void module_close(jfx_module_t module) { (void)module; }
#elif defined(_WIN32)
#include <windows.h>
typedef HMODULE jfx_module_t;
static jfx_module_t module_open(const char *path) { return LoadLibraryA(path); }
static FARPROC module_symbol(jfx_module_t module,const char *name) { return GetProcAddress(module,name); }
static void module_close(jfx_module_t module) { if (module) FreeLibrary(module); }
#else
#include <dlfcn.h>
typedef void *jfx_module_t;
static jfx_module_t module_open(const char *path) { return dlopen(path,RTLD_NOW|RTLD_LOCAL); }
static void *module_symbol(jfx_module_t module,const char *name) { return dlsym(module,name); }
static void module_close(jfx_module_t module) { if (module) dlclose(module); }
#endif

#define MAX_PLUGINS 32u
#define MAX_ACTIONS 32u
#define MAX_EVENTS 32u
#define TEXT_CAP (JFX_PLUGIN_MAX_NAME_LENGTH+1u)
typedef struct instance instance_t;
typedef struct {
    instance_t *owner;
    jfx_node_kind_t kind;
    char name[TEXT_CAP],label[TEXT_CAP],category[TEXT_CAP];
    jfx_param_desc_t params[JFX_NODE_MAX_PARAMS];
    char param_names[JFX_NODE_MAX_PARAMS][TEXT_CAP],param_labels[JFX_NODE_MAX_PARAMS][TEXT_CAP];
    jfx_plugin_process_fn process;
    void *userdata;
    jolt_image_program_t *program;
} effect_t;
typedef struct {
    char name[TEXT_CAP],label[TEXT_CAP];
    jfx_project_kind_t document;
    jfx_plugin_action_fn invoke;
    void *userdata;
} action_t;
typedef struct {
    instance_t *owner;
    jfx_event_type_t type;
    jfx_event_handler_t handler;
    void *userdata;
    bool subscribed;
} subscription_t;
struct instance {
    jfx_plugin_host_t *host;
    uint32_t id;
    jfx_module_t module;
    jfx_plugin_info_t info;
    jfx_plugin_api_t api;
    jfx_plugin_finalize_fn finalize;
    jfx_plugin_shutdown_fn legacy_shutdown;
    void *userdata;
    action_t actions[MAX_ACTIONS];
    subscription_t events[MAX_EVENTS];
    uint32_t action_count,event_count;
    size_t references;
    bool registered,initializing;
};
struct jfx_plugin_host {
    jfx_engine_t *engine;
    instance_t *plugins[MAX_PLUGINS],*loading;
    uint32_t count,next_id;
    bool busy,closing;
    char error[512];
};
static effect_t *effects[JFX_PLUGIN_MAX_EFFECTS];
static size_t effect_count;
static _Thread_local unsigned callback_depth;
static const jfx_port_desc_t image_in[]={ {"in","Image",JFX_PORT_IMAGE,true,{0,0,0,0}} };
static const jfx_port_desc_t image_out[]={ {"out","Image",JFX_PORT_IMAGE,false,{0,0,0,0}} };

static void *allocate_bytes(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),bytes,_Alignof(max_align_t));
}
static void release_bytes(void *pointer) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),pointer); }
static bool fits(const char *text,size_t cap) { return text && *text && strlen(text)<cap; }
static bool identifier(const char *text,size_t cap) {
    if (!fits(text,cap) || !isalnum((unsigned char)*text)) return false;
    for (const char *p=text;*p;++p)
        if (!isalnum((unsigned char)*p) && *p!='.' && *p!='_' && *p!='-') return false;
    return true;
}
static jfx_result_t report(jfx_plugin_host_t *host,jfx_result_t result,const char *message) {
    if (host) snprintf(host->error,sizeof(host->error),"%s",message?message:jfx_result_to_string(result));
    return result;
}
static instance_t *find_instance(const jfx_plugin_host_t *host,uint32_t id) {
    if (!host || !id) return NULL;
    for (uint32_t i=0;i<host->count;++i) if (host->plugins[i]->id==id) return host->plugins[i];
    return NULL;
}
static effect_t *find_effect(const jfx_node_kind_t *kind) {
    for (size_t i=0;i<effect_count;++i) if (&effects[i]->kind==kind) return effects[i];
    return NULL;
}
size_t jfx_plugin_kind_count(void) { return effect_count; }
const jfx_node_kind_t *jfx_plugin_kind_at(size_t index) { return index<effect_count?&effects[index]->kind:NULL; }
bool jfx_plugin_kind_is_custom(const jfx_node_kind_t *kind) { return find_effect(kind)!=NULL; }
bool jfx_plugin_kind_retain(const jfx_node_kind_t *kind) {
    effect_t *e=find_effect(kind); if (!e) return false;
    ++e->owner->references; return true;
}
static void dispose_instance(instance_t *plugin);
void jfx_plugin_kind_release(const jfx_node_kind_t *kind) {
    effect_t *e=find_effect(kind); if (!e || !e->owner->references) return;
    instance_t *plugin=e->owner; jfx_plugin_host_t *host=plugin->host;
    if (--plugin->references==0 && host->closing) {
        dispose_instance(plugin);
        if (!host->count) release_bytes(host);
    }
}
static void *service_allocate(void *context,size_t bytes,size_t alignment) {
    (void)context;
    if (!bytes || !alignment || (alignment&(alignment-1)) || alignment>4096) return NULL;
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),bytes,alignment);
}
static void service_free(void *context,void *pointer) { (void)context; release_bytes(pointer); }
static void service_log(void *context,const char *message) {
    instance_t *p=context;
    if (p && message) tilly_log_simple(TILLY_LOG_INFO,"[plugin %s] %s",p->info.identifier,message);
}
static jfx_result_t can_register(instance_t *plugin,uint32_t capability) {
    if (!plugin || !plugin->initializing) return JFX_ERROR_INVALID_ARGUMENT;
    if (!(plugin->info.capabilities&capability))
        return report(plugin->host,JFX_ERROR_PLUGIN_FAILURE,"Plugin registration requires a declared capability.");
    return JFX_SUCCESS;
}
static jfx_result_t add_effect(instance_t *plugin,const char *name,const char *label,const char *category,
    const jfx_param_desc_t *params,size_t count,jfx_plugin_process_fn process,void *userdata,jolt_image_program_t *program) {
    if (!identifier(name,JFX_PLUGIN_MAX_ID_LENGTH+1u) || !fits(label,TEXT_CAP) || !fits(category,TEXT_CAP) ||
        count>JFX_NODE_MAX_PARAMS || (count && !params)) return JFX_ERROR_INVALID_ARGUMENT;
    if (jfx_node_kind_find(name)) return JFX_ERROR_ALREADY_EXISTS;
    if (effect_count==JFX_PLUGIN_MAX_EFFECTS) return JFX_ERROR_OUT_OF_MEMORY;
    for (size_t i=0;i<count;++i) {
        const jfx_param_desc_t *p=params+i;
        if (!identifier(p->name,JFX_PLUGIN_MAX_ID_LENGTH+1u) || !fits(p->label,TEXT_CAP) || !isfinite(p->minimum) ||
            !isfinite(p->maximum) || !isfinite(p->default_value) || !isfinite(p->step) || p->step<0 ||
            p->minimum>p->maximum || p->default_value<p->minimum || p->default_value>p->maximum ||
            (p->integral && (floorf(p->minimum)!=p->minimum || floorf(p->maximum)!=p->maximum || floorf(p->default_value)!=p->default_value)))
            return JFX_ERROR_INVALID_ARGUMENT;
        for (size_t j=0;j<i;++j) if (!strcmp(params[j].name,p->name)) return JFX_ERROR_ALREADY_EXISTS;
    }
    effect_t *e=allocate_bytes(sizeof(*e)); if (!e) return JFX_ERROR_OUT_OF_MEMORY;
    memset(e,0,sizeof(*e)); e->owner=plugin; e->process=process; e->userdata=userdata; e->program=program;
    snprintf(e->name,sizeof(e->name),"%s",name); snprintf(e->label,sizeof(e->label),"%s",label);
    snprintf(e->category,sizeof(e->category),"%s",category);
    e->kind=(jfx_node_kind_t){e->name,e->label,e->category,1,image_in,1,image_out,count,e->params,0,NULL};
    for (size_t i=0;i<count;++i) {
        e->params[i]=params[i];
        snprintf(e->param_names[i],TEXT_CAP,"%s",params[i].name); snprintf(e->param_labels[i],TEXT_CAP,"%s",params[i].label);
        e->params[i].name=e->param_names[i]; e->params[i].label=e->param_labels[i];
    }
    effects[effect_count++]=e; return JFX_SUCCESS;
}
static jfx_result_t service_effect(void *context,const jfx_plugin_effect_desc_t *desc) {
    if (!desc || desc->size<sizeof(*desc) || !desc->process) return JFX_ERROR_INVALID_ARGUMENT;
    instance_t *plugin=context; jfx_result_t r=can_register(plugin,JFX_PLUGIN_CAP_KERNELS);
    if (r!=JFX_SUCCESS) return r;
    return add_effect(plugin,desc->name,desc->label,desc->category,desc->parameters,desc->parameter_count,desc->process,desc->userdata,NULL);
}
static jfx_result_t service_kernel(void *context,const jfx_plugin_kernel_desc_t *desc) {
    if (!desc || desc->size<sizeof(*desc) || !desc->source) return JFX_ERROR_INVALID_ARGUMENT;
    instance_t *plugin=context; jfx_result_t r=can_register(plugin,JFX_PLUGIN_CAP_KERNELS);
    if (r!=JFX_SUCCESS) return r;
    jolt_image_program_t *program=NULL; jolt_diagnostic_t diagnostic={0};
    diagnostic.size=sizeof(diagnostic);
    jolt_status_t status=jolt_image_compile(desc->library?desc->library:"",desc->source,&program,&diagnostic);
    if (status!=JOLT_OK) {
        char message[256];
        snprintf(message,sizeof(message),"Kernel compile at %zu:%zu: %s",diagnostic.line,diagnostic.column,diagnostic.message);
        return report(plugin->host,status==JOLT_ERR_MEMORY?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_INVALID_ARGUMENT,message);
    }
    size_t count=jolt_image_parameter_count(program);
    jfx_param_desc_t params[JFX_NODE_MAX_PARAMS];
    if (count>JFX_NODE_MAX_PARAMS) { jolt_image_program_destroy(program); return JFX_ERROR_INVALID_ARGUMENT; }
    for (size_t i=0;i<count;++i) {
        const jolt_image_parameter_info_t *p=jolt_image_parameter_info(program,i);
        params[i]=(jfx_param_desc_t){p->name,p->name,(float)p->minimum,(float)p->maximum,(float)p->default_value,p->integer?1.0f:.01f,p->integer!=0};
    }
    r=add_effect(plugin,desc->name,desc->label,desc->category,params,count,NULL,NULL,program);
    if (r!=JFX_SUCCESS) jolt_image_program_destroy(program);
    return r;
}
static jfx_result_t service_action(void *context,const jfx_plugin_action_desc_t *desc) {
    if (!desc || desc->size<sizeof(*desc) || !desc->invoke || !identifier(desc->name,TEXT_CAP) ||
        !fits(desc->label,TEXT_CAP) || (desc->document!=JFX_PROJECT_KIND_GRAPH && desc->document!=JFX_PROJECT_KIND_SEQUENCE)) return JFX_ERROR_INVALID_ARGUMENT;
    instance_t *plugin=context; jfx_result_t r=can_register(plugin,JFX_PLUGIN_CAP_EDITOR);
    if (r!=JFX_SUCCESS) return r;
    for (uint32_t i=0;i<plugin->host->count;++i) {
        instance_t *p=plugin->host->plugins[i];
        for (uint32_t a=0;a<p->action_count;++a) if (!strcmp(p->actions[a].name,desc->name)) return JFX_ERROR_ALREADY_EXISTS;
    }
    if (plugin->action_count==MAX_ACTIONS) return JFX_ERROR_OUT_OF_MEMORY;
    action_t *a=&plugin->actions[plugin->action_count++];
    snprintf(a->name,TEXT_CAP,"%s",desc->name); snprintf(a->label,TEXT_CAP,"%s",desc->label);
    a->document=desc->document; a->invoke=desc->invoke; a->userdata=desc->userdata; return JFX_SUCCESS;
}
static void event_relay(jfx_event_type_t type,void *data,void *userdata) {
    subscription_t *subscription=userdata;
    if (!subscription->subscribed) return;
    ++callback_depth; subscription->handler(type,data,subscription->userdata); --callback_depth;
}
static jfx_result_t service_subscribe(void *context,jfx_event_type_t type,jfx_event_handler_t handler,void *userdata) {
    if (type<0 || type>=JFX_EVENT_COUNT || !handler) return JFX_ERROR_INVALID_ARGUMENT;
    instance_t *plugin=context; jfx_result_t r=can_register(plugin,JFX_PLUGIN_CAP_EVENTS);
    if (r!=JFX_SUCCESS) return r;
    if (plugin->event_count==MAX_EVENTS) return JFX_ERROR_OUT_OF_MEMORY;
    subscription_t *s=&plugin->events[plugin->event_count];
    *s=(subscription_t){plugin,type,handler,userdata,false};
    if (!event_subscribe(type,event_relay,s)) return JFX_ERROR_OUT_OF_MEMORY;
    s->subscribed=true; ++plugin->event_count; return JFX_SUCCESS;
}

static void unsubscribe_all(instance_t *plugin) {
    for (uint32_t i=0;i<plugin->event_count;++i) {
        subscription_t *s=&plugin->events[i];
        if (s->subscribed) { s->subscribed=false; event_unsubscribe_user(s->type,event_relay,s); }
    }
}
static void dispose_instance(instance_t *plugin) {
    jfx_plugin_host_t *host=plugin->host;
    unsubscribe_all(plugin);
    /* Withdraw zero-reference kinds before notifying consumers/finalizers. */
    for (size_t i=effect_count;i>0;--i) if (effects[i-1]->owner==plugin) {
        effect_t *e=effects[i-1]; jolt_image_program_destroy(e->program); release_bytes(e);
        memmove(effects+i-1,effects+i,(effect_count-i)*sizeof(*effects)); --effect_count;
    }
    ++callback_depth;
    if (plugin->registered && !plugin->initializing) event_publish(JFX_EVENT_PLUGIN_UNLOAD,&plugin->info);
    if (plugin->finalize) plugin->finalize(plugin->userdata);
    if (plugin->legacy_shutdown) plugin->legacy_shutdown(host);
    --callback_depth;
    for (uint32_t i=0;i<host->count;++i) if (host->plugins[i]==plugin) {
        memmove(host->plugins+i,host->plugins+i+1,(host->count-i-1)*sizeof(*host->plugins)); --host->count; break;
    }
    module_close(plugin->module); release_bytes(plugin);
}
jfx_result_t jfx_plugin_host_create(jfx_engine_t *engine,jfx_plugin_host_t **out_host) {
    if (!engine || !out_host) return JFX_ERROR_INVALID_ARGUMENT;
    *out_host=NULL; jfx_plugin_host_t *host=allocate_bytes(sizeof(*host));
    if (!host) return JFX_ERROR_OUT_OF_MEMORY;
    memset(host,0,sizeof(*host)); host->engine=engine; host->next_id=1; *out_host=host; return JFX_SUCCESS;
}
void jfx_plugin_host_destroy(jfx_plugin_host_t *host) {
    if (!host || host->closing || host->busy || callback_depth) return;
    host->closing=true; host->busy=true;
    for (uint32_t i=host->count;i>0;--i) {
        instance_t *plugin=host->plugins[i-1]; unsubscribe_all(plugin);
        if (!plugin->references) dispose_instance(plugin);
    }
    host->busy=false; if (!host->count) release_bytes(host);
}
jfx_result_t jfx_plugin_host_register(jfx_plugin_host_t *host,const jfx_plugin_desc_t *desc) {
    if (!host || !desc || desc->size<sizeof(*desc) || !host->loading ||
        !identifier(desc->identifier,JFX_PLUGIN_MAX_ID_LENGTH+1u) ||
        !fits(desc->display_name,TEXT_CAP) || !fits(desc->vendor,TEXT_CAP)) return JFX_ERROR_INVALID_ARGUMENT;
    if (desc->minimum_api_version>JFX_PLUGIN_API_VERSION) return JFX_ERROR_VERSION_MISMATCH;
    instance_t *plugin=host->loading;
    if (plugin->registered) return JFX_ERROR_ALREADY_EXISTS;
    for (uint32_t i=0;i<host->count;++i)
        if (host->plugins[i]!=plugin && !strcmp(host->plugins[i]->info.identifier,desc->identifier)) return JFX_ERROR_ALREADY_EXISTS;
    plugin->info.size=sizeof(plugin->info);
    snprintf(plugin->info.identifier,sizeof(plugin->info.identifier),"%s",desc->identifier);
    snprintf(plugin->info.display_name,sizeof(plugin->info.display_name),"%s",desc->display_name);
    snprintf(plugin->info.vendor,sizeof(plugin->info.vendor),"%s",desc->vendor);
    plugin->info.version=desc->version; plugin->info.minimum_api_version=desc->minimum_api_version;
    plugin->info.capabilities=desc->capabilities; plugin->registered=true; return JFX_SUCCESS;
}
static instance_t *new_instance(jfx_plugin_host_t *host,jfx_module_t module) {
    instance_t *plugin=allocate_bytes(sizeof(*plugin)); if (!plugin) return NULL;
    memset(plugin,0,sizeof(*plugin)); plugin->host=host; plugin->module=module;
    plugin->id=host->next_id++; if (!plugin->id) plugin->id=host->next_id++;
    plugin->initializing=true;
    plugin->api=(jfx_plugin_api_t){sizeof(plugin->api),JFX_PLUGIN_SDK_MAJOR,JFX_PLUGIN_SDK_MINOR,plugin,
        service_allocate,service_free,service_log,service_effect,service_kernel,service_action,service_subscribe,jfx_editor_command,
        jfx_editor_kind,jfx_editor_sequence_state,jfx_editor_graph_state};
    host->plugins[host->count++]=plugin; host->loading=plugin; return plugin;
}
static jfx_result_t finish_load(jfx_plugin_host_t *host,instance_t *plugin,jfx_result_t result,uint32_t *out_id) {
    host->loading=NULL;
    if (result!=JFX_SUCCESS || !plugin->registered) {
        if (result==JFX_SUCCESS) result=JFX_ERROR_PLUGIN_FAILURE;
        if (!host->error[0]) report(host,result,NULL);
        dispose_instance(plugin); host->busy=false; return result;
    }
    plugin->initializing=false; *out_id=plugin->id;
    event_publish(JFX_EVENT_PLUGIN_LOAD,&plugin->info);
    host->error[0]=0; host->busy=false; return JFX_SUCCESS;
}
static jfx_result_t attach(jfx_plugin_host_t *host,const jfx_plugin_definition_t *d,jfx_module_t module,uint32_t *out_id) {
    if (d->size<sizeof(*d) || !d->initialize || d->info.size<sizeof(d->info)) {
        module_close(module); return report(host,JFX_ERROR_INVALID_ARGUMENT,"Invalid plugin definition or initializer.");
    }
    if (d->sdk_major!=JFX_PLUGIN_SDK_MAJOR || d->minimum_sdk_minor>JFX_PLUGIN_SDK_MINOR) {
        module_close(module); return report(host,JFX_ERROR_VERSION_MISMATCH,"Incompatible plugin SDK version.");
    }
    const uint32_t supported=JFX_PLUGIN_CAP_KERNELS|JFX_PLUGIN_CAP_EVENTS|JFX_PLUGIN_CAP_EDITOR;
    if (d->info.capabilities&~supported) {
        module_close(module); return report(host,JFX_ERROR_NOT_IMPLEMENTED,"Plugin requests an unsupported registration capability.");
    }
    instance_t *plugin=new_instance(host,module);
    if (!plugin) { module_close(module); return JFX_ERROR_OUT_OF_MEMORY; }
    host->busy=true; jfx_result_t r=jfx_plugin_host_register(host,&d->info);
    if (r==JFX_SUCCESS) {
        plugin->finalize=d->shutdown;
        ++callback_depth; r=d->initialize(&plugin->api,&plugin->userdata); --callback_depth;
    }
    return finish_load(host,plugin,r,out_id);
}
jfx_result_t jfx_plugin_host_attach(jfx_plugin_host_t *host,const jfx_plugin_definition_t *d,uint32_t *out_id) {
    if (out_id) *out_id=0;
    if (!host || !d || !out_id) return report(host,JFX_ERROR_INVALID_ARGUMENT,"Definition and output ID are required.");
    if (host->busy || host->closing || callback_depth) return report(host,JFX_ERROR_BUSY,"Plugin lifecycle is busy.");
    if (host->count==MAX_PLUGINS) return report(host,JFX_ERROR_OUT_OF_MEMORY,"Plugin capacity reached.");
    host->error[0]=0; return attach(host,d,NULL,out_id);
}
jfx_result_t jfx_plugin_host_load(jfx_plugin_host_t *host,const char *path,uint32_t *out_id) {
    if (out_id) *out_id=0;
    if (!host || !path || !*path || !out_id) return report(host,JFX_ERROR_INVALID_ARGUMENT,"A module path and output ID are required.");
    if (host->busy || host->closing || callback_depth) return report(host,JFX_ERROR_BUSY,"Plugin lifecycle is busy.");
    if (host->count==MAX_PLUGINS) return report(host,JFX_ERROR_OUT_OF_MEMORY,"Plugin capacity reached.");
    host->error[0]=0;
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
    return report(host,JFX_ERROR_NOT_IMPLEMENTED,"Native modules are unavailable; use jfx_plugin_host_attach.");
#else
    jfx_module_t module=module_open(path);
    if (!module) {
#if defined(_WIN32)
        snprintf(host->error,sizeof(host->error),"Cannot load module (Windows error %lu).",(unsigned long)GetLastError());
#else
        snprintf(host->error,sizeof(host->error),"%s",dlerror());
#endif
        return JFX_ERROR_NOT_FOUND;
    }
    jfx_plugin_entry_fn entry=NULL;
    /* POSIX and Win32 loader pointer representations are copied, avoiding
     * ISO C object/function pointer casts in the public implementation. */
#if defined(_WIN32)
    FARPROC symbol=module_symbol(module,"jfx_plugin_entry");
#else
    void *symbol=module_symbol(module,"jfx_plugin_entry");
#endif
    if (sizeof(entry)==sizeof(symbol)) memcpy(&entry,&symbol,sizeof(entry));
    if (entry) {
        jfx_plugin_definition_t d={0}; d.size=sizeof(d);
        host->busy=true; ++callback_depth;
        jfx_result_t r=entry(JFX_PLUGIN_SDK_MAJOR,JFX_PLUGIN_SDK_MINOR,&d);
        --callback_depth; host->busy=false;
        if (r!=JFX_SUCCESS) { module_close(module); return report(host,r,NULL); }
        return attach(host,&d,module,out_id);
    }
    jfx_plugin_init_fn legacy=NULL; symbol=module_symbol(module,"jfx_plugin_init");
    if (sizeof(legacy)==sizeof(symbol)) memcpy(&legacy,&symbol,sizeof(legacy));
    if (!legacy) { module_close(module); return report(host,JFX_ERROR_NOT_FOUND,"Module exports neither jfx_plugin_entry nor jfx_plugin_init."); }
    instance_t *plugin=new_instance(host,module);
    if (!plugin) { module_close(module); return JFX_ERROR_OUT_OF_MEMORY; }
    symbol=module_symbol(module,"jfx_plugin_shutdown");
    if (sizeof(plugin->legacy_shutdown)==sizeof(symbol)) memcpy(&plugin->legacy_shutdown,&symbol,sizeof(plugin->legacy_shutdown));
    host->busy=true; ++callback_depth;
    jfx_result_t r=legacy(host,JFX_PLUGIN_API_VERSION); --callback_depth;
    return finish_load(host,plugin,r,out_id);
#endif
}
jfx_result_t jfx_plugin_host_unload(jfx_plugin_host_t *host,uint32_t id) {
    if (!host) return JFX_ERROR_INVALID_ARGUMENT;
    if (host->busy || host->closing || callback_depth) return report(host,JFX_ERROR_BUSY,"Plugin lifecycle is busy.");
    instance_t *plugin=find_instance(host,id);
    if (!plugin) return report(host,JFX_ERROR_NOT_FOUND,"Plugin ID not found.");
    if (plugin->references) return report(host,JFX_ERROR_BUSY,"Plugin is used by a document, undo/redo history or export snapshot.");
    host->busy=true; dispose_instance(plugin); host->busy=false; host->error[0]=0; return JFX_SUCCESS;
}
uint32_t jfx_plugin_host_count(const jfx_plugin_host_t *host) { return host?host->count:0; }
jfx_result_t jfx_plugin_host_get_info(const jfx_plugin_host_t *host,uint32_t id,jfx_plugin_info_t *out) {
    if (!host || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    instance_t *plugin=find_instance(host,id); if (!plugin) return JFX_ERROR_NOT_FOUND;
    *out=plugin->info; return JFX_SUCCESS;
}
jfx_result_t jfx_plugin_host_info_at(const jfx_plugin_host_t *host,uint32_t index,uint32_t *out_id,jfx_plugin_info_t *out) {
    if (!host || !out_id || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    if (index>=host->count) return JFX_ERROR_NOT_FOUND;
    *out_id=host->plugins[index]->id; *out=host->plugins[index]->info; return JFX_SUCCESS;
}
uint32_t jfx_plugin_host_action_count(const jfx_plugin_host_t *host) {
    uint32_t count=0;
    if (host) for (uint32_t i=0;i<host->count;++i) count+=host->plugins[i]->action_count;
    return count;
}
jfx_result_t jfx_plugin_host_action_info(const jfx_plugin_host_t *host,uint32_t index,jfx_plugin_action_info_t *out) {
    if (!host || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    for (uint32_t i=0;i<host->count;++i) {
        instance_t *plugin=host->plugins[i];
        if (index>=plugin->action_count) { index-=plugin->action_count; continue; }
        action_t *a=&plugin->actions[index]; out->size=sizeof(*out); out->plugin_id=plugin->id; out->document=a->document;
        snprintf(out->name,sizeof(out->name),"%s",a->name); snprintf(out->label,sizeof(out->label),"%s",a->label); return JFX_SUCCESS;
    }
    return JFX_ERROR_NOT_FOUND;
}
jfx_result_t jfx_plugin_host_invoke(jfx_plugin_host_t *host,const char *name,const jfx_plugin_action_context_t *context) {
    if (!host || !name || !context || context->size<sizeof(*context) || !context->editor)
        return report(host,JFX_ERROR_INVALID_ARGUMENT,"Action name, editor and selection context are required.");
    if (host->busy || host->closing || callback_depth) return report(host,JFX_ERROR_BUSY,"Plugin lifecycle is busy.");
    for (uint32_t i=0;i<host->count;++i) {
        instance_t *plugin=host->plugins[i];
        for (uint32_t a=0;a<plugin->action_count;++a) if (!strcmp(name,plugin->actions[a].name)) {
            action_t *action=&plugin->actions[a];
            jfx_result_t r=jfx_editor_begin_edit(context->editor,action->document);
            if (r!=JFX_SUCCESS) return report(host,r,NULL);
            jfx_editor_set_kind(context->editor,action->document);
            host->busy=true; ++callback_depth; r=action->invoke(action->userdata,context); --callback_depth;
            jfx_result_t end=r==JFX_SUCCESS?jfx_editor_commit_edit(context->editor):jfx_editor_cancel_edit(context->editor);
            host->busy=false;
            if (end!=JFX_SUCCESS) r=end;
            return report(host,r,r==JFX_SUCCESS?"":NULL);
        }
    }
    return report(host,JFX_ERROR_NOT_FOUND,"Plugin action not found.");
}
const char *jfx_plugin_host_error(const jfx_plugin_host_t *host) { return host?host->error:NULL; }

typedef struct { effect_t *effect; const float *parameters; double seconds; jfx_result_t result; } image_call_t;
static jolt_status_t run_image(void *userdata,const float *src,size_t width,size_t height,size_t limit,float *out) {
    image_call_t *call=userdata; effect_t *e=call->effect;
    size_t count=width*height*4,bytes=count*sizeof(float);
    for (size_t i=0;i<count;++i) out[i]=NAN; /* Detect incompletely written native output. */
    for (size_t i=0;i<width*height;++i) if (src[i*4+3]<0 || src[i*4+3]>1) return JOLT_ERR_ARGUMENT;
    if (e->process) {
        jfx_plugin_image_t image={sizeof(image),(uint32_t)width,(uint32_t)height,call->seconds,limit,src,call->parameters,e->kind.param_count,out};
        ++callback_depth; call->result=e->process(e->userdata,&image); --callback_depth;
        if (call->result!=JFX_SUCCESS) return JOLT_ERR_ARGUMENT;
        for (size_t i=0;i<width*height;++i) if (out[i*4+3]<0 || out[i*4+3]>1) return JOLT_ERR_NUMERIC;
        return JOLT_OK;
    }
    if (bytes>limit || width*height>SIZE_MAX/10000) return JOLT_ERR_BUDGET;
    float *premul=allocate_bytes(bytes); if (!premul) return JOLT_ERR_MEMORY;
    for (size_t i=0;i<width*height;++i) {
        float alpha=src[i*4+3];
        if (alpha<0 || alpha>1) { release_bytes(premul); return JOLT_ERR_ARGUMENT; }
        for (size_t c=0;c<3;++c) premul[i*4+c]=src[i*4+c]*alpha;
        premul[i*4+3]=alpha;
    }
    jolt_image_parameter_t params[JFX_NODE_MAX_PARAMS];
    for (size_t p=0;p<e->kind.param_count;++p) params[p]=(jolt_image_parameter_t){e->params[p].name,call->parameters[p]};
    jolt_status_t status=jolt_image_program_run(e->program,premul,width,height,params,e->kind.param_count,NULL,0,limit-bytes,width*height*10000,out);
    release_bytes(premul);
    if (status==JOLT_OK) for (size_t i=0;i<width*height;++i) {
        float alpha=out[i*4+3];
        if (alpha<0 || alpha>1) return JOLT_ERR_NUMERIC;
        for (size_t c=0;c<3;++c) out[i*4+c]=alpha>0?out[i*4+c]/alpha:0;
    }
    return status;
}
jfx_result_t jfx_plugin_kind_process(const jfx_node_kind_t *kind,const float *params,const float *input,
    uint32_t width,uint32_t height,double seconds,size_t limit,float *out) {
    effect_t *effect=find_effect(kind);
    if (!effect || !params || !input || !out || !isfinite(seconds)) return JFX_ERROR_INVALID_ARGUMENT;
    for (size_t p=0;p<kind->param_count;++p) {
        const jfx_param_desc_t *d=kind->params+p; float v=params[p];
        if (!isfinite(v) || v<d->minimum || v>d->maximum || (d->integral && floorf(v)!=v)) return JFX_ERROR_INVALID_ARGUMENT;
    }
    image_call_t call={effect,params,seconds,JFX_SUCCESS};
    jolt_status_t status=jolt_image_task_run(run_image,&call,input,width,height,limit,out);
    if (call.result!=JFX_SUCCESS) return call.result;
    if (status==JOLT_OK) return JFX_SUCCESS;
    return status==JOLT_ERR_BUDGET || status==JOLT_ERR_MEMORY?JFX_ERROR_OUT_OF_MEMORY:JFX_ERROR_PLUGIN_FAILURE;
}
