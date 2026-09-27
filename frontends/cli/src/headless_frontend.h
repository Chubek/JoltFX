#ifndef JOLTFX_HEADLESS_FRONTEND_H
#define JOLTFX_HEADLESS_FRONTEND_H

#include "jfx_frontend.h"

/* The headless frontend: the shared frontend contract with no window and no UI.
 *
 * It is what the CLI drives, and it is the reference implementation of
 * jfx_frontend_t: playback, a real RGBA8 render through the engine's selected
 * backend, and a PPM frame-sequence export. Everything is real, so the CLI's
 * `render` and `export` exercise the same path a GUI frontend uses. */

/* The ops table, for callers that drive the contract directly. */
extern const jfx_frontend_ops_t jfx_headless_frontend_ops;

/* Selects the rendered effect. Unknown names are rejected. */
jfx_result_t jfx_headless_set_effect(void *state, const char *effect_name, float parameter);

/* Resolved backend name for the engine this frontend owns. */
const char *jfx_headless_backend_name(const jfx_frontend_t *frontend);

#endif /* JOLTFX_HEADLESS_FRONTEND_H */
