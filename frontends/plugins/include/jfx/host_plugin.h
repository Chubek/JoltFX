#ifndef JFX_HOST_PLUGIN_H
#define JFX_HOST_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"
#include "jfx/jfx_color.h"
#include "jfx/jfx_editor.h"
#include "jfx/jfx_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JFX_HOST_AFTER_EFFECTS = 0,
    JFX_HOST_PREMIERE,
    JFX_HOST_DAVINCI,
    JFX_HOST_COUNT
} jfx_host_kind_t;

/* Capability bits a host bridge actually implements.
 *
 * A bridge sets a bit only in the same translation unit that implements the
 * capability. `jfx_host_plugin_get_info` currently reports none, because the
 * shipped bridges are host-independent stubs: they are identified and
 * versioned, but no host SDK entry point, import or export is present. */
typedef enum {
    JFX_HOST_PLUGIN_IMPORT = 1u << 0,
    JFX_HOST_PLUGIN_EXPORT = 1u << 1,
    JFX_HOST_PLUGIN_EFFECT = 1u << 2
} jfx_host_plugin_feature_t;

typedef struct {
    size_t size;
    jfx_host_kind_t host;
    const char *host_name;
    const char *plugin_identifier;
    /* Bitmask of jfx_host_plugin_feature_t, honestly reflecting what exists. */
    uint32_t features;
    /* Non-zero when the proprietary host SDK is still required before the
     * bridge can be loaded into its host. */
    uint32_t requires_host_sdk;
} jfx_host_plugin_info_t;

/* Describes the SDK-independent bridge. Host SDK entry points are optional. */
jfx_result_t jfx_host_plugin_get_info(jfx_host_kind_t host,
    jfx_host_plugin_info_t *out_info);

jfx_result_t jfx_ae_plugin_get_info(jfx_host_plugin_info_t *out_info);
jfx_result_t jfx_premiere_plugin_get_info(jfx_host_plugin_info_t *out_info);
jfx_result_t jfx_davinci_plugin_get_info(jfx_host_plugin_info_t *out_info);

/* SDK-independent color panels. A host adapter creates separate parameter groups
 * from kind->category (Color Calibration / Color Grading), and passes host frame
 * buffers through this executor-backed entry point. SDK registration is separate
 * from the bridge capabilities above. Straight float RGBA; output is transactional. */
size_t jfx_host_color_kind_count(jfx_host_kind_t host, jfx_color_section_t section);
const jfx_node_kind_t *jfx_host_color_kind_at(jfx_host_kind_t host,
    jfx_color_section_t section, size_t index);
jfx_result_t jfx_host_color_process(jfx_host_kind_t host, const char *kind,
    const jfx_node_value_t *values, const jfx_lut_t *lut, const float *src,
    size_t width, size_t height, float *out);

/* SDK-independent NLE session for all three hosts. SDK panels bind the same
 * sequence-state JSON and commands as desktop/mobile/web, including color
 * sections. Rendering is RGBA8, project import/export is the shared .jfx form. */
typedef struct jfx_host_nle jfx_host_nle_t;
/* 3D workspace over the same portable host session. Use 3d.* commands, shared
 * load/save/render/export calls, and this state for a host's panel controls. */
typedef jfx_host_nle_t jfx_host_scene3d_t;
jfx_result_t jfx_host_scene3d_create(jfx_host_kind_t host,uint32_t width,uint32_t height,jfx_host_scene3d_t **out_session);
jfx_result_t jfx_host_scene3d_state(jfx_host_scene3d_t *session,char *out_json,size_t capacity);
jfx_result_t jfx_host_nle_create(jfx_host_kind_t host,uint32_t width,uint32_t height,jfx_host_nle_t **out_session);
void jfx_host_nle_destroy(jfx_host_nle_t *session);
jfx_result_t jfx_host_nle_load(jfx_host_nle_t *session,const char *text,size_t length,char *out_error,size_t error_size);
jfx_result_t jfx_host_nle_save(jfx_host_nle_t *session,char *out_text,size_t capacity,size_t *out_written);
jfx_result_t jfx_host_nle_state(jfx_host_nle_t *session,char *out_json,size_t capacity);
jfx_result_t jfx_host_nle_edit(jfx_host_nle_t *session,const char *op,uint32_t a,uint32_t b,uint32_t c,double value,const char *text);
jfx_result_t jfx_host_nle_render(jfx_host_nle_t *session,double seconds,uint32_t width,uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_host_nle_write_frame(jfx_host_nle_t *session,uint64_t frame,uint32_t width,uint32_t height,const char *path);
jfx_result_t jfx_host_nle_export_begin(jfx_host_nle_t *session,const jfx_export_options_t *options,jfx_export_job_t **out_job);
jfx_result_t jfx_host_nle_audio_mixer(jfx_host_nle_t *session,uint32_t rate,jfx_audio_mixer_t **out_mixer);

/* Composition sessions share the editor implementation and command vocabulary
 * with NLE sessions. All three host adapters receive the full typed library.
 * No host SDK is needed to edit, persist, preview or export these graphs. */
typedef jfx_host_nle_t jfx_host_composition_t;
jfx_result_t jfx_host_composition_create(jfx_host_kind_t host,uint32_t width,uint32_t height,jfx_host_composition_t **out_session);
void jfx_host_composition_destroy(jfx_host_composition_t *session);
jfx_result_t jfx_host_composition_load(jfx_host_composition_t *session,const char *text,size_t length,char *out_error,size_t error_size);
jfx_result_t jfx_host_composition_save(jfx_host_composition_t *session,char *out_text,size_t capacity,size_t *out_written);
jfx_result_t jfx_host_composition_state(jfx_host_composition_t *session,char *out_json,size_t capacity);
jfx_result_t jfx_host_composition_edit(jfx_host_composition_t *session,const char *op,uint32_t a,uint32_t b,uint32_t c,double value,const char *text);
jfx_result_t jfx_host_composition_render(jfx_host_composition_t *session,uint32_t node,double seconds,uint32_t width,uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_host_composition_write_frame(jfx_host_composition_t *session,uint32_t node,double seconds,uint32_t width,uint32_t height,const char *path);

#ifdef __cplusplus
}
#endif

#endif /* JFX_HOST_PLUGIN_H */
