#ifndef TILLY_TEST_MODULE_FIXTURE_H
#define TILLY_TEST_MODULE_FIXTURE_H
#include "tilly/module.h"
typedef struct {
    unsigned initialized, shut_down;
    int query_registry;
    int fail_initialize;
    const char *self_path;
    const char *dependency_path;
    tilly_module_t *dependency;
    unsigned missing_dependency;
    tilly_context_t *last_context;
} tilly_test_module_state_t;
#endif
