/* novogfx.h — Public C API for NovoGFX (version 0.1.0)
 * License: MIT OR Apache-2.0
 *
 * Generated from `NovoGFX-API.idl`. The Rust implementation lives in
 * `capi/novogfx-c` and re-exports the safe Rust API from the `novogfx`
 * facade crate.
 *
 * Design notes:
 *   - All handles are opaque pointers. Never dereference them directly.
 *   - All functions return `novogfx_status_t` unless otherwise noted.
 *   - Strings are NUL-terminated UTF-8.
 *   - Every object with a `create`/`open` constructor has a matching
 *     `destroy`/`close` destructor.
 *   - Thread safety: a single novogfx_device_t may be used from one
 *     thread at a time. Use one device per thread for concurrency.
 */
#ifndef NOVOGFX_H
#define NOVOGFX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Versioning                                                          */
/* ------------------------------------------------------------------ */

/** ABI version as MAJOR*10000 + MINOR*100 + PATCH. */
unsigned long novogfx_abi_version(void);
/** Human-readable library version string, e.g. "0.1.0". Static, never freed. */
const char *novogfx_version_string(void);

/* ------------------------------------------------------------------ */
/* Status codes                                                        */
/* ------------------------------------------------------------------ */

typedef uint32_t novogfx_status_t;
#define NOVOGFX_OK                    0u
#define NOVOGFX_ERR_INVALID_ARGUMENT  1u
#define NOVOGFX_ERR_IO                2u
#define NOVOGFX_ERR_NOT_FOUND         3u
#define NOVOGFX_ERR_NOT_SUPPORTED     4u
#define NOVOGFX_ERR_PERMISSION_DENIED 5u
#define NOVOGFX_ERR_BUSY              6u
#define NOVOGFX_ERR_AGAIN             7u
#define NOVOGFX_ERR_OUT_OF_MEMORY     8u
#define NOVOGFX_ERR_DEVICE_LOST       9u
#define NOVOGFX_ERR_CANCELLED         10u
#define NOVOGFX_ERR_INTERNAL          11u

/** Static, human-readable string for a status code. Never freed. */
const char *novogfx_status_string(novogfx_status_t status);

/* ------------------------------------------------------------------ */
/* Opaque handles                                                      */
/* ------------------------------------------------------------------ */

typedef struct novogfx_device_t novogfx_device_t;
typedef struct novogfx_connector_t novogfx_connector_t;
typedef struct novogfx_crtc_t novogfx_crtc_t;
typedef struct novogfx_encoder_t novogfx_encoder_t;
typedef struct novogfx_plane_t novogfx_plane_t;
typedef struct novogfx_framebuffer_t novogfx_framebuffer_t;
typedef struct novogfx_dumb_buffer_t novogfx_dumb_buffer_t;
typedef struct novogfx_atomic_request_t novogfx_atomic_request_t;
typedef struct novogfx_event_reader_t novogfx_event_reader_t;
typedef struct novogfx_blob_t novogfx_blob_t;
typedef struct novogfx_property_t novogfx_property_t;

/* ------------------------------------------------------------------ */
/* Device                                                              */
/* ------------------------------------------------------------------ */

#define NOVOGFX_CAP_ATOMIC         (1ul << 0)
#define NOVOGFX_CAP_UNIVERSAL_PLANES (1ul << 1)
#define NOVOGFX_CAP_DUMB_BUFFER    (1ul << 2)
#define NOVOGFX_CAP_PRIME          (1ul << 3)
#define NOVOGFX_CAP_ASYNC_PAGE_FLIP (1ul << 4)

typedef struct novogfx_device_info_t {
    char *driver_name;
    char *driver_date;
    char *driver_desc;
    unsigned long driver_major;
    unsigned long driver_minor;
    unsigned long driver_patch;
    unsigned long bus_type;
    unsigned long device_id;
    unsigned long vendor_id;
} novogfx_device_info_t;

