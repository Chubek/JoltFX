#ifndef JFX_ANIMATION_H
#define JFX_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "jfx_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JFX_ANIMATION_API_MAJOR 1
#define JFX_ANIMATION_API_MINOR 0
#define JFX_ANIMATION_BYTECODE_VERSION 1u

typedef struct jfx_animation_scene jfx_animation_scene_t;
typedef struct jfx_animation_tab jfx_animation_tab_t;

typedef struct { float x, y; } jfx_animation_vec2_t;
typedef struct {
    jfx_animation_vec2_t translation;
    float rotation;
    jfx_animation_vec2_t scale;
} jfx_animation_transform_t;

typedef struct {
    size_t size;
    const char *name;
    uint32_t parent; /* UINT32_MAX for a root */
    jfx_animation_transform_t transform;
} jfx_animation_bone_desc_t;

typedef enum {
    JFX_ANIMATION_CURVE_STEP = 0,
    JFX_ANIMATION_CURVE_LINEAR = 1,
    JFX_ANIMATION_CURVE_SMOOTH = 2
} jfx_animation_curve_t;

typedef struct {
    size_t size;
    uint32_t bone;
    uint8_t property; /* 0=x, 1=y, 2=rotation, 3=scale-x, 4=scale-y */
    double time;
    float value;
    jfx_animation_curve_t curve;
} jfx_animation_key_t;

typedef struct {
    size_t size;
    uint32_t bone_count;
    jfx_animation_transform_t *world_transforms;
} jfx_animation_pose_t;

typedef struct {
    size_t size;
    uint32_t bone_count;
    uint32_t key_count;
    double duration;
} jfx_animation_scene_info_t;

jfx_animation_scene_t *jfx_animation_scene_create(void);
void jfx_animation_scene_destroy(jfx_animation_scene_t *scene);
jfx_result_t jfx_animation_scene_add_bone(jfx_animation_scene_t *, const jfx_animation_bone_desc_t *, uint32_t *out_bone);
jfx_result_t jfx_animation_scene_add_key(jfx_animation_scene_t *, const jfx_animation_key_t *);
jfx_result_t jfx_animation_scene_info(const jfx_animation_scene_t *, jfx_animation_scene_info_t *out_info);
jfx_result_t jfx_animation_scene_evaluate(const jfx_animation_scene_t *, double time, jfx_animation_pose_t *out_pose);

/* Deterministic JFA1 bytecode. The returned buffer is owned by the caller and
 * must be released with jfx_animation_bytes_destroy. */
jfx_result_t jfx_animation_compile(const jfx_animation_scene_t *, uint8_t **out_bytes, size_t *out_size);
jfx_result_t jfx_animation_validate(const uint8_t *bytes, size_t size);
jfx_result_t jfx_animation_play(const uint8_t *bytes, size_t size, double time, jfx_animation_pose_t *out_pose);
void jfx_animation_bytes_destroy(uint8_t *bytes);

/* Editor tab state is renderer-neutral. Native frontends can bind its clock to
 * SDL, while the web frontend can drive it from requestAnimationFrame. */
jfx_animation_tab_t *jfx_animation_tab_create(uint32_t width, uint32_t height);
void jfx_animation_tab_destroy(jfx_animation_tab_t *tab);
jfx_animation_scene_t *jfx_animation_tab_scene(jfx_animation_tab_t *tab);
jfx_result_t jfx_animation_tab_set_time(jfx_animation_tab_t *, double time);
double jfx_animation_tab_time(const jfx_animation_tab_t *);
jfx_result_t jfx_animation_tab_tick(jfx_animation_tab_t *, double elapsed_seconds);
jfx_result_t jfx_animation_tab_set_playing(jfx_animation_tab_t *, bool playing);
bool jfx_animation_tab_playing(const jfx_animation_tab_t *);
jfx_result_t jfx_animation_tab_pose(const jfx_animation_tab_t *, jfx_animation_pose_t *out_pose);

#ifdef __cplusplus
}
#endif
#endif
