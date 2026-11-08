#ifndef JOLT_SUBTITLE_IO_H
#define JOLT_SUBTITLE_IO_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
int jolt_subtitle_available(void);
/* Local ASS/SSA file, <=8 MiB, rendered into straight RGBA8. */
jolt_status_t jolt_subtitle_render(const char *path,double seconds,uint32_t width,uint32_t height,uint8_t *rgba);
#ifdef __cplusplus
}
#endif
#endif
