#include "jfx/jfx_modeling3d.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

/* Offscreen Vulkan scene renderer vs the CPU rasterizer. Without a driver
 * the GPU entry must fail closed with output untouched (fallback contract);
 * with a real device the images must agree up to single-sample edges. */
int main() {
    uint8_t poison[64];
    memset(poison, 0xA5, sizeof(poison));
    assert(jfx_scene3d_render_gpu(nullptr, 0, 64, 64, poison, sizeof(poison)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(poison[0] == 0xA5);
    auto *null_scene_check = jfx_scene3d_create();
    assert(null_scene_check);
    assert(jfx_scene3d_render_gpu(null_scene_check, 0, 0, 64, poison,
               sizeof(poison)) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_scene3d_render_gpu(null_scene_check, 0, 3000, 64, poison,
               sizeof(poison)) == JFX_ERROR_INVALID_ARGUMENT);

    auto *s = jfx_scene3d_create();
    assert(s);
    assert(jfx_scene3d_command(s, "3d.add", 0, 0, 0, 0, "cube") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.add", 0, 0, 0, 0, "plane") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.transform", 1, 1, 0, -2, "") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 0, 4, 0, 0, "") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 0, 4, 90, 360, "") == JFX_SUCCESS);

    constexpr uint32_t kW = 320, kH = 180;
    tilly::vector<uint8_t> cpu(size_t(kW) * kH * 4), gpu(size_t(kW) * kH * 4, 0xA5);
    assert(jfx_scene3d_render(s, 1.0, kW, kH, cpu.data(), cpu.size()) == JFX_SUCCESS);
    jfx_result_t r = jfx_scene3d_render_gpu(s, 1.0, kW, kH, gpu.data(), gpu.size());
    if (r == JFX_ERROR_NOT_IMPLEMENTED) {
        std::printf("vulkan scene: backend disabled, CPU fallback contract holds\n");
        jfx_scene3d_destroy(null_scene_check);
        jfx_scene3d_destroy(s);
        return 0;
    }
    if (r != JFX_SUCCESS) {
        /* No usable device here: output must be untouched. */
        for (uint8_t byte : gpu) assert(byte == 0xA5);
        std::printf("vulkan scene: no device, fallback contract holds\n");
        jfx_scene3d_destroy(null_scene_check);
        jfx_scene3d_destroy(s);
        return 0;
    }
    /* Both rendered: background is (22,26,34); compare coverage and pixels. */
    size_t cpu_fg = 0, gpu_fg = 0, diff = 0;
    double total_abs = 0;
    for (size_t i = 0; i < size_t(kW) * kH; ++i) {
        bool c = cpu[i * 4] != 22 || cpu[i * 4 + 1] != 26 || cpu[i * 4 + 2] != 34;
        bool g = gpu[i * 4] != 22 || gpu[i * 4 + 1] != 26 || gpu[i * 4 + 2] != 34;
        cpu_fg += c;
        gpu_fg += g;
        double worst = 0;
        for (int ch = 0; ch < 3; ++ch)
            worst = std::max(worst,
                std::fabs(double(cpu[i * 4 + ch]) - double(gpu[i * 4 + ch])));
        total_abs += worst;
        if (worst > 12) ++diff;
    }
    double pixels = double(kW) * kH;
    std::printf("vulkan scene: cpu_fg=%.3f gpu_fg=%.3f diff_frac=%.4f mean_abs=%.3f\n",
        cpu_fg / pixels, gpu_fg / pixels, diff / pixels, total_abs / pixels);
    assert(cpu_fg > pixels * 0.05);
    assert(std::fabs(double(gpu_fg) - double(cpu_fg)) / cpu_fg < 0.20);
    assert(diff / pixels < 0.10);
    jfx_scene3d_destroy(null_scene_check);
    jfx_scene3d_destroy(s);
    std::printf("vulkan scene ok\n");
    return 0;
}
