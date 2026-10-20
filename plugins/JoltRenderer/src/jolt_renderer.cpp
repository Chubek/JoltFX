#include <joltfx/sdk/renderer.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(JFX_JOLT_RENDERER_XSIMD) && (!defined(__EMSCRIPTEN__) || defined(__wasm_simd128__))
#include <xsimd/xsimd.hpp>
#if XSIMD_WITH_SSE2 || XSIMD_WITH_NEON || XSIMD_WITH_NEON64 || XSIMD_WITH_WASM
#define JOLT_USE_SIMD 1
#endif
#endif

namespace {
// Display-referred exposure resolve. Only this pixel stage uses xsimd; the
// Ingagi ECS/BVH tracing service is not advertised as SIMD ray intersection.
void resolve(uint8_t *pixels,size_t count,float gain) {
    size_t i=0;
#ifdef JOLT_USE_SIMD
    using batch=xsimd::batch<float>;
    constexpr size_t lanes=batch::size;
    float values[lanes],gains[lanes];
    for (;i+lanes<=count;i+=lanes) {
        for (size_t j=0;j<lanes;++j) { values[j]=float(pixels[i+j]); gains[j]=(i+j)%4==3?1.f:gain; }
        auto v=batch::load_unaligned(values)*batch::load_unaligned(gains);
        xsimd::min(v,batch(255.f)).store_unaligned(values);
        for (size_t j=0;j<lanes;++j) pixels[i+j]=uint8_t(values[j]+.5f);
    }
#endif
    for (;i<count;++i) if (i%4!=3) pixels[i]=uint8_t(std::min(255.f,float(pixels[i])*gain)+.5f);
}
jfx_result_t render(const jfx_renderer_host_t *h,const jfx_renderer_frame_t *f,uint8_t *out,size_t capacity) {
    if (!h || h->size<sizeof(*h) || !h->allocate || !h->release || !h->available || !h->render_scene ||
        !f || f->size<sizeof(*f) || !f->scene || !out || !f->width || !f->height || f->width>2048 || f->height>2048 ||
        capacity<size_t(f->width)*f->height*4 || !std::isfinite(f->seconds) || f->seconds<0 || f->seconds>1e9 ||
        (f->device!=JFX_RENDERER_DEVICE_CPU && f->device!=JFX_RENDERER_DEVICE_GPU) ||
        f->samples>64 || f->bounces>8 || f->timeout_ms>60000 || !std::isfinite(f->reflectivity) ||
        f->reflectivity<0 || f->reflectivity>1 || !std::isfinite(f->exposure) || f->exposure< -16 || f->exposure>16)
        return JFX_ERROR_INVALID_ARGUMENT;
    if (h->abi_major!=JFX_RENDERER_ABI_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    auto backend=f->device==JFX_RENDERER_DEVICE_CPU?JFX_RENDERER_BACKEND_INGAGI:JFX_RENDERER_BACKEND_VULKAN;
    if (!h->available(h->context,backend)) return JFX_ERROR_NOT_IMPLEMENTED;
    size_t bytes=size_t(f->width)*f->height*4;
    auto *scratch=static_cast<uint8_t *>(h->allocate(h->context,bytes,16));
    if (!scratch) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t result=JFX_ERROR_BACKEND_FAILURE;
    // No exceptions, retained callbacks, raw threads or engine linkage.
    try {
        result=h->render_scene(h->context,f,backend,scratch,bytes);
        if (result==JFX_SUCCESS) {
            resolve(scratch,bytes,std::exp2(f->exposure));
            std::memcpy(out,scratch,bytes);
        }
    } catch (...) { result=JFX_ERROR_BACKEND_FAILURE; }
    h->release(h->context,scratch); return result;
}
}

extern "C" jfx_result_t jfx_jolt_renderer_entry(uint32_t major,uint32_t minor,jfx_renderer_definition_t *out) {
    (void)minor;
    if (!out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    if (major!=JFX_RENDERER_ABI_MAJOR) return JFX_ERROR_VERSION_MISMATCH;
    uint32_t capabilities=JFX_RENDERER_CAP_CPU|JFX_RENDERER_CAP_GPU;
#ifdef JOLT_USE_SIMD
    capabilities|=JFX_RENDERER_CAP_SIMD;
#endif
    *out={sizeof(*out),JFX_RENDERER_ABI_MAJOR,0,"org.joltfx.renderer","Jolt Renderer",0x01000000u,capabilities,render};
    return JFX_SUCCESS;
}
#ifdef JFX_JOLT_RENDERER_MODULE
extern "C" JFX_RENDERER_EXPORT jfx_result_t jfx_renderer_entry(uint32_t major,uint32_t minor,jfx_renderer_definition_t *out) {
    return jfx_jolt_renderer_entry(major,minor,out);
}
#endif
