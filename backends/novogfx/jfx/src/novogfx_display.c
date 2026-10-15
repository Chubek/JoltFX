#define _POSIX_C_SOURCE 200809L
#include "novogfx_display.h"
#include "novogfx.h"
#include "tilly/allocator.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct jfx_novogfx_display {
    novogfx_device_t *device;
    novogfx_resources_t *resources;
    novogfx_connector_t *connector;
    novogfx_crtc_t *crtc;
    novogfx_dumb_buffer_t *buffers[2];
    novogfx_framebuffer_t *fbs[2]; /* Borrowed from buffers. */
    novogfx_dumb_buffer_info_t info[2];
    uint8_t *maps[2];
    novogfx_event_reader_t *events;
    drmModeCrtc *saved;
    uint32_t *saved_connectors;
    int saved_count;
    uint32_t width, height;
    unsigned front;
    bool pending, changed;
    uint64_t flip_started_ns;
};

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static jfx_result_t fail(char *error, size_t size, const char *op, unsigned status) {
    if (error && size) snprintf(error, size, "NovoGFX %s: %s (%u)", op,
        novogfx_status_string(status), status);
    fprintf(stderr, "NovoGFX %s: %s (%u)\n", op, novogfx_status_string(status), status);
    return status == NOVOGFX_ERR_OUT_OF_MEMORY ? JFX_ERROR_OUT_OF_MEMORY : JFX_ERROR_BACKEND_FAILURE;
}

void jfx_novogfx_display_destroy(jfx_novogfx_display_t *d) {
    if (!d) return;
    if (d->changed && d->saved) {
        /* A synchronous restore retires either scanout buffer, including an
         * outstanding flip, before their mappings/GEM objects are released. */
        int fd = novogfx_device_fd(d->device);
        if (drmModeSetCrtc(fd, d->saved->crtc_id, d->saved->buffer_id,
                d->saved->x, d->saved->y, d->saved_connectors, d->saved_count,
                d->saved->mode_valid ? &d->saved->mode : NULL) != 0)
            fprintf(stderr, "NovoGFX restore CRTC failed: errno %d\n", errno);
    }
    novogfx_event_reader_destroy(d->events);
    for (unsigned i = 0; i < 2; ++i) novogfx_dumb_buffer_destroy(d->buffers[i]);
    if (d->saved) drmModeFreeCrtc(d->saved);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), d->saved_connectors);
    novogfx_crtc_destroy(d->crtc);
    novogfx_resources_destroy(d->resources);
    if (d->device) {
        if (drmIsMaster(novogfx_device_fd(d->device))) drmDropMaster(novogfx_device_fd(d->device));
        novogfx_device_destroy(d->device);
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), d);
}

jfx_result_t jfx_novogfx_display_create(const char *path,
    jfx_novogfx_display_t **out, char *error, size_t size) {
    if (!out) return JFX_ERROR_INVALID_ARGUMENT;
    *out = NULL;
    jfx_novogfx_display_t *d = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*d), _Alignof(jfx_novogfx_display_t));
    if (!d) return JFX_ERROR_OUT_OF_MEMORY;
    memset(d, 0, sizeof(*d));
    unsigned status = NOVOGFX_ERR_NOT_FOUND;
    const char *op = "open DRM card (use JFX_NOVOGFX_DEVICE=/dev/dri/cardN)";
    if (path && path[0]) status = novogfx_device_open(path, &d->device);
    else for (unsigned i = 0; i < 16; ++i) {
        char card[64]; snprintf(card, sizeof(card), "/dev/dri/card%u", i);
        status = novogfx_device_open(card, &d->device);
        if (status == NOVOGFX_OK) break;
    }
    if (status != NOVOGFX_OK) goto failed;
    int fd = novogfx_device_fd(d->device);
    op = "acquire DRM master (run on a free VT, not under a compositor)";
    if (!drmIsMaster(fd) && drmSetMaster(fd) != 0) { status = NOVOGFX_ERR_PERMISSION_DENIED; goto failed; }
    op = "enable atomic modesetting";
    status = novogfx_device_enable_atomic(d->device);
    if (status != NOVOGFX_OK) goto failed;
    status = novogfx_device_get_resources(d->device, &d->resources);
    if (status != NOVOGFX_OK) goto failed;
    /* Use an already routed connector: never steal a CRTC from another output.
     * fbcon normally establishes this routing on the active VT. */
    op = "find connected, configured output";
    status = NOVOGFX_ERR_NOT_FOUND;
    for (unsigned long i = 0; i < d->resources->num_connectors; ++i) {
        novogfx_connector_t *c = d->resources->connectors[i];
        novogfx_connector_status_t connected;
        if (novogfx_connector_status(c, &connected) != NOVOGFX_OK || connected != NOVOGFX_CONNECTOR_CONNECTED) continue;
        d->crtc = novogfx_connector_current_crtc(c);
        if (!d->crtc) continue;
        d->saved = drmModeGetCrtc(fd, (uint32_t)novogfx_crtc_id(d->crtc));
        if (d->saved && d->saved->mode_valid) { d->connector = c; break; }
        if (d->saved) drmModeFreeCrtc(d->saved);
        d->saved = NULL;
        novogfx_crtc_destroy(d->crtc); d->crtc = NULL;
    }
    if (!d->connector) goto failed;
    d->width = d->saved->mode.hdisplay; d->height = d->saved->mode.vdisplay;
    if (!d->width || !d->height || d->width > 8192 || d->height > 8192) { status = NOVOGFX_ERR_NOT_SUPPORTED; goto failed; }
    d->saved_connectors = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        d->resources->num_connectors * sizeof(uint32_t), _Alignof(uint32_t));
    if (!d->saved_connectors) { status = NOVOGFX_ERR_OUT_OF_MEMORY; goto failed; }
    for (unsigned long i = 0; i < d->resources->num_connectors; ++i) {
        novogfx_crtc_t *c = novogfx_connector_current_crtc(d->resources->connectors[i]);
        if (c && novogfx_crtc_id(c) == novogfx_crtc_id(d->crtc))
            d->saved_connectors[d->saved_count++] = (uint32_t)novogfx_connector_id(d->resources->connectors[i]);
        novogfx_crtc_destroy(c);
    }
    op = "create scanout buffers";
    for (unsigned i = 0; i < 2; ++i) {
        novogfx_dumb_buffer_desc_t desc = { d->width, d->height, 32, 0 };
        status = novogfx_dumb_buffer_create(d->device, &desc, &d->buffers[i], &d->fbs[i]);
        if (status != NOVOGFX_OK) goto failed;
        status = novogfx_dumb_buffer_get_info(d->buffers[i], &d->info[i]);
        if (status != NOVOGFX_OK) goto failed;
        if (d->info[i].width != d->width || d->info[i].height != d->height ||
            d->info[i].pitch < (size_t)d->width * 4 || d->info[i].pitch % 4 != 0 ||
            d->info[i].pitch > SIZE_MAX / d->height ||
            d->info[i].size < (size_t)d->info[i].pitch * d->height) {
            status = NOVOGFX_ERR_INTERNAL; goto failed;
        }
        status = novogfx_dumb_buffer_map(d->buffers[i], &d->maps[i]);
        if (status != NOVOGFX_OK) goto failed;
        memset(d->maps[i], 0, d->info[i].size);
    }
    status = novogfx_event_reader_create(d->device, &d->events);
    if (status != NOVOGFX_OK) goto failed;
    op = "initial atomic scanout";
    novogfx_atomic_request_t *request = NULL;
    status = novogfx_atomic_request_create(d->device, &request);
    if (status != NOVOGFX_OK) goto failed;
    status = novogfx_atomic_request_set_crtc(request, d->crtc, d->fbs[0],
        novogfx_crtc_current_mode(d->crtc), d->connector);
    if (status == NOVOGFX_OK) status = novogfx_atomic_request_commit(request,
        NOVOGFX_COMMIT_ALLOW_MODESET | NOVOGFX_COMMIT_TEST_ONLY);
    if (status == NOVOGFX_OK) status = novogfx_atomic_request_commit(request, NOVOGFX_COMMIT_ALLOW_MODESET);
    novogfx_atomic_request_destroy(request);
    if (status != NOVOGFX_OK) goto failed;
    d->changed = true;
    *out = d;
    return JFX_SUCCESS;
