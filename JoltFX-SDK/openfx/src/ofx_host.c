#include <joltfx/sdk/openfx.h>
#include <ofxImageEffect.h>
#include <ofxProperty.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdalign.h>
#if !defined(_WIN32)
#include <stdatomic.h>
#endif

#if defined(__EMSCRIPTEN__) || defined(__wasi__)
typedef void *module_t;
static module_t open_module(const char *path) { (void)path; return NULL; }
static void close_module(module_t m) { (void)m; }
static void *symbol(module_t m,const char *name) { (void)m; (void)name; return NULL; }
#elif defined(_WIN32)
#include <windows.h>
typedef HMODULE module_t;
static module_t open_module(const char *path) { return LoadLibraryA(path); }
static void close_module(module_t m) { if (m) FreeLibrary(m); }
static void *symbol(module_t m,const char *name) {
    FARPROC f=GetProcAddress(m,name); void *p=NULL;
    if (sizeof(p)==sizeof(f)) memcpy(&p,&f,sizeof(p));
    return p;
}
#else
#include <dlfcn.h>
typedef void *module_t;
static module_t open_module(const char *path) { return dlopen(path,RTLD_NOW|RTLD_LOCAL); }
static void close_module(module_t m) { if (m) dlclose(m); }
static void *symbol(module_t m,const char *name) { return dlsym(m,name); }
#endif

/* Bounded inspection storage. Every string returned to a plugin remains valid
 * until that property is changed or the descriptor is destroyed. */
#define VALUES 16
#define TEXT 512
#define PROPERTIES 128
enum { POINTER, STRING, DOUBLE, INTEGER };
typedef union { void *p; char s[TEXT]; double d; int i; } value_t;
typedef struct property {
    struct property *next;
    char name[128];
    int type,count,initial_count;
    value_t values[VALUES],initial[VALUES];
} property_t;
typedef struct {
    jfx_ofx_host_t *host;
    property_t *first;
    unsigned count;
} properties_t;
typedef struct {
    OfxPlugin *plugin;
    properties_t descriptor;
    bool loaded,described;
} plugin_t;
struct jfx_ofx_host {
    struct jfx_ofx_host *next;
    jfx_ofx_services_t services;
    module_t module;
    OfxHost ofx;
    properties_t properties;
    plugin_t plugins[JFX_OFX_MAX_PLUGINS];
    uint32_t count;
    bool busy,oom;
    char error[TEXT];
};
/* A binary's setHost normally writes process-global plugin state. Never let a
 * second host overwrite it while the first is alive (including path aliases). */