novogfx_status_t novogfx_device_open(const char *path, novogfx_device_t **out_device);
novogfx_status_t novogfx_device_get_info(novogfx_device_t *device, novogfx_device_info_t *out_info);
unsigned long novogfx_device_caps(const novogfx_device_t *device);
novogfx_status_t novogfx_device_enable_atomic(const novogfx_device_t *device);
int novogfx_device_fd(const novogfx_device_t *device);
void novogfx_device_destroy(novogfx_device_t *device);

/* ------------------------------------------------------------------ */
/* Resources                                                           */
/* ------------------------------------------------------------------ */

typedef struct novogfx_resources_t {
    unsigned long num_connectors;
    unsigned long num_crtcs;
    unsigned long num_encoders;
    unsigned long num_planes;
    unsigned long num_framebuffers;
    novogfx_connector_t **connectors;
    novogfx_crtc_t **crtcs;
    novogfx_encoder_t **encoders;
    novogfx_plane_t **planes;
    novogfx_framebuffer_t **framebuffers;
} novogfx_resources_t;

/* Lists own their arrays and handles. Overlapping lists share handle identity;
 * a shared handle remains live until its last owning list is destroyed.
 * Do not pass these borrowed handles to individual handle destructors. */
novogfx_status_t novogfx_device_get_resources(const novogfx_device_t *device, novogfx_resources_t **out_resources);
void novogfx_resources_destroy(novogfx_resources_t *resources);

/* ------------------------------------------------------------------ */
/* Connectors                                                          */
/* ------------------------------------------------------------------ */

typedef uint32_t novogfx_connector_status_t;
#define NOVOGFX_CONNECTOR_CONNECTED    0u
#define NOVOGFX_CONNECTOR_DISCONNECTED 1u
#define NOVOGFX_CONNECTOR_UNKNOWN      2u

typedef uint32_t novogfx_connector_type_t;
/* The IDL uses UNKNOWN for both status (2) and type (0). Keep the status
 * spelling compatible and disambiguate the type's zero value explicitly. */
#define NOVOGFX_CONNECTOR_TYPE_UNKNOWN 0u
#define NOVOGFX_CONNECTOR_VGA 1u
#define NOVOGFX_CONNECTOR_DVI_I 2u
#define NOVOGFX_CONNECTOR_DVI_D 3u
#define NOVOGFX_CONNECTOR_DVI_A 4u
#define NOVOGFX_CONNECTOR_COMPOSITE 5u
#define NOVOGFX_CONNECTOR_SVIDEO 6u
#define NOVOGFX_CONNECTOR_LVDS 7u
#define NOVOGFX_CONNECTOR_COMPONENT 8u
#define NOVOGFX_CONNECTOR_9PIN_DIN 9u
#define NOVOGFX_CONNECTOR_DISPLAYPORT 10u
#define NOVOGFX_CONNECTOR_HDMI_A 11u
#define NOVOGFX_CONNECTOR_HDMI_B 12u
#define NOVOGFX_CONNECTOR_TV 13u
#define NOVOGFX_CONNECTOR_EDP 14u
#define NOVOGFX_CONNECTOR_VIRTUAL 15u
#define NOVOGFX_CONNECTOR_DSI 16u
#define NOVOGFX_CONNECTOR_DPI 17u

typedef struct novogfx_mode_t {
    unsigned long clock_khz;
    unsigned short hdisplay;
    unsigned short hsync_start;
    unsigned short hsync_end;
    unsigned short htotal;
    unsigned short hskew;
    unsigned short vdisplay;
    unsigned short vsync_start;
    unsigned short vsync_end;
    unsigned short vtotal;
    unsigned short vscan;
    unsigned long flags;
    unsigned long type;
    const char *name;
} novogfx_mode_t;

