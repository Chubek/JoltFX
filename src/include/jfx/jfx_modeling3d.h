#ifndef JFX_MODELING3D_H
#define JFX_MODELING3D_H
#include "jfx_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_MODELING3D_API_MAJOR 1
#define JFX_MODELING3D_API_MINOR 2
#define JFX_3D_MAX_OBJECTS 64u
#define JFX_3D_MAX_VERTICES 65536u
#define JFX_3D_MAX_TRIANGLES 131072u
#define JFX_3D_MAX_KEYS 4096u
typedef struct jfx_scene3d jfx_scene3d_t;
typedef struct {
    size_t size;
    char name[128];
    uint32_t vertices,triangles,keys;
    float transform[9],color[3],mass;
    bool visible;
} jfx_object3d_info_t;
/* Additive procedural inspector; existing object-info layout is unchanged.
 * generator: 0 mesh, 1 bicubic NURBS patch, 2 metaball isosurface.
 * cloner_mode: 0 off, 1 linear, 2 radial, 3 XZ grid. XYZW controls use W
 * as rational weight for NURBS or radius for metaballs. */
typedef struct {
    size_t size;
    uint32_t generator, resolution, control_count, ball_count;
    float controls[16][4], balls[16][4];
    uint32_t cloner_mode, instances;
    float spacing;
    bool smooth;
} jfx_procedural3d_info_t;
jfx_result_t jfx_scene3d_procedural_info(const jfx_scene3d_t *scene,uint32_t object,
    jfx_procedural3d_info_t *out_info);
jfx_result_t jfx_scene3d_script(const jfx_scene3d_t *scene,uint32_t object,uint32_t channel,
    char *out_source,size_t capacity);
uint32_t jfx_scene3d_object_count(const jfx_scene3d_t *scene);
uint32_t jfx_scene3d_fps(const jfx_scene3d_t *scene);
uint32_t jfx_scene3d_frames(const jfx_scene3d_t *scene);
jfx_result_t jfx_scene3d_object_info(const jfx_scene3d_t *scene,uint32_t object,jfx_object3d_info_t *out_info);
jfx_result_t jfx_scene3d_vertex(const jfx_scene3d_t *scene,uint32_t object,uint32_t vertex,float out_xyz[3]);
jfx_result_t jfx_scene3d_camera(const jfx_scene3d_t *scene,float out_camera[7]);
/* Normalized camera-local-to-world quaternion in XYZW order. */
jfx_result_t jfx_scene3d_camera_quaternion(const jfx_scene3d_t *scene,float out_xyzw[4]);
/* Single-owner-thread scene. Objects use zero-based indices. The editor owns a
 * scene; standalone clients may own one too. All C boundaries catch exceptions. */
jfx_scene3d_t *jfx_scene3d_create(void);
void jfx_scene3d_destroy(jfx_scene3d_t *scene);
/* Edits are failure-atomic and have bounded 32-step history. API 1.2 adds tube,
 * hemisphere, wedge, tetrahedron, octahedron and icosahedron to 3d.add.
 * See docs/modeling3d.md for the complete command/primitive catalog. */
jfx_result_t jfx_scene3d_command(jfx_scene3d_t *scene,const char *op,
    uint32_t a,uint32_t b,uint32_t c,double value,const char *text);
bool jfx_scene3d_can_undo(const jfx_scene3d_t *scene);
bool jfx_scene3d_can_redo(const jfx_scene3d_t *scene);
void jfx_scene3d_clear_history(jfx_scene3d_t *scene);
/* Native scene format starts with `scene3d 1`. Meshes are embedded, not linked.
 * Loading validates the complete document before replacement; clears history. */
jfx_result_t jfx_scene3d_load(jfx_scene3d_t *scene,const char *text,size_t length,
    char *out_error,size_t error_size);
jfx_result_t jfx_scene3d_save(const jfx_scene3d_t *scene,char *out_text,size_t capacity,size_t *out_written);
jfx_result_t jfx_scene3d_state(const jfx_scene3d_t *scene,char *out_json,size_t capacity);
/* Transform/animation channels: position XYZ, Euler degrees XYZ, scale XYZ.
 * Interpolation: 0 hold, 1 linear, 2 smoothstep. Values are sampled at frame/fps. */
jfx_result_t jfx_scene3d_sample(const jfx_scene3d_t *scene,uint32_t object,double seconds,
    float out_transform[9]);
jfx_result_t jfx_scene3d_sample_instance(const jfx_scene3d_t *scene,uint32_t object,
    uint32_t instance,double seconds,float out_transform[9]);
/* CPU z-buffered 4-sample antialiased perspective viewport, smooth/flat normals
 * and near-plane clipping. Output remains untouched on error. Max dimension 2048. */
jfx_result_t jfx_scene3d_render(const jfx_scene3d_t *scene,double seconds,uint32_t width,
    uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_scene3d_write_png(const jfx_scene3d_t *scene,double seconds,
    uint32_t width,uint32_t height,const char *path);
#ifdef __cplusplus
}
#endif
#endif
