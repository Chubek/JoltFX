#include "jfx/jfx_modeling3d.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cmath>
#include <cstring>

static void edit(jfx_scene3d_t *s,const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double v=0,const char *text="") {
    assert(jfx_scene3d_command(s,op,a,b,c,v,text)==JFX_SUCCESS);
}
static tilly::string save(jfx_scene3d_t *s) {
    tilly::vector<char> data(16*1024*1024); size_t n=0;
    assert(jfx_scene3d_save(s,data.data(),data.size(),&n)==JFX_SUCCESS);
    return {data.data(),n};
}
static jfx_object3d_info_t info(jfx_scene3d_t *s,uint32_t i=0) {
    jfx_object3d_info_t result{}; result.size=sizeof(result);
    assert(jfx_scene3d_object_info(s,i,&result)==JFX_SUCCESS); return result;
}
int main() {
    auto *s=jfx_scene3d_create(); assert(s);
    edit(s,"3d.add",32,0,0,0,"sphere"); edit(s,"3d.view");
    auto base=save(s);
    for (uint32_t brush=0;brush<5;++brush) {
        edit(s,"3d.sculpt_begin");
        for (int i=0;i<3;++i) edit(s,"3d.sculpt",0,brush,1,.1,"0.53 0.5 1 0.6 0");
        assert(save(s)!=base);
        edit(s,"3d.sculpt_end"); auto sculpted=save(s);
        edit(s,"undo"); assert(save(s)==base);
        edit(s,"redo"); assert(save(s)==sculpted);
        edit(s,"undo");
    }
    edit(s,"3d.sculpt_begin"); edit(s,"3d.sculpt",0,0,0,.1,"0.5 0.5 1 0.6 0");
    assert(jfx_scene3d_command(s,"3d.remove",0,0,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    edit(s,"3d.sculpt_cancel"); assert(save(s)==base);
    // Picking uses animation and inverse nonuniform scale, not raw mesh XYZ.
    edit(s,"3d.transform",0,6,0,2); edit(s,"3d.transform",0,7,0,.7);
    edit(s,"3d.key",0,0,0,3); edit(s,"3d.key",0,0,30,0);
    auto transformed=save(s);
    edit(s,"3d.sculpt",0,0,0,.1,"0.5 0.5 1 0.6 1"); assert(save(s)!=transformed);
    edit(s,"undo"); assert(save(s)==transformed);
    edit(s,"undo"); edit(s,"undo"); edit(s,"undo"); edit(s,"undo"); assert(save(s)==base);
    // Foreground geometry occludes a selected mesh; no edits through other objects.
    edit(s,"3d.add",0,0,0,0,"cube"); edit(s,"3d.transform",1,2,0,3);
    auto occluded=save(s);
    edit(s,"3d.sculpt",0,0,0,.1,"0.5 0.5 1 0.6 0"); assert(save(s)==occluded);
    edit(s,"undo"); edit(s,"undo"); assert(save(s)==base);
    // Misses and invalid strokes neither deform the mesh nor consume history.
    jfx_scene3d_clear_history(s);
    edit(s,"3d.sculpt_begin"); edit(s,"3d.sculpt",0,0,0,.1,"0 0 1 0.1 0"); edit(s,"3d.sculpt_end");
    assert(save(s)==base && !jfx_scene3d_can_undo(s));
    for (auto text:{"0.5 0.5 1 -1 0","nan 0.5 1 1 0","0.5 0.5 0 1 0","0.5 0.5 1 1 0 extra"})
        assert(jfx_scene3d_command(s,"3d.sculpt",0,0,0,.1,text)==JFX_ERROR_INVALID_ARGUMENT && save(s)==base);
    assert(jfx_scene3d_command(s,"3d.sculpt",0,5,0,.1,"0.5 0.5 1 1 0")==JFX_ERROR_INVALID_ARGUMENT);
    // Each LOD derives from the sculpt, preserves its transform/keys and leaves
    // the source untouched. High is a lossless triangular refinement.
    edit(s,"3d.key",0,0,30,2);
    auto source=info(s); base=save(s);
    for (uint32_t level=0;level<3;++level) {
        edit(s,"3d.poly_copy",0,level);
        auto derived=info(s,1);
        assert(derived.keys==source.keys);
        assert(level==2?derived.triangles==source.triangles*4:derived.triangles<source.triangles);
        assert(std::strstr(derived.name,level==0?"Low-Poly":level==1?"Mid-Poly":"High-Poly"));
        auto text=save(s); auto *copy=jfx_scene3d_create(); assert(copy);
        assert(jfx_scene3d_load(copy,text.data(),text.size(),nullptr,0)==JFX_SUCCESS && save(copy)==text);
        jfx_scene3d_destroy(copy); edit(s,"undo"); assert(save(s)==base);
    }
    edit(s,"3d.optimize"); assert(info(s).triangles==source.triangles); edit(s,"undo"); assert(save(s)==base);
    edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,"cube"); edit(s,"3d.hull");
    assert(info(s).vertices==8 && info(s).triangles==12);
    edit(s,"3d.path_bake",0,10,20,0,"-2 0 0  -1 1 0  1 1 0  2 0 0");
    float t[9]; assert(jfx_scene3d_sample(s,0,10./30,t)==JFX_SUCCESS && std::abs(t[0]+1)<1e-5f);
    assert(jfx_scene3d_sample(s,0,20./30,t)==JFX_SUCCESS && std::abs(t[0]-1)<1e-5f);
    assert(jfx_scene3d_sample(s,0,15./30,t)==JFX_SUCCESS && t[1]>1);
    base=save(s);
    assert(jfx_scene3d_command(s,"3d.path_bake",0,20,10,0,"")==JFX_ERROR_INVALID_ARGUMENT && save(s)==base);
    edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,"metaball"); edit(s,"3d.triangulate");
    jfx_procedural3d_info_t p{}; p.size=sizeof(p);
    assert(jfx_scene3d_procedural_info(s,0,&p)==JFX_SUCCESS && p.generator==0);
    edit(s,"undo"); assert(jfx_scene3d_procedural_info(s,0,&p)==JFX_SUCCESS && p.generator==2);
    jfx_scene3d_destroy(s);
}
