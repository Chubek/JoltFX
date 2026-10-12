//go:build cgo

#include "bridge.h"
ingagi_status ig_invoke(ingagi_callback fn, void *userdata,
    const ingagi_value *args, uint32_t argc, ingagi_value *result,
    char *error, size_t capacity) {
    return fn(userdata, args, argc, result, error, capacity);
}
