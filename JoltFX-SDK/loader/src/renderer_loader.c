#include <joltfx/sdk/loader.h>
#include <string.h>
#if defined(__EMSCRIPTEN__) || defined(__wasi__)
typedef void *module_handle_t;
static void module_close(module_handle_t p) { (void)p; }
#elif defined(_WIN32)
#include <windows.h>
typedef HMODULE module_handle_t;
static void module_close(module_handle_t p) { if (p) FreeLibrary(p); }
#else
#include <dlfcn.h>
typedef void *module_handle_t;
static void module_close(module_handle_t p) { if (p) dlclose(p); }
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
static bool valid_text(const char *s) {
    if (!s || !*s) return false;
    for (size_t i=0;i<96;++i) if (!s[i]) return true;
    return false;
}
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
#if defined(_WIN32)
    module_handle_t handle=LoadLibraryA(path);
    FARPROC symbol=handle?GetProcAddress(handle,"jfx_renderer_entry"):NULL;
#else
    module_handle_t handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    void *symbol=handle?dlsym(handle,"jfx_renderer_entry"):NULL;
#endif
    if (!handle) return JFX_ERROR_NOT_FOUND;
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
