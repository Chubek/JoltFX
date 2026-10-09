#include "cpu_numeric.h"
#include <chrono>
#include <cstdio>
#include <vector>
int main() {
    constexpr size_t count=1920*1080*4;
    std::vector<float> src(count,.25f),dst(count); std::vector<uint8_t> bytes(count,127);
    auto measure=[&](const char *name,auto fn) {
        auto start=std::chrono::steady_clock::now();
        for (int i=0;i<50;++i) fn();
        std::printf("%s lanes=%zu: %.3f ms checksum=%g\n",name,jfx_cpu_lanes(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/50,double(dst[99]));
    };
    measure("exposure",[&] { jfx_cpu_exposure(dst.data(),src.data(),count/4,1.5f); });
    measure("decode",[&] { jfx_cpu_decode_unorm(dst.data(),bytes.data(),count); });
    measure("accumulate",[&] { jfx_cpu_accumulate(dst.data(),src.data(),count,.3f,.7f); });
}
