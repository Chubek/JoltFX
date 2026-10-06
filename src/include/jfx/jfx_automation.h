#ifndef JFX_AUTOMATION_H
#define JFX_AUTOMATION_H
#include "jfx_vst3.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_AUTOMATION_API_MAJOR 1
#define JFX_AUTOMATION_API_MINOR 0
#define JFX_AUDIO_MAX_AUTOMATION_LANES 64u
#define JFX_AUDIO_MAX_AUTOMATION_KEYS 128u
typedef enum { JFX_AUTOMATION_TRACK_GAIN,JFX_AUTOMATION_PLUGIN_PARAMETER } jfx_audio_automation_target_t;
typedef struct { uint64_t frame; double value; } jfx_audio_automation_key_t;
typedef struct {
    size_t size;
    jfx_audio_automation_target_t target;
    uint32_t insert,parameter;
    jfx_interp_t interpolation;
    uint32_t key_count;
    jfx_audio_automation_key_t keys[JFX_AUDIO_MAX_AUTOMATION_KEYS];
} jfx_audio_automation_lane_t;
size_t jfx_timeline_audio_automation_count(const jfx_timeline_t *timeline,uint32_t track);
jfx_result_t jfx_timeline_get_audio_automation(const jfx_timeline_t *timeline,uint32_t track,uint32_t lane,jfx_audio_automation_lane_t *out_lane);
/* Replaces a lane identified by target/insert/parameter; zero keys removes it.
 * Keys are strictly increasing sequence frames. Plugin values are normalized;
 * track gain values are linear [0,16]. */
jfx_result_t jfx_timeline_set_audio_automation(jfx_timeline_t *timeline,uint32_t track,const jfx_audio_automation_lane_t *lane);
#ifdef __cplusplus
}
#endif
#endif
