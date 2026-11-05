#ifndef JFX_EZDESIGN_MODEL_H
#define JFX_EZDESIGN_MODEL_H

#include "tilly/memory.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstdint>

// Private, portable modeling core. No ImGui, renderer or vendor geometry types.
namespace jfx::ez {
using Id = uint64_t;
using Vec = glm::dvec3;
using Loop = tilly::vector<Id>;
struct Tolerances {
    static constexpr double weld = 1e-7;       // metres
    static constexpr double plane = 1e-6;      // metres, measured from local origin
    static constexpr double boolean = 1e-6;    // reserved solid conversion budget
    static constexpr double snap_pixels = 9;  // never used to weld geometry
    static constexpr double coordinate_limit = 1e9;
};
struct Vertex { Id id=0; Vec position{}; };
struct Edge { Id id=0, a=0, b=0; };
struct Face {
    Id id=0;
    Loop outer;
    tilly::vector<Loop> holes;
    uint32_t front=0, back=0;
};
struct Instance {
    Id id=0, definition=0;
    glm::dmat4 transform{1};
    bool component=false;
};
struct Scope {
    Id id=1;
    tilly::vector<Vertex> vertices;
    tilly::vector<Edge> edges;
    tilly::vector<Face> faces;
    tilly::vector<Instance> instances;
};
struct Reference { Id scope=1, vertex=0; Vec point{}; };
struct Dimension { Id id=0; Reference a,b; }; // missing vertex => unresolved, not a dangling pointer
struct Material { glm::dvec3 color{.65,.73,.82}; };
struct View {
    Vec target{};
    double yaw=35, pitch=30, distance=12;
    bool orthographic=false;
};
struct Document {
    Id next=2;
    tilly::vector<Scope> scopes{Scope{}};
    tilly::vector<Dimension> dimensions;
    tilly::vector<Material> materials{Material{},Material{{.65,.4,.25}},Material{{.3,.6,.4}}};
    View view;
    unsigned units=0; // 0 m, 1 mm, 2 ft; model coordinates are always metres
};
struct Plane {
    Vec origin{}, u{1,0,0}, v{0,0,-1}, normal{0,1,0};
    glm::dvec2 project(Vec p) const { p-=origin; return {glm::dot(p,u),glm::dot(p,v)}; }
    Vec point(glm::dvec2 p) const { return origin+u*p.x+v*p.y; }
};
enum class Kind { None, Vertex, Edge, Face, Group, Component };
struct Entity { Kind kind=Kind::None; Id scope=1, id=0; };
struct Change {
    Id before=0;
    tilly::vector<Id> after; // identity, split, merge target, or empty for deletion
};
struct Changes { tilly::vector<Change> entities; tilly::vector<Id> created; };
enum class Op { Line, Rectangle, Polygon, Extrude, Opening, Offset, Delete,
                Group, NewGroup, Instance, Unique, Transform, MoveVertex, Dimension, Paint, Cleanup, New, TransformGeometry,
                AddMaterial, EditMaterial, ReverseFace };
struct Command {
    Op op=Op::Line;
    Id scope=1, entity=0;
    tilly::vector<Vec> points;
    Vec normal{0,1,0};
    double value=0;
    glm::dmat4 transform{1};
    uint32_t material=0;
    Vec color{.65,.73,.82};
    bool back_side=false;
    Vec pivot{}; // TransformGeometry operates relative to this double-precision origin
    Reference a,b;
};
struct Triangle { std::array<Vec,3> points; Id face=0, scope=1; uint32_t material=0,back_material=0; Loop path; };
struct Mesh { tilly::vector<Triangle> triangles; };
const Scope &scope(const Document &, Id);
Scope &scope(Document &, Id);
const Vertex &vertex(const Scope &, Id);
const Face &face(const Scope &, Id);
Plane plane(const Scope &, const Face &);
Loop entity_vertices(const Scope &, Id); // vertex, edge endpoints, or all face loops
void validate(const Document &); // throws actionable std::invalid_argument; ordinary open geometry allowed
void validate_solid(const Scope &); // closure, opposite incidence, positive signed volume
Mesh triangulate(const Document &, Id active_scope=1);
double area(const Scope &, const Face &);
bool resolve(const Document &, const Reference &, Vec &);
bool parse_length(const char *, unsigned units, double &metres) noexcept;

// Every operation edits a candidate. Publish and bounded history happen only
// after topology validation and change-map allocation have succeeded.
class Editor {
public:
    Document doc;
    Changes changes;
    char error[256]{};
    uint64_t revision=1;
    bool execute(const Command &) noexcept;
    bool undo() noexcept;
    bool redo() noexcept;
    bool can_undo() const { return !past.empty(); }
    bool can_redo() const { return !future.empty(); }
    bool preview(const Command &, Document &out) noexcept;
    bool save(const char *) noexcept;
    bool load(const char *) noexcept;
    bool export_obj(const char *, Id active_scope=1) noexcept;
    bool export_ply(const char *, Id active_scope=1) noexcept;
private:
    tilly::vector<Document> past, future;
    Id high_water=2; // never reuse an ID after undo or a branch
    bool publish(Document &&) noexcept;
};
} // namespace jfx::ez
#endif
