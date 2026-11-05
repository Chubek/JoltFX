#ifndef JFX_EZDESIGN_TOOLS_H
#define JFX_EZDESIGN_TOOLS_H
#include "model.h"

namespace jfx::ez {
struct Ray { Vec origin{}, direction{0,0,-1}; };
struct Camera {
    View view;
    double width=1,height=1;
    glm::dquat orientation() const;
    glm::dmat4 view_matrix() const;
    glm::dmat4 projection() const;
    Ray ray(glm::dvec2 pixel) const;
    bool project(Vec,glm::dvec2 &pixel,double *depth=nullptr) const;
    bool on_plane(glm::dvec2,const Plane &,Vec &) const;
    void orbit(glm::dvec2 delta);
    void pan(glm::dvec2 delta);
    void zoom(double wheel,glm::dvec2 cursor,const Vec *target=nullptr);
    void fit(const Document &,Id active_scope=1);
};
struct Hit {
    Entity entity;
    Loop path;
    Vec position{},normal{};
    double depth=1e100,screen_distance=1e100;
};
struct Bounds { Vec low{1e100},high{-1e100}; };
// Revision-owned triangle bounds hierarchy. Never stores borrowed document
// pointers. Vertex/edge screen-space tests remain scoped to editable geometry.
class PickIndex {
public:
    Mesh mesh;
    void rebuild(Mesh);
    Hit intersect(Ray) const;
private:
    struct Node { Bounds bounds; size_t start=0,count=0; int left=-1,right=-1; };
    tilly::vector<Node> nodes;
    tilly::vector<size_t> order;
    int build(size_t,size_t);
};
enum class SnapKind { Length, Plane, Face, Edge, Axis, Origin, Midpoint, Endpoint };
struct Snap {
    Vec position{};
    SnapKind kind=SnapKind::Plane;
    Id id=0;
    bool valid=false;
    const char *hint() const;
};
class Inference {
public:
    Snap last;
    Snap resolve(const Document &,Id,const PickIndex &,const Camera &,glm::dvec2,
                 const Plane &,const Vec *anchor=nullptr,int axis=0);
};
enum class Tool { Select, Line, Rectangle, Circle, Arc, Pull, Opening, Offset, Measure };
class Controller {
public:
    Editor editor;
    Camera camera;
    Tool tool=Tool::Select;
    Id active_scope=1;
    Entity selected;
    tilly::vector<Id> context; // parent instance IDs, isolated definition editing
    PickIndex index;
    Inference inference;
    Snap snap;
    Hit hover;
    Plane construction;
    Command pending;
    Document candidate;
    Mesh preview_mesh;
    bool started=false,preview_valid=false;
    bool direction_locked=false;
    Vec locked_direction{1,0,0};
    int stage=0,axis=0;
    unsigned segments=32;
    char measurement[128]{},message[256]="Select a tool; ground plane is XZ (metres).";
    uint64_t cached_revision=0;
    uint64_t preview_generation=1;
    bool files_requested=false,wireframe=false;
    char path[512]="design.jfx",export_path[512]="design.obj";
    void refresh();
    void set_tool(Tool);
    void cancel() noexcept;
    void move(glm::dvec2);
    bool click(glm::dvec2);
    bool confirm();
    bool action(const Command &);
    void history(bool redo);
    bool enter();
    void leave();
    const char *expected() const;
    bool has_geometry_preview() const { return preview_valid && tool!=Tool::Measure; }
    glm::dvec2 cursor{};
private:
    Vec anchor{},second{};
    Reference measure_start;
    double drag_start_y=0;
    void update_preview();
};
} // namespace jfx::ez
#endif
