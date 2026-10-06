#ifndef JFX_MIDI_H
#define JFX_MIDI_H
#include "jfx_timeline.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_MIDI_API_MAJOR 1
#define JFX_MIDI_API_MINOR 0
#define JFX_MIDI_MAX_NOTES 1024u
typedef struct {
    size_t size;
    uint64_t frame,length; /* source frames; clip in-point/splits preserve notes */
    uint8_t channel,pitch;
    float velocity;
} jfx_midi_note_t;
size_t jfx_timeline_midi_note_count(const jfx_timeline_t *timeline,uint32_t track,uint32_t clip);
jfx_result_t jfx_timeline_get_midi_note(const jfx_timeline_t *timeline,uint32_t track,
    uint32_t clip,uint32_t note,jfx_midi_note_t *out_note);
jfx_result_t jfx_timeline_add_midi_note(jfx_timeline_t *timeline,uint32_t track,
    uint32_t clip,const jfx_midi_note_t *note);
jfx_result_t jfx_timeline_set_midi_note(jfx_timeline_t *timeline,uint32_t track,
    uint32_t clip,uint32_t note,const jfx_midi_note_t *value);
jfx_result_t jfx_timeline_remove_midi_note(jfx_timeline_t *timeline,uint32_t track,uint32_t clip,uint32_t note);
#ifdef __cplusplus
}
#endif
#endif
