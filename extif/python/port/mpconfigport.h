#include <stdint.h>
#include <stddef.h>
typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;
#if defined(_MSC_VER)
#include <malloc.h>
#define alloca _alloca
#else
#include <alloca.h>
#endif
#define MICROPY_MPHALPORT_H "port/mphalport.h"
#define MICROPY_CONFIG_ROM_LEVEL MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0)
#define MICROPY_PERSISTENT_CODE_LOAD (0)
#define MICROPY_PERSISTENT_CODE_SAVE (0)
#define MICROPY_LONGINT_IMPL MICROPY_LONGINT_IMPL_MPZ
#define MICROPY_FLOAT_IMPL MICROPY_FLOAT_IMPL_DOUBLE
#define MICROPY_PY_BUILTINS_EVAL_EXEC (0)
#define MICROPY_PY_BUILTINS_EXECFILE (0)
#define MICROPY_PY_BUILTINS_INPUT (0)
#define MICROPY_PY_BUILTINS_HELP (0)
#define MICROPY_PY_GC (0)
#define MICROPY_PY_IO (0)
#define MICROPY_PY_SYS (0)
#define MICROPY_PY_MICROPYTHON (0)
#define MICROPY_PY_THREAD (0)
#define MICROPY_PY_OS (0)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_BUILTINS_FLOAT (1)
#define MICROPY_ERROR_REPORTING MICROPY_ERROR_REPORTING_DETAILED
#define MICROPY_CPYTHON_COMPAT (1)
#define MICROPY_ROM_TEXT_COMPRESSION (0)
#define MICROPY_NLR_SETJMP (1)
void jfx_python_vm_hook(void);
#define MICROPY_VM_HOOK_LOOP jfx_python_vm_hook();
#define MICROPY_VM_HOOK_RETURN jfx_python_vm_hook();
