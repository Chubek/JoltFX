#include "joltfx/sdk/renderer.h"
#include "joltfx/sdk/loader.h"
#include "tilly/allocator.h"
#include <cassert>
#include <cmath>
#include <cstring>

extern "C" jfx_result_t jfx_jolt_renderer_entry(uint32_t,uint32_t,jfx_renderer_definition_t *);
static unsigned calls;
static bool fail;
static bool enabled=true,no_memory;
static jfx_renderer_module_t *active;
static void *allocate(void *,size_t n,size_t alignment) {
    if (no_memory) return nullptr;
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,alignment);
}
static void release(void *,void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),p); }
static bool available(void *,jfx_renderer_backend_t) { return enabled; }
static jfx_result_t render(void *,const jfx_renderer_frame_t *f,jfx_renderer_backend_t backend,uint8_t *out,size_t) {
    ++calls; assert(backend==JFX_RENDERER_BACKEND_INGAGI || backend==JFX_RENDERER_BACKEND_VULKAN);
    if (active) {
        assert(jfx_renderer_module_close(active)==JFX_ERROR_BUSY);
        assert(jfx_renderer_module_render(active,f,out,size_t(f->width)*f->height*4)==JFX_ERROR_BUSY);
    }
    for (size_t i=0;i<size_t(f->width)*f->height*4;++i) out[i]=uint8_t(i%256);
    return fail?JFX_ERROR_BACKEND_FAILURE:JFX_SUCCESS;
}
int main(int argc,char **argv) {
    jfx_renderer_definition_t d={}; d.size=sizeof(d);
    assert(jfx_jolt_renderer_entry(999,0,&d)==JFX_ERROR_VERSION_MISMATCH);
    assert(jfx_jolt_renderer_entry(JFX_RENDERER_ABI_MAJOR,0,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_jolt_renderer_entry(JFX_RENDERER_ABI_MAJOR,0,&d)==JFX_SUCCESS);
    assert(!std::strcmp(d.name,"Jolt Renderer") && d.render && d.capabilities&JFX_RENDERER_CAP_CPU);
    jfx_renderer_host_t host={sizeof(host),JFX_RENDERER_ABI_MAJOR,0,nullptr,allocate,release,available,render};
    uint8_t out[7*3*4+2]; std::memset(out,99,sizeof(out));
    jfx_renderer_frame_t f={}; f.size=sizeof(f); f.scene=&host; f.width=7; f.height=3;
    f.device=JFX_RENDERER_DEVICE_CPU; f.samples=4; f.bounces=2; f.reflectivity=.15f;
    assert(d.render(&host,&f,out+1,sizeof(out)-2)==JFX_SUCCESS && calls==1);
    for (size_t i=0;i<sizeof(out)-2;++i) assert(out[i+1]==uint8_t(i));
    assert(out[0]==99 && out[sizeof(out)-1]==99);
    f.exposure=1;
    assert(d.render(&host,&f,out+1,sizeof(out)-2)==JFX_SUCCESS);
    for (size_t i=0;i<sizeof(out)-2;++i) assert(out[i+1]==(i%4==3?uint8_t(i):uint8_t(i*2>255?255:i*2)));
    std::memset(out,99,sizeof(out)); fail=true;
    assert(d.render(&host,&f,out+1,sizeof(out)-2)==JFX_ERROR_BACKEND_FAILURE);
    for (auto p:out) assert(p==99);
    fail=false; f.width=0;
    assert(d.render(&host,&f,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    f.width=7; f.seconds=double(NAN);
    assert(d.render(&host,&f,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    f.seconds=0; f.exposure=NAN;
    assert(d.render(&host,&f,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    f.exposure=0; f.device=JFX_RENDERER_DEVICE_GPU;
    assert(d.render(&host,&f,out+1,sizeof(out)-2)==JFX_SUCCESS);
    assert(d.render(nullptr,&f,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    assert(d.render(&host,nullptr,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    assert(d.render(&host,&f,nullptr,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    assert(d.render(&host,&f,out,1)==JFX_ERROR_INVALID_ARGUMENT);
    enabled=false; std::memset(out,99,sizeof(out));
    assert(d.render(&host,&f,out,sizeof(out))==JFX_ERROR_NOT_IMPLEMENTED);
    enabled=true; no_memory=true;
    assert(d.render(&host,&f,out,sizeof(out))==JFX_ERROR_OUT_OF_MEMORY);
    for (auto p:out) assert(p==99);
    no_memory=false;
    jfx_renderer_module_t *module=nullptr;
    assert(jfx_renderer_module_open(nullptr,&host,&module)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_open("/nonexistent/jolt-renderer",&host,&module)==JFX_ERROR_NOT_FOUND && !module);
    assert(jfx_renderer_module_attach(nullptr,&host,&module)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_attach(&d,nullptr,&module)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_attach(&d,&host,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
    auto invalid=d; invalid.abi_major=999;
    assert(jfx_renderer_module_attach(&invalid,&host,&module)==JFX_ERROR_VERSION_MISMATCH && !module);
    no_memory=true;
    assert(jfx_renderer_module_attach(&d,&host,&module)==JFX_ERROR_OUT_OF_MEMORY && !module);
    no_memory=false;
    assert(jfx_renderer_module_info(nullptr,&d)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_render(nullptr,&f,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_attach(&d,&host,&module)==JFX_SUCCESS);
    active=module;
    assert(jfx_renderer_module_render(module,&f,out,sizeof(out))==JFX_SUCCESS);
    active=nullptr;
    assert(jfx_renderer_module_info(module,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_render(module,nullptr,out,sizeof(out))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_renderer_module_close(module)==JFX_SUCCESS); module=nullptr;
    if (argc>1) {
        assert(jfx_renderer_module_open(argv[1],&host,&module)==JFX_SUCCESS);
        jfx_renderer_definition_t info={}; info.size=sizeof(info);
        assert(jfx_renderer_module_info(module,&info)==JFX_SUCCESS && !std::strcmp(info.name,d.name));
        active=module;
        assert(jfx_renderer_module_render(module,&f,out+1,sizeof(out)-2)==JFX_SUCCESS);
        active=nullptr;
        jfx_renderer_module_close(module);
    }
    jfx_renderer_module_close(nullptr);
}
