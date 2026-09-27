#ifndef JFX_DESKTOP_FRONTEND_H
#define JFX_DESKTOP_FRONTEND_H

#include <stdint.h>

#include "jfx/jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_desktop_frontend jfx_desktop_frontend_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    const char *backend_name;
} jfx_desktop_frontend_config_t;

jfx_result_t jfx_desktop_frontend_create(const jfx_desktop_frontend_config_t *config,
    jfx_desktop_frontend_t **out_frontend);
void jfx_desktop_frontend_destroy(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_open_project(jfx_desktop_frontend_t *frontend,
    const char *path);
jfx_result_t jfx_desktop_frontend_resize(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height);
jfx_result_t jfx_desktop_frontend_play(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_pause(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds);
jfx_result_t jfx_desktop_frontend_draw(jfx_desktop_frontend_t *frontend);
const char *jfx_desktop_frontend_project_path(const jfx_desktop_frontend_t *frontend);

#ifdef __cplusplus
}
#endif

#endif
