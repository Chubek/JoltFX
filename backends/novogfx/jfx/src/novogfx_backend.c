/* NovoGFX backend.
 *
 * NovoGFX is JoltFX's Linux DRM/KMS backend: it owns the display-device
 * lifecycle and capability reporting through the Rust novogfx-c API, and
 * dispatches execution through the shared CPU path in jfx_software_adapter.
 * KMS owns scanout, not shader dispatch, so `used_gpu` always reports the
 * CPU path truthfully even when a DRM device is held.
 *
 * Probing walks /dev/dri/cardN and keeps the first node that opens and
 * enumerates KMS resources; permission failures are expected on seat-less
 * sessions and simply skip the node. When no device probes, the backend
 * still creates and reports the software fallback, exactly like the other
 * portable adapters. */

#include "jfx/novogfx_backend.h"
#include "jfx/software_backend.h"
#include "tilly/allocator.h"

#include <stdio.h>

#include "novogfx.h"

#define JFX_NOVOGFX_MAX_CARD 15

struct jfx_novogfx_backend {
    jfx_software_adapter_t *adapter;
    novogfx_device_t *device; /* NULL when no DRM/KMS device probed. */
    char device_name[256];
    uint32_t api_version;
};

/* Fills device_name/api_version from the driver info. Failures degrade to a
 * generic name: the device is still usable for scanout without them. */
static void novogfx_describe_device(novogfx_device_t *device, char *out_name,
    size_t out_name_size, uint32_t *out_api_version) {
    novogfx_device_info_t info;
    snprintf(out_name, out_name_size, "drm-device (NovoGFX KMS)");
    *out_api_version = 0;
    if (novogfx_device_get_info(device, &info) != NOVOGFX_OK) {
        return;
    }
    if (info.driver_name) {
        snprintf(out_name, out_name_size, "%s (NovoGFX DRM/KMS)", info.driver_name);
    }
    *out_api_version = (uint32_t)((info.driver_major << 16) |
        (info.driver_minor << 8) | (info.driver_patch & 0xffu));
    novogfx_string_free(info.driver_name);
    novogfx_string_free(info.driver_date);
    novogfx_string_free(info.driver_desc);
}

/* Opens the first KMS-capable card node. Returns NULL when none is usable;
 * that is a normal outcome (headless, no seat, no DRM hardware), not an
 * error. */
static novogfx_device_t *novogfx_probe(void) {
    char path[64];
    for (unsigned i = 0; i <= JFX_NOVOGFX_MAX_CARD; ++i) {
        snprintf(path, sizeof(path), "/dev/dri/card%u", i);
        novogfx_device_t *device = NULL;
        if (novogfx_device_open(path, &device) != NOVOGFX_OK || !device) {
            continue;
        }
        /* A node only counts when the kernel answers KMS resource queries. */
        novogfx_resources_t *resources = NULL;
        if (novogfx_device_get_resources(device, &resources) == NOVOGFX_OK &&
            resources) {
            novogfx_resources_destroy(resources);
            return device;
        }
        novogfx_device_destroy(device);
    }
    return NULL;
}

const char *jfx_novogfx_backend_name(void) {
    return "novogfx";
}

jfx_result_t jfx_novogfx_backend_create(const jfx_novogfx_backend_config_t *config,
    jfx_novogfx_backend_t **out_backend) {
    if (!out_backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_backend = NULL;
    jfx_software_adapter_t *adapter = NULL;
    jfx_result_t status = jfx_software_adapter_create(jfx_novogfx_backend_name(),
        config ? config->memory_limit : 0, &adapter);
    if (status != JFX_SUCCESS) {
        return status;
    }
    struct jfx_novogfx_backend *backend = tilly_alloc(
        (tilly_allocator_t *)tilly_default_allocator(), sizeof(*backend),
        _Alignof(struct jfx_novogfx_backend));
    if (!backend) {
        jfx_software_adapter_destroy(adapter);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    backend->adapter = adapter;
    backend->device = NULL;
    backend->device_name[0] = '\0';
    backend->api_version = 0;

    const bool probe = !config || config->probe_gpu;
    if (probe) {
        backend->device = novogfx_probe();
        if (backend->device) {
            novogfx_describe_device(backend->device, backend->device_name,
                sizeof(backend->device_name), &backend->api_version);
        }
    }
    *out_backend = backend;
    return JFX_SUCCESS;
}

void jfx_novogfx_backend_destroy(jfx_novogfx_backend_t *backend) {
    if (!backend) {
        return;
    }
    if (backend->device) {
        novogfx_device_destroy(backend->device);
    }
    jfx_software_adapter_destroy(backend->adapter);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), backend);
}

void jfx_novogfx_query_caps(const jfx_novogfx_backend_t *backend,
    jfx_novogfx_caps_t *out_caps) {
    if (!out_caps) {
        return;
    }
    /* The adapter fills the truthful execution-side caps (CPU path, used_gpu,
     * fallback name); a probed device upgrades the report in place. */
    jfx_software_adapter_query_caps(backend ? backend->adapter : NULL, out_caps);
    if (backend && backend->device) {
        out_caps->gpu_available = true;
        out_caps->api_version = backend->api_version;
        snprintf(out_caps->device_name, sizeof(out_caps->device_name), "%s",
            backend->device_name);
    }
}

bool jfx_novogfx_used_gpu(const jfx_novogfx_backend_t *backend) {
    return jfx_software_adapter_used_gpu(backend ? backend->adapter : NULL);
}

jfx_result_t jfx_novogfx_execute(jfx_novogfx_backend_t *backend, const char *source,
    const float *input_rgba, size_t pixels, const float *parameters,
    size_t parameter_count, float *output_rgba) {
    if (!backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_software_adapter_execute(backend->adapter, source, input_rgba, pixels,
        parameters, parameter_count, output_rgba);
}

jfx_result_t jfx_novogfx_execute_bytecode(jfx_novogfx_backend_t *backend,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    if (!backend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    return jfx_software_adapter_execute_bytecode(backend->adapter, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}

/* ---- HAL ---- */

static void ops_query_caps(jfx_backend_handle_t handle, jfx_backend_caps_t *out_caps) {
    jfx_novogfx_query_caps((jfx_novogfx_backend_t *)handle, out_caps);
}

static jfx_result_t ops_execute_bytecode(jfx_backend_handle_t handle,
    const uint8_t *bytecode, size_t bytecode_size, const float *input_rgba,
    size_t pixels, const float *parameters, size_t parameter_count,
    float *output_rgba) {
    return jfx_novogfx_execute_bytecode((jfx_novogfx_backend_t *)handle, bytecode,
        bytecode_size, input_rgba, pixels, parameters, parameter_count, output_rgba);
}

static void ops_destroy(jfx_backend_handle_t handle) {
    jfx_novogfx_backend_destroy((jfx_novogfx_backend_t *)handle);
}

static const jfx_backend_ops_t kNovoGfxOps = {
    .name = jfx_novogfx_backend_name,
    .query_caps = ops_query_caps,
    .execute_bytecode = ops_execute_bytecode,
    .destroy = ops_destroy,
};

const jfx_backend_ops_t *jfx_novogfx_backend_ops(void) {
    return &kNovoGfxOps;
}
