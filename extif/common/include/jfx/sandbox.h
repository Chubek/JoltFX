#ifndef JFX_SCRIPT_SANDBOX_H
#define JFX_SCRIPT_SANDBOX_H
#include "jfx/script_runtime.h"
/* Resource/system access is granted only through these host capabilities.
 * No flag enables process control, filesystem modules or native-library loading. */
#define JFX_SCRIPT_DEFAULT_MEMORY (4u * 1024u * 1024u)
#define JFX_SCRIPT_DEFAULT_INSTRUCTIONS UINT64_C(100000)
#endif
