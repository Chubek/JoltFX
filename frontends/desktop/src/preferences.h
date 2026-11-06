#ifndef JFX_DESKTOP_PREFERENCES_H
#define JFX_DESKTOP_PREFERENCES_H

#include <cstddef>

// Private application settings, deliberately separate from project/render ABIs.
namespace jfx_preferences {
struct Values {
    float viewport_background[3];
    float orbit_speed, pan_speed, zoom_speed, ui_scale;
    int preview_max_dimension, theme; // theme: dark=0, light=1, classic=2
    bool invert_zoom, show_gimbal, loop_playback, timeline_snap;
    int page_auto_commit_seconds; // 0 disables; otherwise 1..3600 seconds
};
struct Paths { char config[4096], ini[4096]; };
enum class LoadResult { Loaded, Missing, Invalid, IOError };
Values defaults();
bool resolve_paths(Paths &out);
// Missing members use defaults. Invalid files leave the caller unchanged.
LoadResult load(const char *path, Values &out, char *error, size_t capacity);
// Bounded JSON, exclusively created sibling, atomic replacement. Never truncates
// an existing file on validation, allocation, write or publication failure.
bool save(const char *path, const Values &values, char *error, size_t capacity);
}
#endif