novogfx_status_t novogfx_connector_status(const novogfx_connector_t *connector, novogfx_connector_status_t *out_status);
novogfx_connector_type_t novogfx_connector_type(const novogfx_connector_t *connector);
unsigned long novogfx_connector_num_modes(const novogfx_connector_t *connector);
const novogfx_mode_t *novogfx_connector_mode(const novogfx_connector_t *connector, unsigned long index);
const novogfx_mode_t *novogfx_connector_preferred_mode(const novogfx_connector_t *connector);
novogfx_crtc_t *novogfx_connector_current_crtc(const novogfx_connector_t *connector);
const uint8_t *novogfx_connector_edid(const novogfx_connector_t *connector, unsigned long *out_len);
unsigned long novogfx_connector_id(const novogfx_connector_t *connector);
novogfx_connector_t *novogfx_device_find_connected_connector(const novogfx_device_t *device);
void novogfx_connector_destroy(novogfx_connector_t *connector);

/* ------------------------------------------------------------------ */
/* CRTCs / planes / formats                                            */
/* ------------------------------------------------------------------ */

unsigned long novogfx_crtc_id(const novogfx_crtc_t *crtc);
novogfx_framebuffer_t *novogfx_crtc_current_framebuffer(const novogfx_crtc_t *crtc);
const novogfx_mode_t *novogfx_crtc_current_mode(const novogfx_crtc_t *crtc);
void novogfx_crtc_destroy(novogfx_crtc_t *crtc);

typedef uint32_t novogfx_plane_type_t;
#define NOVOGFX_PLANE_PRIMARY 0u
#define NOVOGFX_PLANE_CURSOR  1u
#define NOVOGFX_PLANE_OVERLAY 2u

unsigned long novogfx_plane_id(const novogfx_plane_t *plane);
novogfx_plane_type_t novogfx_plane_type(const novogfx_plane_t *plane);
novogfx_crtc_t *novogfx_plane_current_crtc(const novogfx_plane_t *plane);
int novogfx_plane_supports_format(const novogfx_plane_t *plane, unsigned long drm_format);
void novogfx_plane_destroy(novogfx_plane_t *plane);

const char *novogfx_format_name(unsigned long format);

