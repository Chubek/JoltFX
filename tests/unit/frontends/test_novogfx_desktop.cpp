/* Real CPU rasterization: no DRM master, display server or OpenGL needed. */
#include "software_ui.h"
#include "host_window.h"
#include "imgui.h"
#include "jfx/desktop_frontend.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

int main() {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    {
        jfx_software_ui ui;
        assert(!ui.init(0, 200));
        assert(ui.init(320, 200));
        assert(!ui.init(320, 200));
        const uint8_t red[] = {255, 0, 0, 255};
        assert(!ui.upload(1, 1, nullptr));
        for (int frame = 0; frame < 3; ++frame) {
            void *texture = ui.upload(1, 1, red);
            const uint8_t blue[] = {0,0,255,255, 0,0,255,255};
            void *second = ui.upload(2, 1, blue);
            assert(texture && second && texture != second);
            ImGuiIO &io = ImGui::GetIO();
            io.DisplaySize = ImVec2(320, 200); io.DeltaTime = 1.0f/60;
            ImGui::NewFrame();
            auto *draw = ImGui::GetBackgroundDrawList();
            draw->AddRectFilled(ImVec2(10,10), ImVec2(40,40), IM_COL32(0,255,0,255));
            draw->PushClipRect(ImVec2(50,10), ImVec2(65,40), true);
            draw->AddImage(ImTextureID(reinterpret_cast<uintptr_t>(texture)), ImVec2(50,10), ImVec2(80,40));
            draw->PopClipRect();
            draw->AddImage(ImTextureID(reinterpret_cast<uintptr_t>(second)), ImVec2(100,10), ImVec2(130,40));
            draw->AddText(ImVec2(10,70), IM_COL32_WHITE, "NovoGFX editor");
            ImGui::Render();
            assert(ui.render(ImGui::GetDrawData()));
            auto pixel = [&](int x, int y, int c) { return ui.pixels[size_t(y*320+x)*4+size_t(c)]; };
            assert(pixel(20,20,1) == 255 && pixel(20,20,0) == 0);
            assert(pixel(55,20,0) == 255 && pixel(55,20,1) == 0);
            assert(pixel(70,20,0) == 15 && pixel(70,20,1) == 18);
            assert(pixel(110,20,2) == 255 && pixel(110,20,0) == 0);
            int text_pixels = 0;
            for (int y=70; y<95; ++y) for (int x=10; x<200; ++x) if (pixel(x,y,0)>100) ++text_pixels;
            assert(text_pixels > 100);
        }
    }
    assert(ImGui::GetIO().BackendRendererUserData == nullptr);
    ImGui::DestroyContext();
    /* The actual editor's full draw lists also rasterize without a window. */
    jfx_desktop_frontend_config_t config{};
    config.size = sizeof(config); config.backend_name = "novogfx";
    config.width = 960; config.height = 640;
    jfx_desktop_frontend_t *frontend = nullptr;
    assert(jfx_desktop_frontend_create(&config, &frontend) == JFX_SUCCESS);
    {
        // Force a non-VT stdin even when a developer runs tests on a real VT.
        // Explicit NovoGFX must fail safely, never open an SDL/GL fallback.
        int saved = dup(STDIN_FILENO), input = open("/dev/null", O_RDONLY);
        assert(saved >= 0 && input >= 0 && dup2(input, STDIN_FILENO) >= 0);
        jfx_desktop_window_config_t host{960,640,"NovoGFX guard",false,true};
        char error[256]{};
        auto *window = jfx_desktop_window_create(&host, error, sizeof(error));
        assert(!window && std::strstr(error, "active Linux virtual console"));
        assert(dup2(saved, STDIN_FILENO) >= 0);
        close(saved); close(input);
    }
    {
        jfx_software_ui ui;
        assert(ui.init(960,640));
        for (int i=0; i<3; ++i) {
            assert(jfx_desktop_frontend_draw(frontend) == JFX_SUCCESS);
            assert(ui.render(ImGui::GetDrawData()));
            size_t visible = 0;
            for (size_t p=0; p<ui.pixels.size(); p+=4) if (ui.pixels[p]>40) ++visible;
            assert(visible > 1000);
        }
        assert(jfx_desktop_frontend_frame_count(frontend)==3);
    }
    jfx_desktop_frontend_destroy(frontend);
    std::puts("NovoGFX software editor pixels: OK");
}