static jfx_ofx_host_t *hosts;
#if defined(_WIN32)
static volatile LONG registry_lock;
static bool lock_registry(void) { return InterlockedCompareExchange(&registry_lock,1,0)==0; }
static void unlock_registry(void) { InterlockedExchange(&registry_lock,0); }
#else
static atomic_flag registry_lock=ATOMIC_FLAG_INIT;
static bool lock_registry(void) { return !atomic_flag_test_and_set_explicit(&registry_lock,memory_order_acquire); }
static void unlock_registry(void) { atomic_flag_clear_explicit(&registry_lock,memory_order_release); }
#endif
#if defined(_MSC_VER)
static __declspec(thread) jfx_ofx_host_t *current;
#else
static _Thread_local jfx_ofx_host_t *current;
#endif
static void *alloc(jfx_ofx_host_t *h,size_t n,size_t alignment) {
    void *p=h->services.allocate(h->services.context,n,alignment);
    if (!p) h->oom=true;
    return p;
}
static void dispose(properties_t *p) {
    while (p->first) {
        property_t *next=p->first->next;
        p->host->services.release(p->host->services.context,p->first);
        p->first=next;
    }
    p->count=0;
}
static property_t *find(properties_t *p,const char *name) {
    for (property_t *v=p->first;v;v=v->next) if (!strcmp(v->name,name)) return v;
    return NULL;
}
static OfxStatus access_property(OfxPropertySetHandle handle,const char *name,
        int index,int type,bool create,property_t **out) {
    properties_t *p=(properties_t *)handle;
    if (!p) return kOfxStatErrBadHandle;
    if (!name || !*name || strlen(name)>=sizeof(((property_t *)0)->name)) return kOfxStatErrValue;
    if (index<0 || index>=VALUES) return kOfxStatErrBadIndex;
    property_t *v=find(p,name);
    if (!v && create) {
        if (p->count==PROPERTIES) return kOfxStatErrMemory;
        v=alloc(p->host,sizeof(*v),alignof(property_t));
        if (!v) return kOfxStatErrMemory;
        memset(v,0,sizeof(*v)); strcpy(v->name,name); v->type=type;
        v->next=p->first; p->first=v; ++p->count;
    }
    if (!v) return kOfxStatErrUnknown;
    if (v->type!=type) return kOfxStatErrValue;
    if (!create && index>=v->count) return kOfxStatErrBadIndex;
    *out=v; return kOfxStatOK;
}
#define SCALAR(Name,Type,Kind,Member) \
static OfxStatus set##Name(OfxPropertySetHandle p,const char *n,int i,Type x) { \
    property_t *v; OfxStatus s=access_property(p,n,i,Kind,true,&v); if (s) return s; \
    v->values[i].Member=x; if (v->count<=i) v->count=i+1; return kOfxStatOK; \
} \
static OfxStatus get##Name(OfxPropertySetHandle p,const char *n,int i,Type *x) { \
    if (!x) return kOfxStatErrValue; \
    property_t *v; OfxStatus s=access_property(p,n,i,Kind,false,&v); if (s) return s; \
    *x=v->values[i].Member; return kOfxStatOK; \
}
SCALAR(Pointer,void *,POINTER,p)
SCALAR(Double,double,DOUBLE,d)
SCALAR(Int,int,INTEGER,i)
static OfxStatus setString(OfxPropertySetHandle p,const char *n,int i,const char *x) {
    if (!x || strlen(x)>=TEXT) return kOfxStatErrValue;
    property_t *v; OfxStatus s=access_property(p,n,i,STRING,true,&v); if (s) return s;
    memmove(v->values[i].s,x,strlen(x)+1); if (v->count<=i) v->count=i+1; return kOfxStatOK;
}
static OfxStatus getString(OfxPropertySetHandle p,const char *n,int i,char **x) {
    if (!x) return kOfxStatErrValue;
    property_t *v; OfxStatus s=access_property(p,n,i,STRING,false,&v); if (s) return s;
    *x=v->values[i].s; return kOfxStatOK;
}
static OfxStatus set_many(OfxPropertySetHandle p,const char *n,int count,int type,const void *x) {
    if (count<0 || count>VALUES) return kOfxStatErrBadIndex;
    if (count && !x) return kOfxStatErrValue;
    if (type==STRING) for (int i=0;i<count;++i) {
        const char *s=((const char *const *)x)[i];
        if (!s || strlen(s)>=TEXT) return kOfxStatErrValue;
    }
    value_t values[VALUES]={0};
    for (int i=0;i<count;++i) switch (type) {
        case POINTER: values[i].p=((void *const *)x)[i]; break;
        case STRING: strcpy(values[i].s,((const char *const *)x)[i]); break;
        case DOUBLE: values[i].d=((const double *)x)[i]; break;
        case INTEGER: values[i].i=((const int *)x)[i]; break;
    }
    property_t *v; OfxStatus s=access_property(p,n,0,type,true,&v); if (s) return s;
    memcpy(v->values,values,sizeof(values)); v->count=count;
    return kOfxStatOK;
}
#define VECTOR(Name,Input,Output,Kind) \
static OfxStatus set##Name##N(OfxPropertySetHandle p,const char *n,int c,Input x) { return set_many(p,n,c,Kind,x); } \
static OfxStatus get##Name##N(OfxPropertySetHandle p,const char *n,int c,Output x) { \
    if (c<0 || c>VALUES) return kOfxStatErrBadIndex; \
    if (c && !x) return kOfxStatErrValue; \
    if (!c) { \
        if (!p) return kOfxStatErrBadHandle; \
        if (!n) return kOfxStatErrValue; \
        property_t *empty=find((properties_t *)p,n); \
        return !empty?kOfxStatErrUnknown:empty->type!=Kind?kOfxStatErrValue:kOfxStatOK; \
    } \
    property_t *v; OfxStatus s=access_property(p,n,c?c-1:0,Kind,false,&v); \
    if (s) return s; \
    for (int i=0;i<c;++i) { s=get##Name(p,n,i,&x[i]); if (s) return s; } \
    return kOfxStatOK; \
}
VECTOR(Pointer,void *const *,void **,POINTER)
VECTOR(String,const char *const *,char **,STRING)
VECTOR(Double,const double *,double *,DOUBLE)
VECTOR(Int,const int *,int *,INTEGER)
static OfxStatus reset(OfxPropertySetHandle handle,const char *name) {
    if (!handle) return kOfxStatErrBadHandle;
    if (!name) return kOfxStatErrValue;
    property_t *v=find((properties_t *)handle,name); if (!v) return kOfxStatErrUnknown;
    memcpy(v->values,v->initial,sizeof(v->values)); v->count=v->initial_count; return kOfxStatOK;
}
static OfxStatus dimension(OfxPropertySetHandle handle,const char *name,int *count) {
    if (!handle) return kOfxStatErrBadHandle;
    if (!name || !count) return kOfxStatErrValue;
    property_t *v=find((properties_t *)handle,name); if (!v) return kOfxStatErrUnknown;
    *count=v->count; return kOfxStatOK;
}
static OfxPropertySuiteV1 property_suite={
    setPointer,setString,setDouble,setInt,setPointerN,setStringN,setDoubleN,setIntN,
    getPointer,getString,getDouble,getInt,getPointerN,getStringN,getDoubleN,getIntN,reset,dimension
};
static OfxStatus get_properties(OfxImageEffectHandle handle,OfxPropertySetHandle *out) {
    if (!handle) return kOfxStatErrBadHandle;
    if (!out) return kOfxStatErrValue;
    *out=(OfxPropertySetHandle)handle; return kOfxStatOK;
}
/* Inspection only: unsupported image/parameter operations return a status,
 * rather than exposing NULL function pointers to plugins. */
