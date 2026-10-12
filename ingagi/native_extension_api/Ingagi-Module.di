// D import interface for Ingagi ABI 1.0.0. Install/import as ingagi_module.di.
// Link libingagi_embed. Descriptor and callback lifetimes follow the C header;
// no exceptions may escape callbacks. No ownership of foreign buffers transfers.
module ingagi_module;
import object : size_t;

enum uint INGAGI_ABI_VERSION = 0x010000;
enum INGAGI_MAX_ARGS = 256;
alias ingagi_handle = ulong;
alias ingagi_status = int;
enum : ingagi_status {
    INGAGI_OK, INGAGI_INVALID_ARGUMENT, INGAGI_INVALID_HANDLE,
    INGAGI_COMPILE_ERROR, INGAGI_RUNTIME_ERROR, INGAGI_UNSUPPORTED,
    INGAGI_BUSY, INGAGI_INTERNAL_ERROR
}
enum : uint { INGAGI_NIL, INGAGI_BOOL, INGAGI_INT, INGAGI_FLOAT }
extern(C) {
    struct ingagi_value {
        uint kind;
        uint reserved;
        long integer;
        double real_value = 0; // C field "real"; avoid D's default NaN initializer
    }
    alias ingagi_callback = ingagi_status function(void* userdata,
        const(ingagi_value)* args, uint argc, ingagi_value* result,
        char* error, size_t error_capacity) nothrow;
    struct ingagi_function {
        const(char)* name;
        size_t name_size;
        uint arity;
        uint reserved;
        ingagi_callback callback;
        void* userdata;
    }
    struct ingagi_module {
        uint abi_version;
        uint function_count;
        const(ingagi_function)* functions;
    }
    uint ingagi_abi_version() nothrow;
    ingagi_status ingagi_create(ulong budget, ingagi_handle* result, char* error, size_t capacity) nothrow;
    ingagi_status ingagi_destroy(ingagi_handle handle, char* error, size_t capacity) nothrow;
    ingagi_status ingagi_load(ingagi_handle handle, const(char)* source, size_t size, char* error, size_t capacity) nothrow;
    ingagi_status ingagi_call(ingagi_handle handle, const(char)* name, size_t size,
        const(ingagi_value)* args, uint argc, ingagi_value* result, char* error, size_t capacity) nothrow;
    ingagi_status ingagi_register_module(ingagi_handle handle, const(ingagi_module)* descriptor,
        char* error, size_t capacity) nothrow;
}
static assert(ingagi_value.sizeof == 24);
static assert(ingagi_value.integer.offsetof == 8);
static assert(ingagi_value.real_value.offsetof == 16);
