#ifndef JFX_TEST_BACKEND_H
#define JFX_TEST_BACKEND_H

/* Picks a backend name that this build actually contains.
 *
 * Backends are gated by JFX_BACKEND_* CMake options, and jfx_core publishes
 * those as compile definitions. A test that hardcodes "webgpu" therefore fails
 * in a configuration where that backend is switched off, for no reason related
 * to what the test is checking. Include this and call jfx_test_backend() for a
 * name the engine will accept. */

#include "jfx/jfx_engine.h"

#if defined(JFX_BACKEND_VULKAN)
#define JFX_TEST_BACKEND_NAME "vulkan"
#elif defined(JFX_BACKEND_WEBGPU)
#define JFX_TEST_BACKEND_NAME "webgpu"
#elif defined(JFX_BACKEND_METAL)
#define JFX_TEST_BACKEND_NAME "metal"
#elif defined(JFX_BACKEND_D3D12)
#define JFX_TEST_BACKEND_NAME "d3d12"
#else
#error "no backend enabled"
#endif

static inline const char *jfx_test_backend(void) {
    return JFX_TEST_BACKEND_NAME;
}

#endif /* JFX_TEST_BACKEND_H */
