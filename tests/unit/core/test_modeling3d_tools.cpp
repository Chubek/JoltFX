#include "jfx/jfx_modeling3d.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <array>
#include <algorithm>
#include <cstdio>

static void edit(jfx_scene3d_t *s,const char *op,uint32_t a=0,uint32_t b=0,uint32_t c=0,double v=0,const char *text="") {
    assert(jfx_scene3d_command(s,op,a,b,c,v,text)==JFX_SUCCESS);
}
static tilly::string save(jfx_scene3d_t *s) {
    tilly::vector<char> text(16*1024*1024); size_t n=0;
    assert(jfx_scene3d_save(s,text.data(),text.size(),&n)==JFX_SUCCESS);
    return {text.data(),n};
}
// New solids must be closed, consistently oriented two-manifold meshes, with
// positive enclosed volume. The hollow tube has genus one; the other solids
// have sphere topology. Inspect serialized faces through the public API.
static void closed_solid(jfx_scene3d_t *s,const char *kind) {
    jfx_object3d_info_t info={}; info.size=sizeof(info);
    assert(jfx_scene3d_object_info(s,0,&info)==JFX_SUCCESS);
    tilly::vector<std::array<float,3>> vertices(info.vertices);
    for (uint32_t i=0;i<info.vertices;++i) assert(jfx_scene3d_vertex(s,0,i,vertices[i].data())==JFX_SUCCESS);
    tilly::vector<std::array<uint32_t,3>> edges;
    auto text=save(s); double volume=0; uint32_t faces=0;
    for (size_t start=0;start<text.size();) {
        uint32_t a,b,c;
        if (std::sscanf(text.c_str()+start,"f %u %u %u",&a,&b,&c)==3) {
            assert(a<vertices.size() && b<vertices.size() && c<vertices.size()); ++faces;
            auto &p=vertices[a],&q=vertices[b],&r=vertices[c];
            volume+=double(p[0])*(double(q[1])*double(r[2])-double(q[2])*double(r[1]))+
                double(p[1])*(double(q[2])*double(r[0])-double(q[0])*double(r[2]))+
                double(p[2])*(double(q[0])*double(r[1])-double(q[1])*double(r[0]));
            const uint32_t face[]={a,b,c};
            for (unsigned j=0;j<3;++j) {
                uint32_t x=face[j],y=face[(j+1)%3];
                edges.push_back({std::min(x,y),std::max(x,y),uint32_t(x<y)});
            }
        }
        auto end=text.find('\n',start); if (end==tilly::string::npos) break; start=end+1;
    }
    assert(faces==info.triangles && volume>0 && edges.size()%2==0);
    std::sort(edges.begin(),edges.end());
    for (size_t i=0;i<edges.size();i+=2) {
        assert(edges[i][0]==edges[i+1][0] && edges[i][1]==edges[i+1][1] && edges[i][2]!=edges[i+1][2]);
        if (i) assert(edges[i][0]!=edges[i-1][0] || edges[i][1]!=edges[i-1][1]);
    }
    auto euler=int64_t(info.vertices)-int64_t(edges.size()/2)+info.triangles;
    assert(euler==(!std::strcmp(kind,"tube")?0:2));
}
int main() {
    auto *s=jfx_scene3d_create(); assert(s);
    const char *kinds[]={"sphere","cylinder","cone","torus","capsule","pyramid","disk","nurbs","metaball",
        "tube","hemisphere","wedge","tetrahedron","octahedron","icosahedron"};
    tilly::vector<uint8_t> image(96*96*4),other(image.size());
    for (auto kind:kinds) {
        edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,kind);
        jfx_object3d_info_t info={}; info.size=sizeof(info);
        assert(jfx_scene3d_object_info(s,0,&info)==JFX_SUCCESS && info.triangles>0);
        assert(jfx_scene3d_render(s,0,96,96,image.data(),image.size())==JFX_SUCCESS);
        bool visible=false,antialiased=false;
        for (size_t i=0;i<image.size();i+=4) {
            visible|=image[i]!=22 || image[i+1]!=26 || image[i+2]!=34;
            antialiased|=image[i]!=22 && image[i]<30;
        }
        assert(visible); if (!std::strcmp(kind,"sphere")) assert(antialiased && info.vertices>1000);
        auto text=save(s); auto *copy=jfx_scene3d_create(); assert(copy);
        assert(jfx_scene3d_load(copy,text.data(),text.size(),nullptr,0)==JFX_SUCCESS);
        assert(jfx_scene3d_render(copy,0,96,96,other.data(),other.size())==JFX_SUCCESS && image==other);
        jfx_scene3d_destroy(copy);
    }
    for (auto kind:{"tube","hemisphere","wedge","tetrahedron","octahedron","icosahedron"}) {
        for (uint32_t segments:{8u,31u,128u}) {
            edit(s,"3d.new"); edit(s,"3d.add",segments,0,0,0,kind); closed_solid(s,kind);
            auto baseline=save(s);
            assert(jfx_scene3d_command(s,"3d.add",7,0,0,0,kind)==JFX_ERROR_INVALID_ARGUMENT && save(s)==baseline);
            assert(jfx_scene3d_command(s,"3d.add",129,0,0,0,kind)==JFX_ERROR_INVALID_ARGUMENT && save(s)==baseline);
            edit(s,"undo"); assert(jfx_scene3d_object_count(s)==0);
            edit(s,"redo"); assert(save(s)==baseline);
        }
    }
    edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,"nurbs");
    jfx_procedural3d_info_t p={}; p.size=sizeof(p);
    assert(jfx_scene3d_procedural_info(s,0,&p)==JFX_SUCCESS && p.control_count==16);
    float before[3],after[3]; assert(jfx_scene3d_vertex(s,0,500,before)==JFX_SUCCESS);
    edit(s,"3d.nurbs_point",0,5,1,2);
    edit(s,"3d.nurbs_point",0,5,3,3);
    assert(jfx_scene3d_vertex(s,0,500,after)==JFX_SUCCESS && std::fabs(after[1]-before[1])>.01f);
    auto text=save(s);
    assert(jfx_scene3d_command(s,"3d.nurbs_point",0,5,3,0,"")!=JFX_SUCCESS && save(s)==text);
    edit(s,"undo"); edit(s,"redo"); assert(save(s)==text);
    edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,"metaball");
    assert(jfx_scene3d_procedural_info(s,0,&p)==JFX_SUCCESS && p.ball_count==2);
    edit(s,"3d.metaball_add",0,0,0,0,"0 1 0 0.7");
    edit(s,"3d.metaball_point",0,2,0,.4);
    assert(jfx_scene3d_procedural_info(s,0,&p)==JFX_SUCCESS && p.ball_count==3);
    edit(s,"3d.metaball_remove",0,2);
    assert(jfx_scene3d_procedural_info(s,0,&p)==JFX_SUCCESS && p.ball_count==2);
    edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,"cube");
    edit(s,"3d.cloner",0,1,4,2);
    float t[9]; assert(jfx_scene3d_sample_instance(s,0,3,0,t)==JFX_SUCCESS && t[0]==6);
    edit(s,"3d.cloner",0,2,4,3);
    assert(jfx_scene3d_sample_instance(s,0,1,0,t)==JFX_SUCCESS && std::fabs(t[2]-3)<1e-5f);
    edit(s,"3d.cloner",0,3,4,2);
    assert(jfx_scene3d_sample_instance(s,0,3,0,t)==JFX_SUCCESS && t[0]==2 && t[2]==2);
    edit(s,"3d.script",0,1,0,0,"(defkernel bounce [time frame index value] (+ value (* time 2)))");
    assert(jfx_scene3d_sample_instance(s,0,3,.5,t)==JFX_SUCCESS && t[1]==1);
    text=save(s); assert(jfx_scene3d_load(s,text.data(),text.size(),nullptr,0)==JFX_SUCCESS);
    assert(jfx_scene3d_sample_instance(s,0,3,1,t)==JFX_SUCCESS && t[1]==2);
    char source[4097]; assert(jfx_scene3d_script(s,0,1,source,sizeof(source))==JFX_SUCCESS && std::strstr(source,"bounce"));
    assert(jfx_scene3d_script(s,0,9,source,sizeof(source))!=JFX_SUCCESS);
    assert(jfx_scene3d_script(nullptr,0,1,source,sizeof(source))!=JFX_SUCCESS);
    assert(jfx_scene3d_script(s,0,1,nullptr,0)!=JFX_SUCCESS);
    source[0]='!'; assert(jfx_scene3d_script(s,0,1,source,1)!=JFX_SUCCESS && source[0]=='!');
    assert(jfx_scene3d_command(s,"3d.script",0,1,0,0,"(invalid)")!=JFX_SUCCESS && save(s)==text);
    edit(s,"3d.script",0,1,0,0,"(defkernel bad [time frame index value] (/ 1 time))");
    std::fill(image.begin(),image.end(),99); other=image; std::fill(t,t+9,99);
    assert(jfx_scene3d_sample(s,0,0,t)!=JFX_SUCCESS && t[0]==99);
    assert(jfx_scene3d_render(s,0,96,96,image.data(),image.size())!=JFX_SUCCESS && image==other);
    edit(s,"3d.script",0,1,0,0,"");
    edit(s,"3d.cloner_make_real",0); assert(jfx_scene3d_object_count(s)==4);
    edit(s,"undo"); assert(jfx_scene3d_object_count(s)==1);
    edit(s,"3d.orbit",0,0,0,0,"0 180 0");
    float q[4]; assert(jfx_scene3d_camera_quaternion(s,q)==JFX_SUCCESS);
    float norm=0; for (float x:q) norm+=x*x; assert(std::fabs(norm-1)<1e-5f);
    edit(s,"3d.orbit",0,0,0,0,"0 0 90"); edit(s,"3d.pan",0,0,0,0,"0.1 -0.2 0");
    edit(s,"3d.dolly",0,0,0,1);
    assert(jfx_scene3d_render(s,0,96,96,image.data(),image.size())==JFX_SUCCESS);
    text=save(s); assert(jfx_scene3d_load(s,text.data(),text.size(),nullptr,0)==JFX_SUCCESS);
    assert(jfx_scene3d_render(s,0,96,96,other.data(),other.size())==JFX_SUCCESS && image==other);
    jfx_scene3d_clear_history(s); auto baseline=save(s);
    edit(s,"3d.navigation_begin"); edit(s,"3d.orbit",0,0,0,0,"5 10 15"); edit(s,"3d.orbit",0,0,0,0,"5 10 15");
    assert(jfx_scene3d_command(s,"3d.remove",0,0,0,0,"")!=JFX_SUCCESS);
    edit(s,"3d.navigation_end"); assert(jfx_scene3d_can_undo(s)); edit(s,"undo"); assert(save(s)==baseline && !jfx_scene3d_can_undo(s));
    edit(s,"3d.navigation_begin"); edit(s,"3d.orbit",0,0,0,0,"5 10 15"); edit(s,"3d.navigation_cancel"); assert(save(s)==baseline);
    auto *invalid=jfx_scene3d_create(); assert(invalid);
    tilly::string bad=baseline; auto at=bad.find("quaternion "); assert(at!=tilly::string::npos);
    bad.replace(at,bad.find('\n',at)-at,"quaternion 0 0 0 0");
    assert(jfx_scene3d_load(invalid,bad.data(),bad.size(),nullptr,0)!=JFX_SUCCESS);
    assert(jfx_scene3d_object_count(invalid)==0); jfx_scene3d_destroy(invalid);
    assert(jfx_scene3d_camera_quaternion(nullptr,q)!=JFX_SUCCESS);
    assert(jfx_scene3d_camera_quaternion(s,nullptr)!=JFX_SUCCESS);
    assert(jfx_scene3d_procedural_info(nullptr,0,&p)!=JFX_SUCCESS);
    assert(jfx_scene3d_procedural_info(s,0,nullptr)!=JFX_SUCCESS);
    p.size=1; assert(jfx_scene3d_procedural_info(s,0,&p)!=JFX_SUCCESS);
    assert(jfx_scene3d_sample_instance(nullptr,0,0,0,t)!=JFX_SUCCESS);
    assert(jfx_scene3d_sample_instance(s,0,0,0,nullptr)!=JFX_SUCCESS);
    assert(jfx_scene3d_sample_instance(s,0,999,0,t)!=JFX_SUCCESS);
    assert(jfx_scene3d_sample_instance(s,0,0,std::numeric_limits<double>::quiet_NaN(),t)!=JFX_SUCCESS);
    // Cached normals must invalidate on geometry/shading edits, but survive
    // camera-only updates. Compare each warm-cache edit to a fresh load.
    edit(s,"3d.new"); edit(s,"3d.add",0,0,0,0,"sphere");
    for (unsigned step=0;step<4;++step) {
        assert(jfx_scene3d_render(s,0,96,96,image.data(),image.size())==JFX_SUCCESS);
        if (step==0) edit(s,"3d.vertex",0,4,1,.3);
        if (step==1) edit(s,"3d.smooth",0,0,0,0);
        if (step==2) edit(s,"3d.subdivide");
        if (step==3) edit(s,"3d.align");
        auto snapshot=save(s); auto *cold=jfx_scene3d_create(); assert(cold);
        assert(jfx_scene3d_load(cold,snapshot.data(),snapshot.size(),nullptr,0)==JFX_SUCCESS);
        assert(jfx_scene3d_render(s,0,96,96,image.data(),image.size())==JFX_SUCCESS);
        assert(jfx_scene3d_render(cold,0,96,96,other.data(),other.size())==JFX_SUCCESS && image==other);
        jfx_scene3d_destroy(cold);
    }
    jfx_scene3d_destroy(s);
}
