#ifndef JFX_DESKTOP_NOVOGFX_HOST_H
#define JFX_DESKTOP_NOVOGFX_HOST_H
#include <cstddef>
#include <cstdint>
#include "jfx/jfx_result.h"
struct jfx_novogfx_host;
jfx_novogfx_host *jfx_novogfx_host_create(char *error, size_t size);
void jfx_novogfx_host_destroy(jfx_novogfx_host *host);
bool jfx_novogfx_host_begin(jfx_novogfx_host *host);
jfx_result_t jfx_novogfx_host_end(jfx_novogfx_host *host);
jfx_result_t jfx_novogfx_host_result(const jfx_novogfx_host *host);
uint32_t jfx_novogfx_host_width(const jfx_novogfx_host *host);
uint32_t jfx_novogfx_host_height(const jfx_novogfx_host *host);
void *jfx_novogfx_host_upload(jfx_novogfx_host *host, uint32_t width, uint32_t height, const uint8_t *rgba);
#endif
