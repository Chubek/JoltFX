#include "jfx/desktop_frontend.h"
#include "jfx/jfx_scheduler.h"
#include "jfx_test_backend.h"
#include "tilly/memory.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <GL/gl.h>
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static ImGuiWindow *window(const char *part) {
    for (auto *w:ImGui::GetCurrentContext()->Windows) if (w->Active && std::strstr(w->Name,part)) return w;
    std::fprintf(stderr,"Missing window: %s\n",part);
    for (auto *w:ImGui::GetCurrentContext()->Windows) std::fprintf(stderr,"%s (active=%d)\n",w->Name,int(w->Active));
    assert(false); return nullptr;
}
struct Frame { GLint width=0,height=0; tilly::vector<uint8_t> pixels; };
static Frame frame() {
    auto *w=window("3D viewport");
    auto atlas=ImGui::GetIO().Fonts->TexRef.GetTexID();
    for (const auto &cmd:w->DrawList->CmdBuffer) if (cmd.ElemCount && cmd.GetTexID()!=atlas) {
        Frame f;
        glBindTexture(GL_TEXTURE_2D,GLuint(cmd.GetTexID()));
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&f.width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&f.height);
        assert(f.width>0 && f.height>0);
        f.pixels.resize(size_t(f.width)*size_t(f.height)*4);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,f.pixels.data());
        glBindTexture(GL_TEXTURE_2D,0);
        assert(glGetError()==GL_NO_ERROR);
        size_t visible=0;
        for (size_t i=0;i<f.pixels.size();i+=4) {
            assert(f.pixels[i+3]==255);
            visible+=f.pixels[i]!=22 || f.pixels[i+1]!=26 || f.pixels[i+2]!=34;
        }
        assert(visible>0); return f;
    }
    assert(false); return {};
}
static void settle(jfx_desktop_frontend_t *f) {
    for (unsigned i=0;i<4;++i) { scheduler_wait_idle(); draw(f); }
}
static void click(jfx_desktop_frontend_t *f,const char *label,const char *part,int index=-1) {
    auto *w=window(part); auto &io=ImGui::GetIO(); io.AddMousePosEvent(0,0); draw(f);
    ImGui::DebugLocateItem(index<0?w->GetID(label):ImHashStr(label,0,w->GetID(index))); draw(f);
    ImRect bounds=w->InnerRect; bounds.Expand(4); ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    for (auto &v:ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x); lo.y=std::fmin(lo.y,v.pos.y); hi.x=std::fmax(hi.x,v.pos.x); hi.y=std::fmax(hi.y,v.pos.y);
    }
    assert(lo.x<=hi.x); io.AddMousePosEvent((lo.x+hi.x)/2,(lo.y+hi.y)/2); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f); io.AddMouseButtonEvent(0,false); draw(f);
}
int main() {
    jfx_desktop_frontend_config_t config={}; config.size=sizeof(config);
    config.width=1440; config.height=800; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_MODELING3D)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.add",8,0,0,0,"sphere")==JFX_SUCCESS);
    // Use the default async renderer, including MonkeyTrace when available.
    if (jfx_desktop_frontend_run(f,1,0)!=JFX_SUCCESS) { jfx_desktop_frontend_destroy(f); return 77; }
    settle(f); auto initial=frame();
    for (unsigned i=0;i<12;++i) {
        scheduler_wait_idle(); draw(f); auto current=frame();
        assert(current.width==initial.width && current.height==initial.height && current.pixels==initial.pixels);
    }
    assert(jfx_desktop_frontend_edit(f,"3d.color",0,0,0,.95,"")==JFX_SUCCESS);
    draw(f); assert(frame().pixels==initial.pixels); // old image while new snapshot runs
    assert(jfx_desktop_frontend_edit(f,"3d.color",0,0,0,.5,"")==JFX_SUCCESS);
    scheduler_wait_idle(); draw(f);
    assert(frame().pixels==initial.pixels); // completed result from prior edit is stale
    settle(f); auto colored=frame(); assert(colored.pixels!=initial.pixels);
    assert(jfx_desktop_frontend_edit(f,"3d.script",0,1,0,0,"(defkernel bad [time frame index value] (/ 1 time))")==JFX_SUCCESS);
    settle(f); assert(frame().pixels==colored.pixels); // failed renders keep the image
    assert(jfx_desktop_frontend_edit(f,"3d.script",0,1,0,0,"")==JFX_SUCCESS);
    settle(f);
    auto *reference=jfx_scene3d_create(); assert(reference);
    assert(jfx_scene3d_command(reference,"3d.add",8,0,0,0,"sphere")==JFX_SUCCESS);
    assert(jfx_scene3d_command(reference,"3d.color",0,0,0,.5,"")==JFX_SUCCESS);
    for (int mode:{1,2,0}) {
        click(f,"Viewport shading","3D viewport");
        click(f,mode==1?"Gouraud Shading":mode==2?"Wireframe Shading":"Rendered","##Combo_",mode);
        settle(f); auto current=frame();
        if (mode) {
            assert(current.pixels!=colored.pixels);
            tilly::vector<uint8_t> expected(current.pixels.size());
            assert(jfx_scene3d_render_shaded(reference,0,uint32_t(current.width),uint32_t(current.height),
                jfx_scene3d_shading_t(mode),expected.data(),expected.size())==JFX_SUCCESS);
            assert(expected==current.pixels);
        }
        else assert(current.pixels==colored.pixels);
    }
    jfx_scene3d_destroy(reference);
    // Half-resolution navigation and full-resolution restoration must retain a
    // valid opaque frame throughout, including the first resize request.
    auto *w=window("3D viewport"); auto &io=ImGui::GetIO();
    ImVec2 point(w->InnerRect.Min.x+80,w->InnerRect.Max.y-100);
    io.AddMousePosEvent(point.x,point.y); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f); draw(f);
    frame(); settle(f); auto half=frame();
    assert(half.width<colored.width && half.height<colored.height);
    io.AddMouseButtonEvent(0,false); draw(f); frame(); settle(f);
    auto full=frame(); assert(full.width==colored.width && full.height==colored.height);
    assert(full.pixels==colored.pixels);
    jfx_desktop_frontend_destroy(f);
}
