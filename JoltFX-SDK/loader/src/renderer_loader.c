#include <joltfx/sdk/loader.h>
#include <stdio.h>
#include <string.h>
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
typedef void *module_handle_t;
static void module_close(module_handle_t p) { (void)p; }
static module_handle_t module_open_path(const char *path) { (void)path; return NULL; }
static void *module_symbol_of(module_handle_t p,const char *name) { (void)p; (void)name; return NULL; }
#elif defined(_WIN32)
#include <windows.h>
typedef HMODULE module_handle_t;
static void module_close(module_handle_t p) { if (p) FreeLibrary(p); }
static module_handle_t module_open_path(const char *path) { return LoadLibraryA(path); }
static void *module_symbol_of(module_handle_t p,const char *name) {
    FARPROC symbol=p?GetProcAddress(p,name):NULL;
    void *pointer=NULL;
    if (sizeof(pointer)==sizeof(symbol)) memcpy(&pointer,&symbol,sizeof(pointer));
    return pointer;
}
#else
#include <dlfcn.h>
typedef void *module_handle_t;
static void module_close(module_handle_t p) { if (p) dlclose(p); }
static module_handle_t module_open_path(const char *path) { return dlopen(path,RTLD_NOW|RTLD_LOCAL); }
static void *module_symbol_of(module_handle_t p,const char *name) { return p?dlsym(p,name):NULL; }
#endif
struct jfx_renderer_module {
    jfx_renderer_definition_t definition;
    jfx_renderer_host_t host;
    module_handle_t handle;
    bool busy;
};
static bool valid_host(const jfx_renderer_host_t *h) {
    return h && h->size>=sizeof(*h) && h->abi_major==JFX_RENDERER_ABI_MAJOR &&
        h->allocate && h->release && h->available && h->render_scene;
}
static bool valid_text_n(const char *s,size_t capacity) {
    if (!s || !*s) return false;
    for (size_t i=0;i<capacity;++i) if (!s[i]) return true;
    return false;
}
static bool valid_text(const char *s) { return valid_text_n(s,96); }
jfx_result_t jfx_renderer_module_attach(const jfx_renderer_definition_t *d,
    const jfx_renderer_host_t *h,jfx_renderer_module_t **out) {
    if (out) *out=NULL;
    if (!out || !valid_host(h) || !d || d->size<sizeof(*d) || !d->render ||
        !valid_text(d->identifier) || !valid_text(d->name)) return JFX_ERROR_INVALID_ARGUMENT;
    if (d->abi_major!=JFX_RENDERER_ABI_MAJOR || d->minimum_abi_minor>h->abi_minor)
        return JFX_ERROR_VERSION_MISMATCH;
    jfx_renderer_module_t *m=h->allocate(h->context,sizeof(*m),_Alignof(jfx_renderer_module_t));
    if (!m) return JFX_ERROR_OUT_OF_MEMORY;
    *m=(jfx_renderer_module_t){*d,*h,NULL,false}; *out=m; return JFX_SUCCESS;
}
jfx_result_t jfx_renderer_module_open(const char *path,const jfx_renderer_host_t *h,jfx_renderer_module_t **out) {
    if (out) *out=NULL;
    if (!path || !*path || !out || !valid_host(h)) return JFX_ERROR_INVALID_ARGUMENT;
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
    return JFX_ERROR_NOT_IMPLEMENTED;
#else
    module_handle_t handle=module_open_path(path);
    if (!handle) return JFX_ERROR_NOT_FOUND;
    void *symbol=module_symbol_of(handle,"jfx_renderer_entry");
    jfx_renderer_entry_fn entry=NULL;
    if (sizeof(symbol)==sizeof(entry)) memcpy(&entry,&symbol,sizeof(entry));
    if (!entry) { module_close(handle); return JFX_ERROR_NOT_FOUND; }
    jfx_renderer_definition_t d={0}; d.size=sizeof(d);
    jfx_result_t r=entry(JFX_RENDERER_ABI_MAJOR,h->abi_minor,&d);
    if (r==JFX_SUCCESS) r=jfx_renderer_module_attach(&d,h,out);
    if (r!=JFX_SUCCESS) module_close(handle); else (*out)->handle=handle;
    return r;
#endif
}
jfx_result_t jfx_renderer_module_info(const jfx_renderer_module_t *m,jfx_renderer_definition_t *out) {
    if (!m || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    *out=m->definition; return JFX_SUCCESS;
}
jfx_result_t jfx_renderer_module_render(jfx_renderer_module_t *m,const jfx_renderer_frame_t *f,uint8_t *out,size_t capacity) {
    if (!m || !f || f->size<sizeof(*f) || !out) return JFX_ERROR_INVALID_ARGUMENT;
    if (m->busy) return JFX_ERROR_BUSY;
    m->busy=true;
    jfx_result_t r=m->definition.render(&m->host,f,out,capacity);
    m->busy=false; return r;
}
jfx_result_t jfx_renderer_module_close(jfx_renderer_module_t *m) {
    if (!m) return JFX_SUCCESS;
    if (m->busy) return JFX_ERROR_BUSY;
    jfx_renderer_host_t host=m->host;
    module_close(m->handle); host.release(host.context,m); return JFX_SUCCESS;
}
jfx_result_t jfx_renderer_module_peek(const char *path,jfx_renderer_info_t *out) {
    if (!path || !*path || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
    return JFX_ERROR_NOT_IMPLEMENTED;
#else
    module_handle_t handle=module_open_path(path);
    if (!handle) return JFX_ERROR_NOT_FOUND;
    void *symbol=module_symbol_of(handle,"jfx_renderer_entry");
    jfx_renderer_entry_fn entry=NULL;
    if (sizeof(symbol)==sizeof(entry)) memcpy(&entry,&symbol,sizeof(entry));
    if (!entry) { module_close(handle); return JFX_ERROR_NOT_FOUND; }
    jfx_renderer_definition_t d={0}; d.size=sizeof(d);
    jfx_result_t r=entry(JFX_RENDERER_ABI_MAJOR,JFX_RENDERER_ABI_MINOR,&d);
    if (r==JFX_SUCCESS && (d.size<sizeof(d) || !d.render || !valid_text(d.identifier) || !valid_text(d.name)))
        r=JFX_ERROR_INVALID_ARGUMENT;
    if (r==JFX_SUCCESS && (d.abi_major!=JFX_RENDERER_ABI_MAJOR || d.minimum_abi_minor>JFX_RENDERER_ABI_MINOR))
        r=JFX_ERROR_VERSION_MISMATCH;
    if (r==JFX_SUCCESS) {
        size_t size=out->size;
        memset(out,0,sizeof(*out)); out->size=size;
        out->abi_major=d.abi_major; out->minimum_abi_minor=d.minimum_abi_minor;
        snprintf(out->identifier,sizeof(out->identifier),"%s",d.identifier);
        snprintf(out->name,sizeof(out->name),"%s",d.name);
        out->version=d.version; out->capabilities=d.capabilities;
    }
    module_close(handle); return r;
#endif
}
jfx_result_t jfx_plugin_module_peek(const char *path,jfx_plugin_info_t *out) {
    if (!path || !*path || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
    return JFX_ERROR_NOT_IMPLEMENTED;
#else
    module_handle_t handle=module_open_path(path);
    if (!handle) return JFX_ERROR_NOT_FOUND;
    void *symbol=module_symbol_of(handle,"jfx_plugin_entry");
    jfx_plugin_entry_fn entry=NULL;
    if (sizeof(symbol)==sizeof(entry)) memcpy(&entry,&symbol,sizeof(entry));
    if (!entry) { module_close(handle); return JFX_ERROR_NOT_FOUND; }
    jfx_plugin_definition_t d={0}; d.size=sizeof(d);
    jfx_result_t r=entry(JFX_PLUGIN_SDK_MAJOR,JFX_PLUGIN_SDK_MINOR,&d);
    if (r==JFX_SUCCESS && (d.size<sizeof(d) || d.info.size<sizeof(d.info) || !d.initialize ||
        !valid_text_n(d.info.identifier,sizeof(out->identifier)) ||
        !valid_text_n(d.info.display_name,sizeof(out->display_name)) || !valid_text_n(d.info.vendor,sizeof(out->vendor))))
        r=JFX_ERROR_INVALID_ARGUMENT;
    if (r==JFX_SUCCESS && (d.sdk_major!=JFX_PLUGIN_SDK_MAJOR || d.minimum_sdk_minor>JFX_PLUGIN_SDK_MINOR))
        r=JFX_ERROR_VERSION_MISMATCH;
    if (r==JFX_SUCCESS) {
        size_t size=out->size;
        memset(out,0,sizeof(*out)); out->size=size;
        snprintf(out->identifier,sizeof(out->identifier),"%s",d.info.identifier);
        snprintf(out->display_name,sizeof(out->display_name),"%s",d.info.display_name);
        snprintf(out->vendor,sizeof(out->vendor),"%s",d.info.vendor);
        out->version=d.info.version; out->minimum_api_version=d.info.minimum_api_version;
        out->capabilities=d.info.capabilities;
    }
    module_close(handle); return r;
#endif
}