/* DRM fourcc values from NovoGFX-API.idl. */
#define NOVOGFX_FORMAT_RGB332 0x38424752ul
#define NOVOGFX_FORMAT_BGR233 0x38524742ul
#define NOVOGFX_FORMAT_XRGB4444 0x32315258ul
#define NOVOGFX_FORMAT_XBGR4444 0x32314258ul
#define NOVOGFX_FORMAT_RGBX4444 0x32315852ul
#define NOVOGFX_FORMAT_BGRX4444 0x32315842ul
#define NOVOGFX_FORMAT_ARGB4444 0x32315241ul
#define NOVOGFX_FORMAT_ABGR4444 0x32314241ul
#define NOVOGFX_FORMAT_RGBA4444 0x32314152ul
#define NOVOGFX_FORMAT_BGRA4444 0x32314142ul
#define NOVOGFX_FORMAT_XRGB1555 0x35315258ul
#define NOVOGFX_FORMAT_XBGR1555 0x35314258ul
#define NOVOGFX_FORMAT_RGBX5551 0x35315852ul
#define NOVOGFX_FORMAT_BGRX5551 0x35315842ul
#define NOVOGFX_FORMAT_ARGB1555 0x35315241ul
#define NOVOGFX_FORMAT_ABGR1555 0x35314241ul
#define NOVOGFX_FORMAT_RGBA5551 0x35314152ul
#define NOVOGFX_FORMAT_BGRA5551 0x35314142ul
#define NOVOGFX_FORMAT_RGB565 0x36314752ul
#define NOVOGFX_FORMAT_BGR565 0x36314742ul
#define NOVOGFX_FORMAT_RGB888 0x34324752ul
#define NOVOGFX_FORMAT_BGR888 0x34324742ul
#define NOVOGFX_FORMAT_XRGB8888 0x34325258ul
#define NOVOGFX_FORMAT_XBGR8888 0x34324258ul
#define NOVOGFX_FORMAT_RGBX8888 0x34325852ul
#define NOVOGFX_FORMAT_BGRX8888 0x34325842ul
#define NOVOGFX_FORMAT_ARGB8888 0x34325241ul
#define NOVOGFX_FORMAT_ABGR8888 0x34324241ul
#define NOVOGFX_FORMAT_RGBA8888 0x34324152ul
#define NOVOGFX_FORMAT_BGRA8888 0x34324142ul
#define NOVOGFX_FORMAT_XRGB2101010 0x30335258ul
#define NOVOGFX_FORMAT_XBGR2101010 0x30334258ul
#define NOVOGFX_FORMAT_RGBX1010102 0x30335852ul
#define NOVOGFX_FORMAT_BGRX1010102 0x30335842ul
#define NOVOGFX_FORMAT_ARGB2101010 0x30335241ul
#define NOVOGFX_FORMAT_ABGR2101010 0x30334241ul
#define NOVOGFX_FORMAT_RGBA1010102 0x30334152ul
#define NOVOGFX_FORMAT_BGRA1010102 0x30334142ul
#define NOVOGFX_FORMAT_YUYV 0x56595559ul
#define NOVOGFX_FORMAT_YVYU 0x55595659ul
#define NOVOGFX_FORMAT_UYVY 0x59565955ul
#define NOVOGFX_FORMAT_VYUY 0x59555956ul
#define NOVOGFX_FORMAT_NV12 0x3231564eul
#define NOVOGFX_FORMAT_NV21 0x3132564eul
#define NOVOGFX_FORMAT_NV16 0x3631564eul
#define NOVOGFX_FORMAT_NV61 0x3136564eul
#define NOVOGFX_FORMAT_YUV420 0x32315559ul
#define NOVOGFX_FORMAT_YUV422 0x36315559ul
#define NOVOGFX_FORMAT_YUV444 0x34325559ul

/* ------------------------------------------------------------------ */
/* Dumb buffers / framebuffers                                         */
/* ------------------------------------------------------------------ */

typedef struct novogfx_dumb_buffer_desc_t {
    unsigned long width;
    unsigned long height;
    unsigned long bpp;
    unsigned long flags;
} novogfx_dumb_buffer_desc_t;

typedef struct novogfx_dumb_buffer_info_t {
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned long size;
    unsigned long handle;
} novogfx_dumb_buffer_info_t;

novogfx_status_t novogfx_dumb_buffer_create(novogfx_device_t *device, const novogfx_dumb_buffer_desc_t *desc, novogfx_dumb_buffer_t **out_buffer, novogfx_framebuffer_t **out_fb);
novogfx_status_t novogfx_dumb_buffer_get_info(const novogfx_dumb_buffer_t *buffer, novogfx_dumb_buffer_info_t *out_info);
/* CPU views expire at unmap or destruction. Repeated unmap is harmless;
 * remapping preserves the GEM buffer contents and framebuffer identity. */
novogfx_status_t novogfx_dumb_buffer_map(novogfx_dumb_buffer_t *buffer, uint8_t **out_ptr);
void novogfx_dumb_buffer_unmap(novogfx_dumb_buffer_t *buffer);
novogfx_framebuffer_t *novogfx_dumb_buffer_framebuffer(const novogfx_dumb_buffer_t *buffer);
void novogfx_dumb_buffer_destroy(novogfx_dumb_buffer_t *buffer);

unsigned long novogfx_framebuffer_id(const novogfx_framebuffer_t *fb);
void novogfx_framebuffer_destroy(novogfx_framebuffer_t *fb);

/* ------------------------------------------------------------------ */
/* Atomic modesetting                                                  */
/* ------------------------------------------------------------------ */

