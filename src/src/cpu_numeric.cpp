#include "cpu_numeric.h"
#include <xsimd/xsimd.hpp>
#include <cmath>
#include <limits>
#if !defined(JFX_CPU_SIMD_OFF) && !defined(XSIMD_NO_SUPPORTED_ARCHITECTURE)
#define JFX_VECTOR 1
using Batch=xsimd::batch<float>;
#endif
extern "C" size_t jfx_cpu_lanes() {
#ifdef JFX_VECTOR
    return Batch::size;
#else
    return 1;
#endif
}
extern "C" void jfx_cpu_accumulate(float *dst,const float *src,size_t count,float gain,float master) {
    size_t i=0;
#ifdef JFX_VECTOR
    for (;count-i>=Batch::size;i+=Batch::size)
        (Batch::load_unaligned(dst+i)+(Batch::load_unaligned(src+i)*Batch(gain))*Batch(master)).store_unaligned(dst+i);
#endif
    for (;i<count;++i) dst[i]+=src[i]*gain*master;
}
extern "C" void jfx_cpu_exposure(float *dst,const float *src,size_t pixels,float gain) {
    size_t i=0,count=pixels*4;
#ifdef JFX_VECTOR
    alignas(64) float mask[Batch::size];
    for (size_t j=0;j<Batch::size;++j) mask[j]=j%4==3?1.f:0.f;
    const auto alpha=Batch::load_unaligned(mask)!=Batch(0.f);
    // Supported float batch widths are multiples of RGBA's four channels.
    static_assert(Batch::size%4==0,"RGBA batch alignment");
    for (;count-i>=Batch::size;i+=Batch::size) {
        auto s=Batch::load_unaligned(src+i),v=s*Batch(gain);
        v=xsimd::select(v>Batch(0.f),xsimd::select(v>=Batch(1.f),Batch(1.f),v),Batch(0.f));
        xsimd::select(alpha,s,v).store_unaligned(dst+i);
    }
#endif
    for (;i<count;++i) { float v=src[i]*gain; dst[i]=i%4==3?src[i]:!(v>0)?0:v>=1?1:v; }
}
extern "C" void jfx_cpu_decode_unorm(float *dst,const uint8_t *src,size_t count) {
    // Compilers vectorize byte widening better than xsimd's generic converting
    // load on the baseline SSE2 target; keep the measured faster loop here.
    for (size_t i=0;i<count;++i) dst[i]=float(src[i])/255.f;
}
extern "C" bool jfx_cpu_finite(const float *src,size_t count) {
    size_t i=0;
#ifdef JFX_VECTOR
    for (;count-i>=Batch::size;i+=Batch::size)
        if (!xsimd::all(xsimd::abs(Batch::load_unaligned(src+i))<=Batch(std::numeric_limits<float>::max()))) return false;
#endif
    for (;i<count;++i) if (!std::isfinite(src[i])) return false;
    return true;
}
