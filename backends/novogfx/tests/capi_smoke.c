/* Runtime ABI checks; device checks never apply a modeset or page flip. */
#define _DEFAULT_SOURCE
#include "novogfx.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <sys/mman.h>
#include <errno.h>

int main(void) {
    assert(novogfx_abi_version() == 300);
    assert(strcmp(novogfx_version_string(), "0.3.0") == 0);
    assert(strcmp(novogfx_format_name(NOVOGFX_FORMAT_XRGB8888), "XRGB8888") == 0);
    assert(strcmp(novogfx_format_name(NOVOGFX_FORMAT_RGB565), "RGB565") == 0);
    assert(strcmp(novogfx_format_name(NOVOGFX_FORMAT_YUV444), "YUV444") == 0);
    assert(strcmp(novogfx_format_name(0), "UNKNOWN") == 0);
    assert(novogfx_device_open(NULL, NULL) == NOVOGFX_ERR_INVALID_ARGUMENT);
    assert(novogfx_atomic_request_commit(NULL, NOVOGFX_COMMIT_TEST_ONLY) == NOVOGFX_ERR_INVALID_ARGUMENT);
    assert(novogfx_crtc_current_mode(NULL) == NULL);
    novogfx_resources_destroy(NULL);
    novogfx_device_destroy(NULL);

    const char *path = getenv("NOVOGFX_TEST_DEVICE");
    if (path) {
        novogfx_device_t *device = NULL;
        novogfx_resources_t *resources = NULL;
        assert(novogfx_device_open(path, &device) == NOVOGFX_OK);
        novogfx_atomic_request_t *request = NULL;
        assert(novogfx_atomic_request_create(device, &request) == NOVOGFX_ERR_NOT_SUPPORTED);
        assert(request == NULL);
        assert(novogfx_device_enable_atomic(device) == NOVOGFX_OK);
        assert(novogfx_device_caps(device) & NOVOGFX_CAP_ATOMIC);
        assert(novogfx_device_get_resources(device, &resources) == NOVOGFX_OK);
        novogfx_resources_t *second = NULL;
        assert(novogfx_device_get_resources(device, &second) == NOVOGFX_OK);
        assert(resources->num_connectors == second->num_connectors);
        assert(resources->num_crtcs == second->num_crtcs);
        assert(resources->num_planes == second->num_planes);
        for (unsigned long i = 0; i < resources->num_connectors; ++i)
            assert(resources->connectors[i] == second->connectors[i]);
        for (unsigned long i = 0; i < resources->num_crtcs; ++i)
            assert(resources->crtcs[i] == second->crtcs[i]);
        for (unsigned long i = 0; i < resources->num_planes; ++i)
            assert(resources->planes[i] == second->planes[i]);
        novogfx_resources_destroy(resources);
        resources = second; /* Shared handles remain live through this owner. */
        for (unsigned long i = 0; i < resources->num_connectors; ++i) {
            const novogfx_connector_t *connector = resources->connectors[i];
            for (unsigned long j = 0; j < novogfx_connector_num_modes(connector); ++j) {
                const novogfx_mode_t *mode = novogfx_connector_mode(connector, j);
                assert(mode && mode->name && strlen(mode->name) < 32);
                assert(mode->hdisplay && mode->vdisplay);
            }
        }
        for (unsigned long i = 0; i < resources->num_crtcs; ++i) {
            const novogfx_mode_t *mode = novogfx_crtc_current_mode(resources->crtcs[i]);
            if (mode) assert(mode->name && mode->hdisplay && mode->vdisplay);
        }
        novogfx_dumb_buffer_desc_t desc = {64, 64, 32, 0};
        novogfx_dumb_buffer_t *buffer = NULL;
        novogfx_framebuffer_t *fb = NULL;
#if ULONG_MAX > UINT32_MAX
        desc.width = (1ul << 32) + 64;
        assert(novogfx_dumb_buffer_create(device, &desc, &buffer, &fb) == NOVOGFX_ERR_INVALID_ARGUMENT);
        assert(buffer == NULL && fb == NULL);
        desc.width = 64;
#endif
        assert(novogfx_dumb_buffer_create(device, &desc, &buffer, &fb) == NOVOGFX_OK);
        unsigned char *mapped = NULL;
        assert(novogfx_dumb_buffer_map(buffer, &mapped) == NOVOGFX_OK);
        mapped[0] = 0x5a;
        uintptr_t old_mapping = (uintptr_t)mapped;
        unsigned long fb_id = novogfx_framebuffer_id(fb);
        novogfx_dumb_buffer_unmap(buffer);
        unsigned char residency;
        errno = 0;
        assert(mincore((void *)old_mapping, 1, &residency) == -1 && errno == ENOMEM);
        novogfx_dumb_buffer_unmap(buffer); /* Idempotent. */
        assert(novogfx_dumb_buffer_map(buffer, &mapped) == NOVOGFX_OK);
        assert(mapped[0] == 0x5a);
        assert(novogfx_framebuffer_id(fb) == fb_id);
        novogfx_dumb_buffer_destroy(buffer);

        assert(novogfx_atomic_request_create(device, &request) == NOVOGFX_OK);
        assert(novogfx_atomic_request_commit(request, 16) == NOVOGFX_ERR_INVALID_ARGUMENT);
        novogfx_device_t *foreign = NULL;
        novogfx_resources_t *foreign_resources = NULL;
        assert(novogfx_device_open(path, &foreign) == NOVOGFX_OK);
        assert(novogfx_device_get_resources(foreign, &foreign_resources) == NOVOGFX_OK);
        if (foreign_resources->num_crtcs)
            assert(novogfx_atomic_request_set_crtc(request, foreign_resources->crtcs[0], NULL, NULL, NULL) == NOVOGFX_ERR_INVALID_ARGUMENT);
        novogfx_resources_destroy(foreign_resources);
        novogfx_device_destroy(foreign);
        novogfx_atomic_request_destroy(request);
        novogfx_resources_destroy(resources);
        novogfx_device_destroy(device);
    }
    puts("C ABI runtime checks passed");
    return 0;
}