typedef struct novogfx_plane_update_t {
    novogfx_plane_t *plane;
    novogfx_framebuffer_t *fb;
    novogfx_crtc_t *crtc;
    unsigned long src_x;
    unsigned long src_y;
    unsigned long src_w;
    unsigned long src_h;
    long crtc_x;
    long crtc_y;
    unsigned long crtc_w;
    unsigned long crtc_h;
    unsigned long zpos;
    unsigned long flags;
} novogfx_plane_update_t;

#define NOVOGFX_COMMIT_NONE            0ul
#define NOVOGFX_PLANE_UPDATE_NONE 0ul
#define NOVOGFX_PLANE_UPDATE_FB 1ul
#define NOVOGFX_PLANE_UPDATE_CRTC 2ul
#define NOVOGFX_PLANE_UPDATE_SRC 4ul
#define NOVOGFX_PLANE_UPDATE_DST 8ul
#define NOVOGFX_PLANE_UPDATE_ZPOS 16ul
#define NOVOGFX_PLANE_UPDATE_ALL 0xfffffffful
#define NOVOGFX_COMMIT_ALLOW_MODESET   (1ul << 0)
#define NOVOGFX_COMMIT_NONBLOCK        (1ul << 1)
#define NOVOGFX_COMMIT_PAGE_FLIP_EVENT (1ul << 2)
#define NOVOGFX_COMMIT_TEST_ONLY       (1ul << 3)

/* Enable atomic first. Every selected object must belong to this device. */
novogfx_status_t novogfx_atomic_request_create(novogfx_device_t *device, novogfx_atomic_request_t **out_request);
novogfx_status_t novogfx_atomic_request_set_crtc(novogfx_atomic_request_t *request, novogfx_crtc_t *crtc, novogfx_framebuffer_t *fb, const novogfx_mode_t *mode, novogfx_connector_t *connector);
novogfx_status_t novogfx_atomic_request_add_plane(novogfx_atomic_request_t *request, const novogfx_plane_update_t *update);
novogfx_status_t novogfx_atomic_request_set_connector_crtc(novogfx_atomic_request_t *request, novogfx_connector_t *connector, novogfx_crtc_t *crtc);
novogfx_status_t novogfx_atomic_request_commit(novogfx_atomic_request_t *request, unsigned long flags);
void novogfx_atomic_request_reset(novogfx_atomic_request_t *request);
void novogfx_atomic_request_destroy(novogfx_atomic_request_t *request);

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

typedef uint32_t novogfx_event_type_t;
#define NOVOGFX_EVENT_VBLANK    0u
#define NOVOGFX_EVENT_PAGE_FLIP 1u
#define NOVOGFX_EVENT_HOTPLUG   2u

typedef struct novogfx_event_t {
    novogfx_event_type_t type;
    unsigned long sequence;
    unsigned long tv_sec;
    unsigned long tv_usec;
    unsigned long crtc_id;
    unsigned long user_data;
    int flip_status;
    unsigned long connector_id;
} novogfx_event_t;

novogfx_status_t novogfx_event_reader_create(novogfx_device_t *device, novogfx_event_reader_t **out_reader);
/* Returned events borrow the reader until its next poll/destruction. Failures
 * clear both outputs. CRTC-sequence notifications appear as VBLANK events. */
novogfx_status_t novogfx_event_reader_poll(novogfx_event_reader_t *reader, novogfx_event_t **out_events, unsigned long *out_count);
void novogfx_event_reader_destroy(novogfx_event_reader_t *reader);

/* ------------------------------------------------------------------ */
/* Page flip / memory helpers                                          */
/* ------------------------------------------------------------------ */

novogfx_status_t novogfx_crtc_page_flip(novogfx_crtc_t *crtc, novogfx_framebuffer_t *fb, unsigned long flags, unsigned long user_data);
void novogfx_string_free(char *s);
void novogfx_buffer_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* NOVOGFX_H */
