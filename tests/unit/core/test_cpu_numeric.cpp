#include "cpu_numeric.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
int main() {
    float special[8]={-0.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),.25f,-1,2,.5f,.75f};
    jfx_cpu_exposure(special,special,2,1);
    assert(special[0]==0 && !std::signbit(special[0]) && special[1]==0 && special[2]==1 && special[3]==.25f && special[7]==.75f);
    for (size_t n=0;n<259;++n) {
        std::vector<float> src(n+2),dst(n+2,17),expected(dst);
        std::vector<uint8_t> bytes(n+2);
        for (size_t i=0;i<n;++i) { src[i+1]=float(int(i%31)-15)/7; bytes[i+1]=uint8_t(i); expected[i+1]+=src[i+1]*.3f*.7f; }
        jfx_cpu_accumulate(dst.data()+1,src.data()+1,n,.3f,.7f);
        for (size_t i=0;i<n+2;++i) assert(std::abs(dst[i]-expected[i])<1e-6f);
        jfx_cpu_decode_unorm(dst.data()+1,bytes.data()+1,n);
        for (size_t i=0;i<n;++i) assert(dst[i+1]==float(bytes[i+1])/255.f);
        assert(dst.front()==17 && dst.back()==17);
        assert(jfx_cpu_finite(src.data()+1,n));
        for (size_t i=0;i<n;++i) {
            float old=src[i+1]; src[i+1]=std::numeric_limits<float>::quiet_NaN(); assert(!jfx_cpu_finite(src.data()+1,n));
            src[i+1]=std::numeric_limits<float>::infinity(); assert(!jfx_cpu_finite(src.data()+1,n)); src[i+1]=old;
        }
        std::vector<float> rgba(n*4+2,17);
        for (size_t i=0;i<n*4;++i) rgba[i+1]=float(int(i%23)-4)/9;
        auto ref=rgba;
        for (size_t i=0;i<n*4;++i) if (i%4!=3) { float v=ref[i+1]*1.7f; ref[i+1]=v<0?0:v>1?1:v; }
        jfx_cpu_exposure(rgba.data()+1,rgba.data()+1,n,1.7f);
        assert(rgba==ref);
    }
}
