#include "model.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <limits>
using namespace jfx::ez;
static Command rectangle(Vec a={0,0,0},Vec b={4,0,-3}) {
    Command c; c.op=Op::Rectangle;c.points={a,b};return c;
}
static void extrude(Editor &e,double depth) {
    Command c;c.op=Op::Extrude;c.entity=e.doc.scopes[0].faces[0].id;c.value=depth;assert(e.execute(c));
}
int main() {
    // Face transforms include holes and retain shared topology and endpoint IDs.
    {
    Editor moved;assert(moved.execute(rectangle()));extrude(moved,2);
    Command move;move.op=Op::TransformGeometry;move.entity=moved.doc.scopes[0].faces[0].id;
    move.pivot={1e7,0,0};move.transform[3][1]=1;
    auto before=moved.doc;auto rev=moved.revision;Document preview;
    assert(moved.preview(move,preview));assert(moved.revision==rev);
    assert(moved.execute(move));validate_solid(moved.doc.scopes[0]);
    for(Id id:face(before.scopes[0],move.entity).outer)
        assert(glm::length(vertex(moved.doc.scopes[0],id).position-vertex(before.scopes[0],id).position-Vec(0,1,0))<1e-9);
    assert(moved.undo());assert(moved.redo());
    auto stable=moved.revision;move.transform[0][0]=0;
    assert(!moved.execute(move));assert(moved.revision==stable);
    move.transform=glm::dmat4(1);move.transform[3][1]=1;
    move.entity=moved.doc.scopes[0].vertices[0].id; // warps a quad
    assert(!moved.execute(move));assert(moved.revision==stable);
    Editor wire;Command segment;segment.points={{1e7,0,0},{1e7+2,0,0}};assert(wire.execute(segment));
    move.entity=wire.doc.scopes[0].edges[0].id;move.pivot={1e7,0,0};
    move.transform=glm::dmat4(1);move.transform[0][0]=2;move.transform[3][2]=.001;
    assert(wire.execute(move));assert(std::abs(wire.doc.scopes[0].vertices[1].position.x-(1e7+4))<1e-9);
    assert(std::abs(wire.doc.scopes[0].vertices[0].position.z-.001)<1e-12);
    move.entity=wire.doc.scopes[0].vertices[0].id;
    move.transform=glm::dmat4(1);move.transform[3][1]=2;assert(wire.execute(move));
    assert(wire.doc.scopes[0].vertices[0].position.y==2);
    assert(wire.doc.scopes[0].vertices[1].position.y==0);
    Editor ring;assert(ring.execute(rectangle()));assert(ring.execute(rectangle({1,0,-1},{2,0,-2})));
    move.entity=ring.doc.scopes[0].faces[0].id;move.pivot={0,0,0};
    move.transform=glm::dmat4(1);move.transform[0]={0,2,0,0};move.transform[1]={-2,0,0,0};move.transform[2]={0,0,2,0};
    double old_area=area(ring.doc.scopes[0],ring.doc.scopes[0].faces[0]);
    assert(ring.execute(move));assert(ring.doc.scopes[0].faces[0].holes.size()==1);
    assert(std::abs(area(ring.doc.scopes[0],ring.doc.scopes[0].faces[0])-4*old_area)<1e-9);
    validate(ring.doc);
    }
    Editor e;
    // Chained line closure and a dividing line create two regions, not diagonals.
    Command line;line.points={{0,0,0},{4,0,0}};assert(e.execute(line));
    line.points={{4,0,0},{4,0,-3}};assert(e.execute(line));
    line.points={{4,0,-3},{0,0,-3}};assert(e.execute(line));
    line.points={{0,0,-3},{0,0,0}};assert(e.execute(line));
    assert(e.doc.scopes[0].faces.size()==1);assert(std::abs(area(e.doc.scopes[0],e.doc.scopes[0].faces[0])-12)<1e-9);
    line.points={{2,0,0},{2,0,-3}};assert(e.execute(line));
    assert(e.doc.scopes[0].faces.size()==2);assert(e.doc.scopes[0].vertices.size()==6);
    bool split_reported=false;for(auto &change:e.changes.entities)split_reported=split_reported || change.after.size()==2;assert(split_reported);
    validate(e.doc);assert(e.undo());assert(e.doc.scopes[0].faces.size()==1);assert(e.redo());
    // Signed standalone extrusion; exact closed and coherently oriented shells.
    for(double depth:{2.0,-2.0}) {
        Editor block;assert(block.execute(rectangle()));extrude(block,depth);
        validate_solid(block.doc.scopes[0]);assert(block.doc.scopes[0].faces.size()==6);
        auto mesh=triangulate(block.doc);assert(mesh.triangles.size()==12);
        auto revision=block.revision;Command bad;bad.op=Op::Extrude;bad.entity=block.doc.scopes[0].faces[0].id;bad.value=0;
        assert(!block.execute(bad));assert(block.revision==revision);
    }
    // Boundary-contact door: planar Clipper2 difference plus prism rebuilding.
    Editor door;Command panel;panel.op=Op::Rectangle;panel.normal={0,0,1};panel.points={{0,0,0},{4,3,0}};assert(door.execute(panel));extrude(door,.2);
    Command notch;notch.op=Op::Opening;notch.entity=door.doc.scopes[0].faces[0].id;notch.points={{1,0,.2},{2,2.1,.2}};
    assert(door.execute(notch));validate_solid(door.doc.scopes[0]);assert(door.doc.scopes[0].faces.size()==10);
    // Through opening in a prism adds two inner loops and four tunnel faces.
    Editor wall;assert(wall.execute(rectangle()));extrude(wall,.3);
    Command hole;hole.op=Op::Opening;hole.entity=wall.doc.scopes[0].faces[0].id;
    hole.points={{1,.3,-1},{2,.3,-2}};assert(wall.execute(hole));
    validate_solid(wall.doc.scopes[0]);assert(wall.doc.scopes[0].faces.size()==10);
    assert(triangulate(wall.doc).triangles.size()==32);
    assert(wall.undo());assert(wall.doc.scopes[0].faces.size()==6);assert(wall.redo());
    Command outside=hole;outside.points={{-1,.3,-1},{2,.3,-2}};
    auto revision=wall.revision;assert(!wall.execute(outside));assert(wall.revision==revision);
    Command branch;branch.op=Op::Line;branch.points={{0,.3,-1},{.2,.3,-1}};
    assert(wall.execute(branch));assert(wall.doc.scopes[0].faces.size()==10);assert(wall.undo());
    // Offset collapse is rejected without altering history or topology.
    Editor offset;assert(offset.execute(rectangle()));Command inset;inset.op=Op::Offset;
    inset.entity=offset.doc.scopes[0].faces[0].id;inset.value=-100;assert(!offset.execute(inset));
    inset.value=-.2;assert(offset.execute(inset));validate(offset.doc);
    Editor holed;assert(holed.execute(rectangle()));assert(holed.execute(rectangle({1,0,-1},{2,0,-2})));
    assert(holed.doc.scopes[0].faces.size()==2);Command erase_inner;erase_inner.op=Op::Delete;erase_inner.entity=holed.doc.scopes[0].faces.back().id;assert(holed.execute(erase_inner));
    inset.entity=holed.doc.scopes[0].faces[0].id;assert(holed.execute(inset));assert(holed.doc.scopes[0].faces[0].holes.size()==1);assert(std::abs(area(holed.doc.scopes[0],holed.doc.scopes[0].faces[0])-7.4)<1e-8);
    Command cleanup;cleanup.op=Op::Cleanup;assert(holed.execute(cleanup));extrude(holed,-.3);validate_solid(holed.doc.scopes[0]);
    Editor multiple;Command dumbbell;dumbbell.op=Op::Polygon;
    for(auto p:{glm::dvec2(0,0),glm::dvec2(2,0),glm::dvec2(2,.9),glm::dvec2(4,.9),glm::dvec2(4,0),glm::dvec2(6,0),glm::dvec2(6,2),glm::dvec2(4,2),glm::dvec2(4,1.1),glm::dvec2(2,1.1),glm::dvec2(2,2),glm::dvec2(0,2)})dumbbell.points.push_back({p.x,0,-p.y});
    assert(multiple.execute(dumbbell));inset.entity=multiple.doc.scopes[0].faces[0].id;assert(multiple.execute(inset));assert(multiple.doc.scopes[0].faces.size()==2);
    // Components share the definition; make unique detaches without ID aliasing.
    Command group;group.op=Op::Group;assert(wall.execute(group));
    Id instance=wall.doc.scopes[0].instances[0].id;
    Id definition=wall.doc.scopes[0].instances[0].definition;
    Command copy;copy.op=Op::Instance;copy.entity=instance;copy.points={{5,0,0}};assert(wall.execute(copy));
    assert(wall.doc.scopes[0].instances[1].definition==definition);
    Command paint;paint.op=Op::Paint;paint.scope=definition;paint.entity=scope(wall.doc,definition).faces[0].id;paint.material=1;
    assert(wall.execute(paint));assert(triangulate(wall.doc).triangles[0].material==1);
    Command unique;unique.op=Op::Unique;unique.entity=instance;assert(wall.execute(unique));
    assert(wall.doc.scopes[0].instances[0].definition!=definition);validate(wall.doc);
    // Vertex-attached dimensions become unresolved when targets disappear.
    {
        Editor palette;assert(palette.execute(rectangle()));
        assert(palette.execute(rectangle({1,0,-1},{2,0,-2})));
        Id face_id=palette.doc.scopes[0].faces[0].id;
        Command material;material.op=Op::AddMaterial;material.color={.125,.25,.75};
        assert(palette.execute(material));assert(palette.doc.materials.size()==4);
        Command paint_back;paint_back.op=Op::Paint;paint_back.entity=face_id;
        paint_back.material=3;paint_back.back_side=true;assert(palette.execute(paint_back));
        assert(face(palette.doc.scopes[0],face_id).front==0);
        assert(face(palette.doc.scopes[0],face_id).back==3);
        auto old_face=face(palette.doc.scopes[0],face_id);auto normal=plane(palette.doc.scopes[0],old_face).normal;
        Command reverse;reverse.op=Op::ReverseFace;reverse.entity=face_id;assert(palette.execute(reverse));
        const auto &flipped=face(palette.doc.scopes[0],face_id);
        assert(flipped.front==3 && flipped.back==0 && flipped.holes.size()==1);
        assert(glm::dot(normal,plane(palette.doc.scopes[0],flipped).normal)<-.999);
        assert(flipped.outer.front()==old_face.outer.back());
        assert(flipped.holes[0].front()==old_face.holes[0].back());
        bool found_flipped=false;
        for(auto &triangle:triangulate(palette.doc).triangles)if(triangle.face==face_id){
            found_flipped=true;assert(triangle.material==3 && triangle.back_material==0);
            assert(glm::dot(glm::cross(triangle.points[1]-triangle.points[0],triangle.points[2]-triangle.points[0]),normal)<0);
        }
        assert(found_flipped);
        assert(palette.undo());assert(face(palette.doc.scopes[0],face_id).outer==old_face.outer);
        assert(palette.redo());
        material.op=Op::EditMaterial;material.material=3;material.color={.9,.2,.4};
        assert(palette.execute(material));assert(palette.doc.materials[3].color==material.color);
        assert(palette.undo());assert(palette.doc.materials[3].color==Vec(.125,.25,.75));assert(palette.redo());
        auto unchanged=palette.revision;material.color.x=-1;assert(!palette.execute(material));
        material.color.x=std::numeric_limits<double>::quiet_NaN();assert(!palette.execute(material));
        material.color={1,0,0};material.material=99;assert(!palette.execute(material));assert(palette.revision==unchanged);
        assert(palette.save("ezdesign-material-test.jfx"));Editor restored;assert(restored.load("ezdesign-material-test.jfx"));
        assert(restored.doc.materials[3].color==Vec(.9,.2,.4));
        assert(face(restored.doc.scopes[0],face_id).front==3 && face(restored.doc.scopes[0],face_id).back==0);
        std::remove("ezdesign-material-test.jfx");
        material.op=Op::AddMaterial;
        while(palette.doc.materials.size()<32)assert(palette.execute(material));
        unchanged=palette.revision;assert(!palette.execute(material));assert(palette.revision==unchanged);
        assert(palette.undo());assert(palette.doc.materials.size()==31);assert(palette.redo());
    }
    Editor dimensions;assert(dimensions.execute(rectangle()));Command dim;dim.op=Op::Dimension;
    dim.a={1,dimensions.doc.scopes[0].vertices[0].id,{}};dim.b={1,dimensions.doc.scopes[0].vertices[1].id,{}};
    assert(dimensions.execute(dim));Vec p;assert(resolve(dimensions.doc,dim.a,p));
    Command remove;remove.op=Op::Delete;remove.entity=dim.a.vertex;assert(dimensions.execute(remove));assert(!resolve(dimensions.doc,dim.a,p));assert(dimensions.undo());
    assert(dimensions.execute(group));assert(dimensions.doc.dimensions[0].a.scope==dimensions.doc.scopes[0].instances[0].definition);
    assert(resolve(dimensions.doc,dimensions.doc.dimensions[0].a,p));
    // Strong malformed-input validation, including cyclic definition references.
    Document cyclic=wall.doc;auto &definition_scope=scope(cyclic,cyclic.scopes[0].instances[0].definition);
    definition_scope.instances.push_back({cyclic.next++,1,glm::dmat4(1),true});bool rejected=false;
    try{validate(cyclic);}catch(...){rejected=true;}assert(rejected);
    // A failed move cannot leave a nonplanar polygon in the authoritative model.
    Editor moved;assert(moved.execute(rectangle()));Command move_vertex;move_vertex.op=Op::MoveVertex;move_vertex.entity=moved.doc.scopes[0].vertices[0].id;move_vertex.points={{0,.1,0}};
    revision=moved.revision;assert(!moved.execute(move_vertex));assert(moved.revision==revision);
    // Precision at furniture/building scales far from origin and save/load.
    for(double size:{.01,100.0}) {
        Editor far;assert(far.execute(rectangle({1e7,0,1e7},{1e7+size,0,1e7-size})));
        assert(std::abs(area(far.doc.scopes[0],far.doc.scopes[0].faces[0])-size*size)<size*1e-7);
        assert(far.save("ezdesign-test.jfx"));Editor loaded;assert(loaded.load("ezdesign-test.jfx"));
        assert(loaded.doc.scopes[0].vertices[1].position==far.doc.scopes[0].vertices[1].position);
    }
    assert(wall.save("ezdesign-test.jfx"));Editor loaded;assert(loaded.load("ezdesign-test.jfx"));
    assert(loaded.doc.scopes.size()==wall.doc.scopes.size());assert(loaded.export_obj("ezdesign-test.obj"));
    FILE *bad=std::fopen("ezdesign-bad.jfx","wb");assert(bad);std::fputs("ezdesign 999\n",bad);std::fclose(bad);
    revision=loaded.revision;assert(!loaded.load("ezdesign-bad.jfx"));assert(revision==loaded.revision);
    double length=0;assert(parse_length("1200 mm",0,length) && length==1.2);assert(parse_length("-2 ft",0,length));
    assert(!parse_length("nan",0,length));assert(!parse_length("2 garbage",0,length));
    std::remove("ezdesign-test.jfx");std::remove("ezdesign-test.obj");std::remove("ezdesign-bad.jfx");
}
