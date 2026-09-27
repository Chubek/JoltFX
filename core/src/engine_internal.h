#ifndef JFX_ENGINE_INTERNAL_H
#define JFX_ENGINE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

/* Live-object accounting shared by jfx_buffer, jfx_texture and jfx_kernel.
 *
 * Every engine-owned object holds one slot against a limit taken from
 * jfx_engine_config_t (0 means "use the built-in default"). Acquiring before
 * allocation and releasing on every exit path keeps the configured limits
 * real rather than decorative, and lets jfx_engine_live_* report truth.
 *
 * The engine struct stays private to engine.c; resource translation units go
 * through these accessors. */

/* Resolves a 0 ("use the default") configured limit. */
uint32_t engine_limit(const jfx_engine_t *engine, uint32_t configured, uint32_t fallback);

#define JFX_ENGINE_DECLARE_SLOTS(kind, plural, fallback)                                            \
    /* Takes one slot if the limit allows. False when the pool is full. */                            \
    bool engine_acquire_##kind(jfx_engine_t *engine);                                                \
    /* Returns one slot. Safe on a NULL engine and never underflows. */                               \
    void engine_release_##kind(jfx_engine_t *engine);                                                \
    /* Current live count. */                                                                          \
    uint32_t engine_live_##plural(const jfx_engine_t *engine);

JFX_ENGINE_DECLARE_SLOTS(buffer, buffers, 256u)
JFX_ENGINE_DECLARE_SLOTS(texture, textures, 256u)
JFX_ENGINE_DECLARE_SLOTS(kernel, kernels, 64u)

#endif /* JFX_ENGINE_INTERNAL_H */
