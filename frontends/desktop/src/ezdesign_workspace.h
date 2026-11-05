#ifndef JFX_EZDESIGN_WORKSPACE_H
#define JFX_EZDESIGN_WORKSPACE_H
#include "../ezdesign/tools.h"
#include "host_window.h"
struct jfx_desktop_frontend;

namespace jfx::ez {
struct RasterJob;
class Workspace {
public:
    Controller tools;
    ~Workspace();
    void draw(jfx_desktop_window_t *,const float background[3]);
    void deactivate();
    bool load(const char *) noexcept;
    bool save(const char *) noexcept;
    bool busy() const;
private:
    RasterJob *job=nullptr,*display=nullptr;
    void *texture=nullptr;
    uint64_t render_key=0,desired_key=1;
    View previous_view;
    uint64_t previous_revision=0;
    uint64_t previous_preview_hash=0;
    Id previous_selection=0;
    Id previous_scope=0;
    bool previous_preview=false,gizmo_active=false,local=false;
    int gizmo_operation=0;
    glm::mat4 gizmo_matrix{1};
    Vec gizmo_pivot{};
    glm::vec3 gizmo_origin{};
    unsigned render_width=0,render_height=0;
    double copy_offset[3]={2,0,0};
    int material_index=0;
    float material_color[3]={.65f,.73f,.82f};
    uint64_t material_revision=0;
    bool paint_back=false;
    char ply_path[512]="design.ply";
    float last_background[3]={-1,-1,-1};
    void raster(jfx_desktop_window_t *,const float[3],bool interacting);
    void overlays(float x,float y,float width,float height);
    bool gizmo(float x,float y,float width,float height);
};
// Private integration/test access, not installed or part of the C ABI.
Workspace *workspace(jfx_desktop_frontend *);
} // namespace jfx::ez
#endif