static OfxStatus get_params(OfxImageEffectHandle h,OfxParamSetHandle *p) {
    (void)h; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxStatus clip_define(OfxImageEffectHandle h,const char *n,OfxPropertySetHandle *p) {
    (void)h; (void)n; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxStatus clip_handle(OfxImageEffectHandle h,const char *n,OfxImageClipHandle *c,OfxPropertySetHandle *p) {
    (void)h; (void)n; if (c) *c=NULL; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxStatus clip_props(OfxImageClipHandle h,OfxPropertySetHandle *p) {
    (void)h; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxStatus clip_image(OfxImageClipHandle h,OfxTime t,const OfxRectD *r,OfxPropertySetHandle *p) {
    (void)h; (void)t; (void)r; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxStatus release_image(OfxPropertySetHandle h) { (void)h; return kOfxStatErrBadHandle; }
static OfxStatus clip_rod(OfxImageClipHandle h,OfxTime t,OfxRectD *r) {
    (void)h; (void)t; (void)r; return kOfxStatErrUnsupported;
}
static int abort_effect(OfxImageEffectHandle h) { (void)h; return 0; }
static OfxStatus image_alloc(OfxImageEffectHandle h,size_t n,OfxImageMemoryHandle *p) {
    (void)h; (void)n; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxStatus image_free(OfxImageMemoryHandle h) { (void)h; return kOfxStatErrUnsupported; }
static OfxStatus image_lock(OfxImageMemoryHandle h,void **p) {
    (void)h; if (p) *p=NULL; return kOfxStatErrUnsupported;
}
static OfxImageEffectSuiteV1 image_suite={get_properties,get_params,clip_define,clip_handle,
    clip_props,clip_image,release_image,clip_rod,abort_effect,image_alloc,image_free,image_lock,image_free};
typedef union {
    max_align_t alignment;
    jfx_ofx_services_t services;
} allocation_t;
static OfxStatus memory_alloc(void *handle,size_t bytes,void **out) {
    (void)handle;
    if (!out) return kOfxStatErrValue;
    *out=NULL;
    if (!current) return kOfxStatErrBadHandle;
    if (bytes>SIZE_MAX-sizeof(allocation_t)) return kOfxStatErrMemory;
    allocation_t *a=alloc(current,sizeof(*a)+bytes,alignof(allocation_t));
    if (!a) return kOfxStatErrMemory;
    a->services=current->services; *out=a+1; return kOfxStatOK;
}
static OfxStatus memory_free(void *p) {
    if (p) { allocation_t *a=(allocation_t *)p-1; a->services.release(a->services.context,a); }
    return kOfxStatOK;
}
static OfxMemorySuiteV1 memory_suite={memory_alloc,memory_free};
static OfxStatus message(void *handle,const char *type,const char *id,const char *format,...) {
    (void)handle; (void)id;
    if (!current || !type || !format) return kOfxStatErrValue;
    va_list args; va_start(args,format); vsnprintf(current->error,sizeof(current->error),format,args); va_end(args);
    if (current->services.log) current->services.log(current->services.context,current->error);
    return !strcmp(type,kOfxMessageQuestion)?kOfxStatReplyNo:kOfxStatOK;
}
static OfxMessageSuiteV1 message_suite={message};
static const void *fetch(OfxPropertySetHandle host,const char *name,int version) {
    (void)host;
    if (!name || version!=1) return NULL;
    if (!strcmp(name,kOfxPropertySuite)) return &property_suite;
    if (!strcmp(name,kOfxImageEffectSuite)) return &image_suite;
    if (!strcmp(name,kOfxMemorySuite)) return &memory_suite;
    if (!strcmp(name,kOfxMessageSuite)) return &message_suite;
    return NULL;
}
static bool ok(OfxStatus s) { return s==kOfxStatOK || s==kOfxStatReplyDefault; }
static jfx_result_t result(jfx_ofx_host_t *h,const char *action,OfxStatus s) {
    if (h->oom || s==kOfxStatErrMemory) return JFX_ERROR_OUT_OF_MEMORY;
    if (ok(s)) return JFX_SUCCESS;
    snprintf(h->error,sizeof(h->error),"%s returned OpenFX status %d",action,s);
    if (s==kOfxStatErrUnsupported || s==kOfxStatErrMissingHostFeature) return JFX_ERROR_NOT_IMPLEMENTED;
    return JFX_ERROR_BACKEND_FAILURE;
}
static OfxStatus action(plugin_t *p,const char *name,const void *handle) {
    return p->plugin->mainEntry(name,handle,NULL,NULL);
}
static void defaults(properties_t *p) {
    for (property_t *v=p->first;v;v=v->next) {
        v->initial_count=v->count; memcpy(v->initial,v->values,sizeof(v->initial));
    }
}
static void seed_host(jfx_ofx_host_t *h) {
    OfxPropertySetHandle p=(OfxPropertySetHandle)&h->properties;
    setString(p,kOfxPropType,0,kOfxTypeImageEffectHost);
    setString(p,kOfxPropName,0,"org.joltfx.sdk.inspector");
    setString(p,kOfxPropLabel,0,"JoltFX SDK Inspector");
    int api[]={1,4}; setIntN(p,kOfxPropAPIVersion,2,api);
    setInt(p,kOfxImageEffectHostPropIsBackground,0,1);
    setInt(p,kOfxImageEffectPropSupportsOverlays,0,0);
    setInt(p,kOfxImageEffectPropSupportsMultiResolution,0,0);
    setInt(p,kOfxImageEffectPropSupportsTiles,0,0);
    setInt(p,kOfxImageEffectPropTemporalClipAccess,0,0);
    setInt(p,kOfxImageEffectPropSupportsMultipleClipDepths,0,0);
    setInt(p,kOfxImageEffectPropSupportsMultipleClipPARs,0,0);
    setString(p,kOfxImageEffectPropSupportedContexts,0,kOfxImageEffectContextFilter);
    setString(p,kOfxImageEffectPropSupportedPixelDepths,0,kOfxBitDepthByte);
    setString(p,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentRGBA);
    defaults(&h->properties);
}
static void cleanup(jfx_ofx_host_t *h) {
    for (uint32_t i=h->count;i>0;--i) {
        plugin_t *p=&h->plugins[i-1];
        if (p->loaded) action(p,kOfxActionUnload,NULL);
        dispose(&p->descriptor);
    }
    dispose(&h->properties); close_module(h->module);
    h->services.release(h->services.context,h);
}
static jfx_result_t open_host(const char *path,const jfx_ofx_services_t *services,jfx_ofx_host_t **out) {
    if (out) *out=NULL;
    if (!out || !path || !*path || !services || services->size<sizeof(*services) ||
        !services->allocate || !services->release) return JFX_ERROR_INVALID_ARGUMENT;
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
    return JFX_ERROR_NOT_IMPLEMENTED;
#endif
    if (current) return JFX_ERROR_BUSY;
    module_t module=open_module(path); if (!module) return JFX_ERROR_NOT_FOUND;
    for (jfx_ofx_host_t *other=hosts;other;other=other->next) if (other->module==module) {
        close_module(module); return JFX_ERROR_BUSY;
    }
    int (*count_plugins)(void)=NULL;
    OfxPlugin *(*get_plugin)(int)=NULL;
    OfxStatus (*set_host)(const OfxHost *)=NULL;
#define RESOLVE(fn,name) do { void *s=symbol(module,name); if (sizeof(fn)==sizeof(s)) memcpy(&fn,&s,sizeof(fn)); } while (0)
    RESOLVE(count_plugins,"OfxGetNumberOfPlugins"); RESOLVE(get_plugin,"OfxGetPlugin"); RESOLVE(set_host,"OfxSetHost");
#undef RESOLVE
    if (!count_plugins || !get_plugin) { close_module(module); return JFX_ERROR_NOT_FOUND; }
    jfx_ofx_host_t *h=services->allocate(services->context,sizeof(*h),alignof(jfx_ofx_host_t));
    if (!h) { close_module(module); return JFX_ERROR_OUT_OF_MEMORY; }
    memset(h,0,sizeof(*h)); h->services=*services; h->module=module;
    h->properties.host=h; h->ofx.host=(OfxPropertySetHandle)&h->properties; h->ofx.fetchSuite=fetch;
    h->busy=true; current=h; seed_host(h);
    jfx_result_t r=h->oom?JFX_ERROR_OUT_OF_MEMORY:JFX_SUCCESS;
    if (r==JFX_SUCCESS && set_host) r=result(h,"OfxSetHost",set_host(&h->ofx));
    if (r==JFX_SUCCESS) {
        int count=count_plugins();
        if (count<1 || count>(int)JFX_OFX_MAX_PLUGINS) r=JFX_ERROR_INVALID_ARGUMENT;
        else for (int i=0;i<count;++i) {
            OfxPlugin *p=get_plugin(i);
            if (!p || !p->pluginApi || strcmp(p->pluginApi,kOfxImageEffectPluginApi) || p->apiVersion!=1 ||
                !p->pluginIdentifier || !*p->pluginIdentifier || strlen(p->pluginIdentifier)>=96 || !p->setHost || !p->mainEntry) {
                r=JFX_ERROR_INVALID_ARGUMENT; break;
            }
            plugin_t *item=&h->plugins[h->count++]; item->plugin=p; item->descriptor.host=h;
            p->setHost(&h->ofx);
            if (h->oom) { r=JFX_ERROR_OUT_OF_MEMORY; break; }
        }
    }
    if (r!=JFX_SUCCESS) { cleanup(h); current=NULL; return r; }
    h->busy=false; current=NULL; *out=h; return JFX_SUCCESS;
}
jfx_result_t jfx_ofx_host_open(const char *path,const jfx_ofx_services_t *services,jfx_ofx_host_t **out) {
    if (out) *out=NULL;
    if (!lock_registry()) return JFX_ERROR_BUSY;
    jfx_result_t r=open_host(path,services,out);
    if (r==JFX_SUCCESS) { (*out)->next=hosts; hosts=*out; }
    unlock_registry(); return r;
}
jfx_result_t jfx_ofx_host_close(jfx_ofx_host_t *h) {
    if (!h) return JFX_SUCCESS;
    if (h->busy || current) return JFX_ERROR_BUSY;
    if (!lock_registry()) return JFX_ERROR_BUSY;
    jfx_ofx_host_t **link=&hosts;
    while (*link && *link!=h) link=&(*link)->next;
    if (*link) *link=h->next;
    h->busy=true; current=h; cleanup(h); current=NULL; unlock_registry(); return JFX_SUCCESS;
}
uint32_t jfx_ofx_host_plugin_count(const jfx_ofx_host_t *h) { return h?h->count:0; }
const char *jfx_ofx_host_error(const jfx_ofx_host_t *h) { return h?h->error:"Invalid OpenFX host"; }
static const char *text(properties_t *p,const char *name,const char *fallback) {
    char *s=NULL;
    return getString((OfxPropertySetHandle)p,name,0,&s)==kOfxStatOK?s:fallback;
}
static bool contains(properties_t *p,const char *name,const char *value) {
    property_t *v=find(p,name);
    if (v && v->type==STRING) for (int i=0;i<v->count;++i) if (!strcmp(v->values[i].s,value)) return true;
    return false;
}
jfx_result_t jfx_ofx_host_plugin_info(jfx_ofx_host_t *h,uint32_t index,jfx_ofx_plugin_info_t *out) {
    if (!h || !out || out->size<sizeof(*out) || index>=h->count) return JFX_ERROR_INVALID_ARGUMENT;
    if (h->busy || current) return JFX_ERROR_BUSY;
    h->busy=true; h->oom=false; h->error[0]=0; current=h;
    plugin_t *p=&h->plugins[index]; jfx_result_t r=JFX_SUCCESS;
    if (!p->described) {
        OfxStatus s=action(p,kOfxActionLoad,NULL);
        p->loaded=ok(s); r=result(h,kOfxActionLoad,s);
        if (r==JFX_SUCCESS) {
            OfxPropertySetHandle props=(OfxPropertySetHandle)&p->descriptor;
            setString(props,kOfxPropType,0,kOfxTypeImageEffect);
            setString(props,kOfxPropName,0,p->plugin->pluginIdentifier);
            setString(props,kOfxPropLabel,0,p->plugin->pluginIdentifier);
            setString(props,kOfxImageEffectPluginPropGrouping,0,"");
            defaults(&p->descriptor);
            r=h->oom?JFX_ERROR_OUT_OF_MEMORY:result(h,kOfxActionDescribe,action(p,kOfxActionDescribe,&p->descriptor));
        }
        if (r==JFX_SUCCESS) p->described=true;
        else {
            if (p->loaded) action(p,kOfxActionUnload,NULL);
            p->loaded=false; dispose(&p->descriptor);
        }
    }
    if (r==JFX_SUCCESS) {
        jfx_ofx_plugin_info_t info={0}; info.size=out->size;
        snprintf(info.identifier,sizeof(info.identifier),"%s",p->plugin->pluginIdentifier);
        snprintf(info.label,sizeof(info.label),"%s",text(&p->descriptor,kOfxPropLabel,info.identifier));
        snprintf(info.grouping,sizeof(info.grouping),"%s",text(&p->descriptor,kOfxImageEffectPluginPropGrouping,""));
        info.version_major=p->plugin->pluginVersionMajor; info.version_minor=p->plugin->pluginVersionMinor;
        info.supports_filter=contains(&p->descriptor,kOfxImageEffectPropSupportedContexts,kOfxImageEffectContextFilter) &&
            contains(&p->descriptor,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthByte);
        *out=info;
    }
    current=NULL; h->busy=false; return r;
}
