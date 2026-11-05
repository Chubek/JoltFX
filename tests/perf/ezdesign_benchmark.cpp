#include "tools.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace jfx::ez;
using Clock=std::chrono::steady_clock;
static double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static void require(bool value){if(!value){std::fprintf(stderr,"EZDesign benchmark setup failed\n");std::exit(1);}}
static Document scene(unsigned count){
    Editor e;Command c;c.op=Op::Rectangle;c.normal={0,0,1};c.points={{-3,0,0},{3,3,0}};require(e.execute(c));
    c=Command{};c.op=Op::Extrude;c.entity=e.doc.scopes[0].faces[0].id;c.value=.2;require(e.execute(c));
    c=Command{};c.op=Op::Opening;c.entity=e.doc.scopes[0].faces[0].id;c.points={{-1,1,.2},{1,2.5,.2}};require(e.execute(c));
    c=Command{};c.op=Op::Group;c.value=1;require(e.execute(c));Instance prototype=e.doc.scopes[0].instances[0];e.doc.scopes[0].instances.clear();
    for(unsigned i=0;i<count;++i){auto instance=prototype;instance.id=e.doc.next++;
        if(count==4)instance.transform=glm::rotate(glm::dmat4(1),glm::radians(double(i)*90),Vec(0,1,0))*glm::translate(glm::dmat4(1),Vec(0,0,-3));
        else instance.transform=glm::translate(glm::dmat4(1),Vec(double(i%20)*7,0,double(i/20)*7));
        e.doc.scopes[0].instances.push_back(instance);
    }validate(e.doc);return std::move(e.doc);
}
int main(){
    for(unsigned count:{4u,200u}){
        Editor source;source.doc=scene(count);require(source.save("ezdesign-benchmark.jfx"));Editor loaded;
        auto start=Clock::now();require(loaded.load("ezdesign-benchmark.jfx"));double load=elapsed(start);
        start=Clock::now();auto mesh=triangulate(loaded.doc);double rebuild=elapsed(start);size_t triangles=mesh.triangles.size();
        PickIndex index;start=Clock::now();index.rebuild(std::move(mesh));double bounds=elapsed(start);
        Camera camera;camera.width=1280;camera.height=800;camera.fit(loaded.doc);start=Clock::now();
        size_t hits=0;for(int i=0;i<2000;++i){auto h=index.intersect(camera.ray({double(i%100)*12.8,double(i%80)*10}));hits+=h.entity.id!=0;}double pick=elapsed(start)/2000;
        Inference inference;Plane plane;start=Clock::now();for(int i=0;i<500;++i)inference.resolve(loaded.doc,1,index,camera,{double(i%100)*12.8,double(i%80)*10},plane);double snap=elapsed(start)/500;
        Command edit;edit.op=Op::Extrude;edit.scope=loaded.doc.scopes[0].instances[0].definition;edit.entity=scope(loaded.doc,edit.scope).faces[0].id;edit.value=.01;
        Document preview;start=Clock::now();require(loaded.preview(edit,preview));auto preview_mesh=triangulate(preview);double drag=elapsed(start);
        std::printf("instances=%u triangles=%zu load=%.3f ms mesh=%.3f ms BVH=%.3f ms pick=%.4f ms snap=%.4f ms definition-preview+mesh=%.3f ms hits=%zu\n",count,triangles,load,rebuild,bounds,pick,snap,drag,hits);
    }
    std::remove("ezdesign-benchmark.jfx");
}
