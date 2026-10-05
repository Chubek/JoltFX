#include "tilly/tilly.h"
#include "module_fixture.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static int diagnostics;
static void sink(const tilly_log_entry_t *entry, void *user) {
    (void)user;
    if (!strcmp(entry->message, "context logging alive")) ++diagnostics;
}
int main(int argc, char **argv) {
    assert(argc >= 3);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    union { void *object; tilly_test_module_state_t *(*function)(void); } symbol;
    symbol.object = dlsym(library, "tilly_test_module_state");
    assert(symbol.function);
    tilly_test_module_state_t *state = symbol.function();
    state->query_registry = argc > 3;
    state->self_path = state->query_registry ? argv[1] : NULL;
    state->dependency_path = state->query_registry ? argv[2] : NULL;
    tilly_context_t *first = tilly_init(NULL);
    assert(first);
    tilly_log_add_sink(sink, NULL);
    tilly_log_simple(TILLY_LOG_INFO, "context logging alive");
    assert(diagnostics == 1);
    size_t baseline = tilly_allocator_usage(tilly_get_heap_allocator(first));
    assert(!tilly_module_load(NULL, argv[1]));
    assert(!tilly_module_load(first, NULL));
    assert(!tilly_module_load(first, ""));
    tilly_module_t *a = tilly_module_load(first, argv[1]);
    assert(a && state->initialized == 1);
    assert(tilly_module_get_context(a) == first);
    tilly_module_t *b = tilly_module_load(first, argv[1]);
    assert(b == a && a->ref_count == 2 && state->initialized == 1);
    const char *slash = strrchr(argv[1], '/');
    assert(slash);
    char alias[4096];
    int written = snprintf(alias, sizeof(alias), "%.*s/./%s", (int)(slash - argv[1]), argv[1], slash + 1);
    assert(written > 0 && (size_t)written < sizeof(alias));
    assert(tilly_module_load(first, alias) == a && a->ref_count == 3);
    tilly_module_unload(first, a);
    assert(a->ref_count == 2);
    assert(tilly_module_find(first, a->name) == a);
    assert(!tilly_module_find(first, "missing"));
    tilly_context_t *second = tilly_init(NULL);
    assert(second && tilly_module_get_context(a) == first);
    assert(!tilly_module_get_context(NULL));
    tilly_module_unload(second, a);
    assert(a->ref_count == 2 && state->shut_down == 0);
    assert(tilly_allocator_usage(tilly_get_heap_allocator(second)) == 0);
    tilly_shutdown(second);
    tilly_log_simple(TILLY_LOG_INFO, "context logging alive");
    assert(diagnostics == 2);
    assert(tilly_module_get_context(a) == first);
    tilly_module_unload(first, a);
    assert(b->ref_count == 1 && state->shut_down == 0);
    tilly_module_unload(first, b);
    assert(state->shut_down == 1);
    assert(tilly_allocator_usage(tilly_get_heap_allocator(first)) == baseline);
    /* An already removed handle is checked by registry membership before use. */
    tilly_module_unload(first, b);
    assert(state->shut_down == 1);
    state->fail_initialize = 1;
    assert(!tilly_module_load(first, argv[1]));
    assert(tilly_allocator_usage(tilly_get_heap_allocator(first)) == baseline);
    state->fail_initialize = 0;
    a = tilly_module_load(first, argv[1]);
    assert(a && state->initialized == 3);
    assert(tilly_module_load(first, argv[1]) == a);
    tilly_shutdown(first);
    assert(state->shut_down == 2);
    assert(state->missing_dependency == 0);
    assert(dlclose(library) == 0);
    puts("Tilly module lifetime checks passed");
    return 0;
}
