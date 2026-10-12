/* Ingagi native module / embedding ABI 1.0.0. C11, C++17; cdecl on Windows.
 * All buffers and descriptors are caller-owned. No pointer to Go memory is
 * returned. A handle is an opaque ID, never an address. Zero is invalid.
 * Calls on a busy handle (including callback reentry) return INGAGI_BUSY.
 * Error buffers are optional (NULL,0); nonempty buffers receive a truncated,
 * NUL-terminated UTF-8 diagnostic. Success clears the diagnostic.
 * Input strings are UTF-8 byte spans, not NUL-terminated; names are ASCII
 * identifiers, max 255 bytes. Source is limited to 4 MiB. Max argc is 256.
 * Callback code/userdata must remain valid until the owning handle is destroyed.
 * Registration copies descriptors/names; it does not load/unload shared objects.
 * The host must not unwind exceptions across this ABI or pass invalid pointers.
 */
#ifndef INGAGI_MODULE_H
#define INGAGI_MODULE_H
#include <stddef.h>
#include <stdint.h>
#define INGAGI_ABI_VERSION UINT32_C(0x010000)
#define INGAGI_MAX_ARGS 256
#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t ingagi_handle;
typedef int32_t ingagi_status;
enum { INGAGI_OK=0, INGAGI_INVALID_ARGUMENT=1, INGAGI_INVALID_HANDLE=2,
       INGAGI_COMPILE_ERROR=3, INGAGI_RUNTIME_ERROR=4, INGAGI_UNSUPPORTED=5,
       INGAGI_BUSY=6, INGAGI_INTERNAL_ERROR=7 };
enum { INGAGI_NIL=0, INGAGI_BOOL=1, INGAGI_INT=2, INGAGI_FLOAT=3 };
typedef struct ingagi_value {
    uint32_t kind;
    uint32_t reserved; /* must be zero */
    int64_t integer;   /* BOOL: exactly 0 or 1; INT: signed 64-bit */
    double real;       /* FLOAT; inactive payload fields are ignored */
} ingagi_value;
typedef ingagi_status (*ingagi_callback)(void *userdata,
    const ingagi_value *args, uint32_t argc, ingagi_value *result,
    char *error, size_t error_capacity);
typedef struct ingagi_function {
    const char *name;
    size_t name_size;
    uint32_t arity;
    uint32_t reserved;
    ingagi_callback callback;
    void *userdata;
} ingagi_function;
typedef struct ingagi_module {
    uint32_t abi_version;
    uint32_t function_count;
    const ingagi_function *functions;
} ingagi_module;
/* Implementations of the embedding library define these symbols. */
#ifndef INGAGI_GO_EXPORTS
uint32_t ingagi_abi_version(void);
ingagi_status ingagi_create(uint64_t instruction_budget, ingagi_handle *result,
    char *error, size_t error_capacity);
ingagi_status ingagi_destroy(ingagi_handle handle, char *error, size_t error_capacity);
ingagi_status ingagi_load(ingagi_handle handle, const char *source, size_t source_size,
    char *error, size_t error_capacity);
ingagi_status ingagi_call(ingagi_handle handle, const char *name, size_t name_size,
    const ingagi_value *args, uint32_t argc, ingagi_value *result,
    char *error, size_t error_capacity);
ingagi_status ingagi_register_module(ingagi_handle handle, const ingagi_module *module,
    char *error, size_t error_capacity);
#endif
#ifdef __cplusplus
}
static_assert(sizeof(ingagi_value)==24, "Ingagi value ABI layout");
#else
_Static_assert(sizeof(ingagi_value)==24, "Ingagi value ABI layout");
#endif
#endif
