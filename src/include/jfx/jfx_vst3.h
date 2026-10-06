#ifndef JFX_VST3_H
#define JFX_VST3_H
#include "jfx_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_VST3_API_MAJOR 1
#define JFX_VST3_API_MINOR 1
#define JFX_VST3_MAX_STATE_BYTES (2u * 1024u * 1024u + 12u)
#define JFX_VST3_MAX_EVENTS 2048u
#define JFX_VST3_MAX_PARAM_QUEUES 128u
#define JFX_AUDIO_MAX_INSERTS 8u
#define JFX_AUDIO_MAX_PLUGIN_PARAMS 64u
typedef struct {
    uint32_t id;
    double value;
} jfx_audio_plugin_value_t;
/* Portable rack descriptor. Loading a project does not load native code.
 * Overrides are normalized [0,1]; opaque state is stored through the separate
 * state APIs below. CID is the factory class UID, as 32 hexadecimal characters. */
typedef struct {
    size_t size;
    char path[JFX_NODE_PATH_MAX];
    char cid[33];
    bool enabled;
    uint32_t parameter_count;
    jfx_audio_plugin_value_t parameters[JFX_AUDIO_MAX_PLUGIN_PARAMS];
} jfx_audio_insert_t;
size_t jfx_timeline_audio_insert_count(const jfx_timeline_t *timeline, uint32_t track);
jfx_result_t jfx_timeline_get_audio_insert(const jfx_timeline_t *timeline, uint32_t track,
                                           uint32_t insert, jfx_audio_insert_t *out_insert);
jfx_result_t jfx_timeline_add_audio_insert(jfx_timeline_t *timeline, uint32_t track,
                                           const jfx_audio_insert_t *insert);
jfx_result_t jfx_timeline_remove_audio_insert(jfx_timeline_t *timeline, uint32_t track,
                                              uint32_t insert);
jfx_result_t jfx_timeline_move_audio_insert(jfx_timeline_t *timeline, uint32_t track,
                                            uint32_t insert, uint32_t to);
jfx_result_t jfx_timeline_enable_audio_insert(jfx_timeline_t *timeline, uint32_t track,
                                              uint32_t insert, bool enabled);
jfx_result_t jfx_timeline_audio_insert_parameter(jfx_timeline_t *timeline, uint32_t track,
                                                 uint32_t insert, uint32_t parameter_id,
                                                 double normalized);
jfx_result_t jfx_timeline_clear_audio_insert_parameters(jfx_timeline_t *timeline,uint32_t track,uint32_t insert);
jfx_result_t jfx_timeline_set_master_audio_gain(jfx_timeline_t *timeline, float gain);
float jfx_timeline_master_audio_gain(const jfx_timeline_t *timeline);
jfx_result_t jfx_timeline_set_audio_tempo(jfx_timeline_t *timeline, double bpm);
double jfx_timeline_audio_tempo(const jfx_timeline_t *timeline);
/* Opaque JVS1 component/controller chunks. Borrowed bytes are invalidated by
 * model edits. A zero-byte state clears it. Parsing never loads native code. */
jfx_result_t jfx_timeline_audio_insert_state(jfx_timeline_t *timeline,uint32_t track,
    uint32_t insert,const void *state,size_t bytes);
jfx_result_t jfx_timeline_get_audio_insert_state(const jfx_timeline_t *timeline,
    uint32_t track,uint32_t insert,const void **out_state,size_t *out_bytes);
jfx_result_t jfx_vst3_validate_state(const void *state,size_t bytes);
typedef struct {
    size_t size;
    char cid[33], name[128];
} jfx_vst3_class_t;
typedef struct {
    size_t size;
    uint32_t id;
    char name[128], units[128];
    double value;
    int32_t steps;
    bool read_only;
} jfx_vst3_parameter_t;
typedef struct jfx_vst3_instance jfx_vst3_instance_t;
bool jfx_vst3_available(void);
/* A .vst3 bundle or a native module. Enumerates audio-effect factory classes;
 * *out_count reports the required capacity. No background threads are created. */
jfx_result_t jfx_vst3_classes(const char *path, jfx_vst3_class_t *out_classes, size_t capacity,
                              size_t *out_count);
/* One stereo main bus, float32, optional separate controller. All native VST3
 * calls, including independent mixers, use one serialized owner thread.
 * Unsupported bus layouts fail explicitly. Block bound <=65536. */
