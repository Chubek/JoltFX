#ifndef INGAGI_ANIMATION_NATIVE_H
#define INGAGI_ANIMATION_NATIVE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private synchronous desktop bridge. Callback borrows UTF-8 output only for
 * the call, returns 0 on success. No partial output on compile/runtime errors.
 * target: 0=JFXDRAW2, 1=HTML Canvas, 2=SVG frame 0. */
typedef int (*ig_animation_output)(void *user, const char *data, size_t size);
#ifndef INGAGI_ANIMATION_EXPORTS
int ig_animation_bake(const char *source,size_t size,int fps,int frames,int target,
                     ig_animation_output output,void *user,char *error,size_t capacity);
int ig_animation_export(const char *drawing,size_t size,int target,
                       ig_animation_output output,void *user,char *error,size_t capacity);
#endif
#ifdef __cplusplus
}
#endif
#endif
