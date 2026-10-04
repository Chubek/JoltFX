#ifndef JFX_SCRIPT_WASM_H
#define JFX_SCRIPT_WASM_H
#include "jfx/script_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
jfx_script_status_t jfx_wasm_script_create(const jfx_script_desc_t *desc,
    jfx_script_runtime_t **out_runtime);
/* joltwasm ABI 1: little-endian 32-byte values in linear memory. Type uses
 * jfx_value_type_t. payload stores i64/f64/u32/color, 2-4 f32 lanes, or a
 * u32 string offset and u32 length. Resources use an opaque u64 scope token.
 * FUNCTION records cannot be forged by a guest; subscriptions use export names.
 * Typed modules export immutable i32 globals jfx_abi_version=1, jfx_scratch,
 * jfx_scratch_size, and each callable has (i32 args,i32 argc,i32 result)->i32.
 * Import joltfx.call(op_ptr,op_len,args,argc,result)->i32 for shared services.
 * For string service results, initialize result payload with destination and
 * capacity; the host returns the written length. No WASI imports are supplied. */
#define JFX_WASM_ABI_VERSION 1
#define JFX_WASM_VALUE_SIZE 32
#ifdef __cplusplus
}
#endif
#endif
