#include "../native_extension_api/Ingagi-Module.h"
ingagi_status ig_invoke(ingagi_callback fn, void *userdata,
    const ingagi_value *args, uint32_t argc, ingagi_value *result,
    char *error, size_t capacity);