jfx_result_t jfx_vst3_create(const jfx_audio_insert_t *insert, uint32_t sample_rate,
                              uint32_t max_block, jfx_vst3_instance_t **out_instance);
jfx_result_t jfx_vst3_create_with_state(const jfx_audio_insert_t *insert,const void *state,
    size_t state_bytes,uint32_t sample_rate,uint32_t max_block,jfx_vst3_instance_t **out_instance);
/* NULL/zero capacity queries required bytes. Output is untouched on failure. */
jfx_result_t jfx_vst3_save_state(jfx_vst3_instance_t *instance,void *out_state,
    size_t capacity,size_t *out_bytes);
bool jfx_vst3_is_instrument(const jfx_vst3_instance_t *instance);
bool jfx_vst3_accepts_midi(const jfx_vst3_instance_t *instance);
void jfx_vst3_destroy(jfx_vst3_instance_t *instance);
size_t jfx_vst3_parameter_count(const jfx_vst3_instance_t *instance);
jfx_result_t jfx_vst3_parameter(jfx_vst3_instance_t *instance, uint32_t index,
                                jfx_vst3_parameter_t *out_parameter);
uint32_t jfx_vst3_latency(const jfx_vst3_instance_t *instance);
jfx_result_t jfx_vst3_set_tempo(jfx_vst3_instance_t *instance, double bpm);
typedef enum { JFX_MIDI_NOTE_OFF=0,JFX_MIDI_NOTE_ON=1 } jfx_midi_event_type_t;
typedef struct {
    size_t size;
    uint32_t sample_offset;
    jfx_midi_event_type_t type;
    uint8_t channel,pitch;
    float velocity;
    int32_t note_id;
} jfx_midi_event_t;
jfx_result_t jfx_vst3_process_events(jfx_vst3_instance_t *instance,uint64_t sample,
    size_t frames,float *stereo,const jfx_midi_event_t *events,size_t event_count);
/* Controller changes are queued to the processor. Native UI callbacks are
 * delivered on the serialized owner thread; callers must not destroy/reenter
 * the instance from a callback. */
typedef enum { JFX_VST3_EDIT_BEGIN,JFX_VST3_EDIT_VALUE,JFX_VST3_EDIT_END,JFX_VST3_EDIT_DIRTY } jfx_vst3_edit_phase_t;
typedef void (*jfx_vst3_edit_fn)(void *user,jfx_vst3_edit_phase_t phase,uint32_t id,double value);
jfx_result_t jfx_vst3_set_edit_callback(jfx_vst3_instance_t *instance,jfx_vst3_edit_fn callback,void *user);
jfx_result_t jfx_vst3_set_parameter(jfx_vst3_instance_t *instance,uint32_t id,double value);
/* Queue a processor-only normalized automation point for the next block.
 * Offsets must increase per parameter; at most 512 points per parameter. */
jfx_result_t jfx_vst3_add_parameter_point(jfx_vst3_instance_t *instance,uint32_t id,uint32_t sample_offset,double value);
/* Native parent types: HWND, NSView, X11EmbedWindowID. Parent remains alive
 * until editor_close. A resize callback resizes the host container only. */
typedef jfx_result_t (*jfx_vst3_resize_fn)(void *user,uint32_t width,uint32_t height);
jfx_result_t jfx_vst3_editor_size(jfx_vst3_instance_t *instance,uint32_t *out_width,uint32_t *out_height);
jfx_result_t jfx_vst3_editor_open(jfx_vst3_instance_t *instance,void *parent,const char *platform,
    jfx_vst3_resize_fn resize,void *user);
void jfx_vst3_editor_close(jfx_vst3_instance_t *instance);
jfx_result_t jfx_vst3_editor_resize(jfx_vst3_instance_t *instance,uint32_t width,uint32_t height);
void jfx_vst3_editor_focus(jfx_vst3_instance_t *instance,bool focused);
void jfx_vst3_editor_key(jfx_vst3_instance_t *instance,bool down,uint16_t character,int16_t key,int16_t modifiers);
void jfx_vst3_editor_wheel(jfx_vst3_instance_t *instance,float distance);
/* Dispatch registered Linux plugin timers/readable descriptors without workers. */
void jfx_vst3_pump(void);
/* In-place interleaved stereo; no host allocation in process. */
jfx_result_t jfx_vst3_process(jfx_vst3_instance_t *instance, uint64_t sample, size_t frames,
                              float *stereo);
#ifdef __cplusplus
}
#endif
#endif
