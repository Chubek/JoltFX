/* Drive the actual Dear ImGui canvas, then inspect the shared document/pixels.
 * This catches widget hit-testing and gesture commits beyond model API tests. */
#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include <cassert>
#include <cstring>
#include <limits>

static void frame(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static void move(jfx_desktop_frontend_t *f,ImVec2 p) { ImGui::GetIO().AddMousePosEvent(p.x,p.y); frame(f); }
static void button(jfx_desktop_frontend_t *f,int b,bool down) { ImGui::GetIO().AddMouseButtonEvent(b,down); frame(f); }
static ImVec2 canvas_origin() {
    ImVec2 origin(std::numeric_limits<float>::max(),std::numeric_limits<float>::max());
    ImDrawData *data=ImGui::GetDrawData();
    for (int l=0;l<data->CmdListsCount;++l) for (const auto &vertex:data->CmdLists[l]->VtxBuffer)
        if (vertex.col==IM_COL32(25,30,40,255)) { if (vertex.pos.x<origin.x) origin.x=vertex.pos.x; if (vertex.pos.y<origin.y) origin.y=vertex.pos.y; }
    assert(origin.x<std::numeric_limits<float>::max()); return origin;
}
static void state(jfx_desktop_frontend_t *f,char *out,size_t cap) { assert(jfx_desktop_frontend_graph_state(f,out,cap)==JFX_SUCCESS); }
static void edit(jfx_desktop_frontend_t *f,const char *op,uint32_t a=0) { assert(jfx_desktop_frontend_edit(f,op,a,0,0,0,"")==JFX_SUCCESS); }
int main() {
    jfx_desktop_frontend_config_t config{}; config.size=sizeof(config); config.width=1280; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    for (int p=0;p<JFX_DESKTOP_PANEL_COUNT;++p) assert(jfx_desktop_frontend_set_panel_visible(f,(jfx_desktop_panel_t)p,p==JFX_DESKTOP_PANEL_NODE_COMPOSITING)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_node_compositing_new_graph(f)==JFX_SUCCESS);
    uint32_t n; assert(jfx_desktop_frontend_node_compositing_add_node(f,"invert",nullptr,&n)==JFX_SUCCESS && n==1);
    edit(f,"node.output",1); frame(f);
    assert(jfx_desktop_frontend_workspace(f)==JFX_DESKTOP_WORKSPACE_COMPOSITING); frame(f);
    ImVec2 origin=canvas_origin();
    auto point=[&](float x,float y) { return ImVec2(origin.x+20+x*.8f,origin.y+20+y*.8f); };
    auto drag=[&](ImVec2 from,ImVec2 to) { move(f,from); button(f,0,true); move(f,to); button(f,0,false); };
    uint8_t pixels[4]; char json[8192],before[8192];
    assert(jfx_desktop_frontend_render_graph(f,UINT32_MAX,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==0);
    drag(point(210,44),point(240,44));
    assert(jfx_desktop_frontend_render_graph(f,UINT32_MAX,0,1,1,pixels,4)==JFX_SUCCESS && pixels[0]==0 && pixels[3]==255);
    state(f,json,sizeof(json)); assert(std::strstr(json,"\"source\":0,\"port\":0"));
    drag(point(260,20),point(310,50));
    state(f,json,sizeof(json)); assert(std::strstr(json,"\"x\":290,\"y\":30"));
    edit(f,"undo"); state(f,json,sizeof(json)); assert(std::strstr(json,"\"x\":240,\"y\":0") && std::strstr(json,"\"source\":0"));
    edit(f,"redo"); frame(f);
    move(f,point(290,74)); button(f,1,true); button(f,1,false);
    assert(jfx_desktop_frontend_render_graph(f,UINT32_MAX,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==0);
    edit(f,"undo"); frame(f); state(f,before,sizeof(before));
    /* A self-wire must preserve both the old input and redo availability. */
    drag(point(500,74),point(290,74)); state(f,json,sizeof(json)); assert(!std::strcmp(json,before));
    assert(jfx_desktop_frontend_render_graph(f,UINT32_MAX,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==255);
    jfx_desktop_frontend_destroy(f);
}