failed:;
    jfx_result_t result = fail(error, size, op, status);
    jfx_novogfx_display_destroy(d);
    return result;
}

uint32_t jfx_novogfx_display_width(const jfx_novogfx_display_t *d) { return d ? d->width : 0; }
uint32_t jfx_novogfx_display_height(const jfx_novogfx_display_t *d) { return d ? d->height : 0; }

jfx_result_t jfx_novogfx_display_present(jfx_novogfx_display_t *d, const uint8_t *rgba, size_t pitch) {
    if (!d || !rgba || pitch < (size_t)d->width * 4 || pitch > SIZE_MAX / d->height)
        return JFX_ERROR_INVALID_ARGUMENT;
    novogfx_event_t *events = NULL; unsigned long count = 0;
    unsigned status = novogfx_event_reader_poll(d->events, &events, &count);
    if (status != NOVOGFX_OK && status != NOVOGFX_ERR_AGAIN) return fail(NULL, 0, "poll flip", status);
    for (unsigned long i = 0; i < count; ++i) {
        if (events[i].type == NOVOGFX_EVENT_PAGE_FLIP &&
            (events[i].crtc_id == 0 || events[i].crtc_id == novogfx_crtc_id(d->crtc))) {
            if (events[i].flip_status != 0) return fail(NULL, 0, "flip completion", NOVOGFX_ERR_DEVICE_LOST);
            d->pending = false;
        }
    }
    if (d->pending) {
        if (monotonic_ns() - d->flip_started_ns > UINT64_C(2000000000))
            return fail(NULL, 0, "flip completion timeout", NOVOGFX_ERR_DEVICE_LOST);
        return JFX_ERROR_BUSY;
    }
    unsigned back = d->front ^ 1u;
    /* NovoGFX's 32-bpp dumb framebuffer is XRGB8888, not RGBA byte order. */
    for (uint32_t y = 0; y < d->height; ++y) {
        uint8_t *dst = d->maps[back] + y * d->info[back].pitch;
        const uint8_t *src = rgba + y * pitch;
        for (uint32_t x = 0; x < d->width; ++x) {
            uint32_t packed = 0xff000000u | ((uint32_t)src[x*4] << 16) |
                ((uint32_t)src[x*4+1] << 8) | src[x*4+2];
            memcpy(dst + x*4, &packed, sizeof(packed));
        }
    }
    status = novogfx_crtc_page_flip(d->crtc, d->fbs[back], DRM_MODE_PAGE_FLIP_EVENT, 0);
    if (status != NOVOGFX_OK) return fail(NULL, 0, "page flip", status);
    d->front = back; d->pending = true;
    d->flip_started_ns = monotonic_ns();
    return JFX_SUCCESS;
}
