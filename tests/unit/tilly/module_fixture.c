#include "module_fixture.h"
static tilly_test_module_state_t state;
static int initialize(tilly_context_t *ctx) {
    ++state.initialized;
    state.last_context = ctx;
    if (state.query_registry) (void)tilly_module_find(ctx, "missing");
    if (state.self_path && tilly_module_load(ctx, state.self_path)) return -2;
    if (state.fail_initialize) return -1;
    if (state.dependency_path) {
        state.dependency = tilly_module_load(ctx, state.dependency_path);
        if (!state.dependency) return -3;
    }
    return 0;
}
static void shut_down(tilly_context_t *ctx) {
    ++state.shut_down;
    state.last_context = ctx;
    if (state.query_registry) (void)tilly_module_find(ctx, "missing");
    if (state.dependency) {
        if (!tilly_module_find(ctx, "libtilly_test_dependency")) ++state.missing_dependency;
        tilly_module_unload(ctx, state.dependency);
        state.dependency = NULL;
    }
}
TILLY_MODULE_EXPORT tilly_module_api_t *tilly_module_register(void) {
    static tilly_module_api_t api = {"fixture", "1.0", initialize, shut_down};
    return &api;
}
TILLY_MODULE_EXPORT tilly_test_module_state_t *tilly_test_module_state(void) {
    return &state;
}
