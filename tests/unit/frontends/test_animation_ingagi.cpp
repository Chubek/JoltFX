#include "../../../frontends/desktop/src/animation_script.h"
#include <cassert>
#include <cstring>

int main() {
    jfx_drawing::Editor e;char error[1024]{};
    e.doc.frames=3;
    assert(jfx_drawing::bake_ingagi(e,error,sizeof(error)));
    assert(e.doc.layers[0].cels.size()==3);
    assert(e.cel().shapes.size()==1);
    const auto first=e.cel().shapes[0].points[0].x;
    e.frame=3;assert(e.cel().shapes[0].points[0].x>first);
    e.undo();assert(e.cel().shapes.empty());e.redo();assert(e.cel().shapes.size()==1);
    auto baseline=e.serialize();const auto history=e.past.size();
    std::strcpy(e.script,"fn frame(t: f64) { loop {} }");
    assert(!jfx_drawing::bake_ingagi(e,error,sizeof(error)));
    assert(std::strstr(error,"budget"));assert(e.serialize()==baseline && e.past.size()==history);
    std::strcpy(e.script,"fn frame(t: f64) { return [anim_path(anim_star(vec2(200,200),60,30,5,t),true,4278190335,4278190080,2)]; }");
    assert(jfx_drawing::bake_ingagi(e,error,sizeof(error)));
    assert(e.cel().shapes[0].kind==jfx_drawing::Kind::Polygon);
    std::strcpy(e.html_path,"animation-ingagi-test.html");
    assert(jfx_drawing::export_canvas(e,error,sizeof(error)));
    tilly::string html;assert(e.read_file(e.html_path,html,1024*1024));
    assert(html.find("requestAnimationFrame")!=tilly::string::npos);
    assert(html.find("\"kind\":4")!=tilly::string::npos);
    std::remove(e.html_path);
    bool called=false;
    auto output=[](void *user,const char *,size_t)->int{*static_cast<bool *>(user)=true;return 0;};
    assert(ig_animation_bake("bad",3,24,3,0,output,&called,error,sizeof(error))!=0);
    assert(!called);
    assert(ig_animation_export("bad",3,1,output,&called,error,sizeof(error))!=0);
    assert(!called);
}
