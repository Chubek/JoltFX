#include "model.h"
#include "jfx/jfx_modeling3d.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace jfx::ez;
static void edit(Editor &e,const Command &c){
    auto revision=e.revision;assert(e.execute(c));assert(e.revision==revision+1);assert(e.undo());assert(e.redo());validate(e.doc);
}
static Id object(Editor &e,const glm::dmat4 &matrix=glm::dmat4(1),bool component=false){
    Command group;group.op=Op::NewGroup;group.transform=matrix;group.value=component?1:0;edit(e,group);
    return e.doc.scopes[0].instances.back().definition;
}
static Id panel(Editor &e,Id scope_id,Vec a,Vec b,Vec normal,double depth){
    Command c;c.op=Op::Rectangle;c.scope=scope_id;c.points={a,b};c.normal=normal;edit(e,c);
    Id face_id=scope(e.doc,scope_id).faces[0].id;c=Command{};c.op=Op::Extrude;c.scope=scope_id;c.entity=face_id;c.value=depth;edit(e,c);return face_id;
}
int main(int argc,char **argv){
    Editor e;Id floor=object(e);panel(e,floor,{-3,0,3},{3,0,-3},{0,1,0},-.15);
    Command dimension;dimension.op=Op::Dimension;dimension.a={floor,scope(e.doc,floor).vertices[0].id,{}};dimension.b={floor,scope(e.doc,floor).vertices[1].id,{}};edit(e,dimension);
    Id door_scope=0,window_scope=0;
    for(unsigned i=0;i<4;++i){
        auto matrix=glm::rotate(glm::dmat4(1),glm::radians(double(i)*90),Vec(0,1,0))*glm::translate(glm::dmat4(1),Vec(0,0,-3));
        Id wall=object(e,matrix);Id cap=panel(e,wall,{-3,0,0},{3,3,0},{0,0,1},.2);
        if(i<2){Command cut;cut.op=Op::Opening;cut.scope=wall;cut.entity=cap;cut.points=i==0?tilly::vector<Vec>{{-1,0,.2},{0,2.1,.2}}:tilly::vector<Vec>{{-.8,1,.2},{.8,2.2,.2}};edit(e,cut);}
        if(i==0)door_scope=wall;
        if(i==1)window_scope=wall;
        Command paint;paint.op=Op::Paint;paint.scope=wall;paint.entity=cap;paint.material=1;edit(e,paint);
        validate_solid(scope(e.doc,wall));
    }
    Id furniture=object(e,glm::translate(glm::dmat4(1),Vec(-1,0,0)),true);
    Id seat=panel(e,furniture,{0,0,0},{.5,0,-.5},{0,1,0},.5);
    Id first=e.doc.scopes[0].instances.back().id;Command copy;copy.op=Op::Instance;copy.entity=first;copy.points={{2,0,0}};edit(e,copy);
    assert(e.doc.scopes[0].instances.back().definition==furniture);
    Command shared;shared.op=Op::Extrude;shared.scope=furniture;shared.entity=seat;shared.value=.1;edit(e,shared);
    auto mesh=triangulate(e.doc);unsigned changed_instances=0;
    for(auto &instance:e.doc.scopes[0].instances)if(instance.definition==furniture){
        double maximum=-1e100;for(auto &t:mesh.triangles)if(!t.path.empty() && t.path[0]==instance.id)for(Vec p:t.points)maximum=std::max(maximum,p.y);
        assert(std::abs(maximum-.6)<1e-9);++changed_instances;
    }assert(changed_instances==2);
    e.doc.units=1;e.doc.view.target={0,1,0};e.doc.view.distance=14;
    const char *path=argc>1?argv[1]:"ezdesign-room-test.jfx";
    assert(e.save(path));Editor reopened;assert(reopened.load(path));
    assert(reopened.doc.scopes[0].instances.size()==7 && reopened.doc.units==1);
    assert(scope(reopened.doc,door_scope).faces.size()==10 && scope(reopened.doc,window_scope).faces.size()==10);
    assert(face(scope(reopened.doc,window_scope),scope(reopened.doc,window_scope).faces[0].id).front==1);
    Vec a,b;assert(resolve(reopened.doc,reopened.doc.dimensions[0].a,a));assert(resolve(reopened.doc,reopened.doc.dimensions[0].b,b));assert(std::abs(glm::length(b-a)-6)<1e-9);
    tilly::string export_path=tilly::string(path)+".ply";assert(reopened.export_ply(export_path.c_str()));
    auto *reader=jfx_scene3d_create();assert(reader);assert(jfx_scene3d_command(reader,"3d.import_ply",0,0,0,0,export_path.c_str())==JFX_SUCCESS);
    jfx_object3d_info_t info{};info.size=sizeof(info);assert(jfx_scene3d_object_info(reader,0,&info)==JFX_SUCCESS);assert(info.triangles==mesh.triangles.size());jfx_scene3d_destroy(reader);
    if(argc==1){std::remove(path);std::remove(export_path.c_str());}
}
