//! Raw `extern "C"` declarations for `libdrm` (`xf86drm.h`, `xf86drmMode.h`).
//!
//! These are thin declarations over the C library. Every call is `unsafe`
//! and every pointer must be validated by the caller (see the safe wrappers
//! in `novogfx-core` and above for the audited call sites).

use std::os::raw::{c_char, c_int, c_uint, c_ulong, c_void};

use super::drm_mode::DRM_DISPLAY_MODE_LEN;
use super::drm_mode::DRM_PROP_NAME_LEN;

/// Opaque atomic request type (`drmModeAtomicReqPtr`).
#[repr(C)]
pub struct drmModeAtomicReq {
    _private: [u8; 0],
}

/// Userspace resource list (`drmModeRes`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeRes {
    /// Number of framebuffers.
    pub count_fbs: c_int,
    /// Framebuffer ids.
    pub fbs: *mut u32,
    /// Number of CRTCs.
    pub count_crtcs: c_int,
    /// CRTC ids.
    pub crtcs: *mut u32,
    /// Number of connectors.
    pub count_connectors: c_int,
    /// Connector ids.
    pub connectors: *mut u32,
    /// Number of encoders.
    pub count_encoders: c_int,
    /// Encoder ids.
    pub encoders: *mut u32,
    /// Minimum width.
    pub min_width: u32,
    /// Maximum width.
    pub max_width: u32,
    /// Minimum height.
    pub min_height: u32,
    /// Maximum height.
    pub max_height: u32,
}

/// Userspace mode info (`drmModeModeInfo`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeModeInfo {
    /// Pixel clock in kHz.
    pub clock: u32,
    /// Horizontal display size.
    pub hdisplay: u16,
    /// Horizontal sync start.
    pub hsync_start: u16,
    /// Horizontal sync end.
    pub hsync_end: u16,
    /// Horizontal total.
    pub htotal: u16,
    /// Horizontal skew.
    pub hskew: u16,
    /// Vertical display size.
    pub vdisplay: u16,
    /// Vertical sync start.
    pub vsync_start: u16,
    /// Vertical sync end.
    pub vsync_end: u16,
    /// Vertical total.
    pub vtotal: u16,
    /// Vertical scan.
    pub vscan: u16,
    /// Approximate refresh rate.
    pub vrefresh: u32,
    /// Flags.
    pub flags: u32,
    /// Type.
    pub type_: u32,
    /// Name.
    pub name: [c_char; DRM_DISPLAY_MODE_LEN],
}

/// Userspace framebuffer (`drmModeFB2`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeFB2 {
    /// Framebuffer id.
    pub fb_id: u32,
    /// Width.
    pub width: u32,
    /// Height.
    pub height: u32,
    /// Fourcc pixel format.
    pub pixel_format: u32,
    /// Modifier.
    pub modifier: u64,
    /// Flags.
    pub flags: u32,
    /// GEM handles.
    pub handles: [u32; 4],
    /// Pitches.
    pub pitches: [u32; 4],
    /// Offsets.
    pub offsets: [u32; 4],
}

/// Userspace property blob (`drmModePropertyBlobRes`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModePropertyBlobRes {
    /// Blob id.
    pub id: u32,
    /// Length in bytes.
    pub length: u32,
    /// Data pointer.
    pub data: *mut c_void,
}

/// Userspace property (`drmModePropertyRes`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModePropertyRes {
    /// Property id.
    pub prop_id: u32,
    /// Flags (`DRM_MODE_PROP_*`).
    pub flags: u32,
    /// Name.
    pub name: [c_char; DRM_PROP_NAME_LEN],
    /// Number of values.
    pub count_values: c_int,
    /// Values / blob lengths.
    pub values: *mut u64,
    /// Number of enums.
    pub count_enums: c_int,
    /// Enum entries.
    pub enums: *mut super::drm_mode::drm_mode_property_enum,
    /// Number of blobs.
    pub count_blobs: c_int,
    /// Blob ids.
    pub blob_ids: *mut u32,
}

/// Userspace CRTC (`drmModeCrtc`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeCrtc {
    /// CRTC id.
    pub crtc_id: u32,
    /// Framebuffer id (0 = disconnected).
    pub buffer_id: u32,
    /// X position.
    pub x: u32,
    /// Y position.
    pub y: u32,
    /// Width.
    pub width: u32,
    /// Height.
    pub height: u32,
    /// Whether `mode` is valid.
    pub mode_valid: c_int,
    /// Current mode.
    pub mode: drmModeModeInfo,
    /// Gamma size.
    pub gamma_size: c_int,
}

/// Userspace encoder (`drmModeEncoder`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeEncoder {
    /// Encoder id.
    pub encoder_id: u32,
    /// Encoder type.
    pub encoder_type: u32,
    /// Bound CRTC id.
    pub crtc_id: u32,
    /// Possible CRTCs bitmask.
    pub possible_crtcs: u32,
    /// Possible clones bitmask.
    pub possible_clones: u32,
}

/// Userspace connector (`drmModeConnector`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeConnector {
    /// Connector id.
    pub connector_id: u32,
    /// Bound encoder id.
    pub encoder_id: u32,
    /// Connector type.
    pub connector_type: u32,
    /// Per-type id.
    pub connector_type_id: u32,
    /// Connection status (1/2/3).
    pub connection: c_int,
    /// Width in mm.
    pub mm_width: u32,
    /// Height in mm.
    pub mm_height: u32,
    /// Subpixel order.
    pub subpixel: c_int,
    /// Number of modes.
    pub count_modes: c_int,
    /// Modes array.
    pub modes: *mut drmModeModeInfo,
    /// Number of properties.
    pub count_props: c_int,
    /// Property ids.
    pub props: *mut u32,
    /// Property values.
    pub prop_values: *mut u64,
    /// Number of encoders.
    pub count_encoders: c_int,
    /// Encoder ids.
    pub encoders: *mut u32,
}

/// Generic object properties (`drmModeObjectProperties`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModeObjectProperties {
    /// Number of properties.
    pub count_props: u32,
    /// Property ids.
    pub props: *mut u32,
    /// Property values.
    pub prop_values: *mut u64,
}

/// Userspace plane (`drmModePlane`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModePlane {
    /// Number of formats.
    pub count_formats: u32,
    /// Supported formats.
    pub formats: *mut u32,
    /// Plane id.
    pub plane_id: u32,
    /// Bound CRTC id.
    pub crtc_id: u32,
    /// Bound framebuffer id.
    pub fb_id: u32,
    /// CRTC x.
    pub crtc_x: u32,
    /// CRTC y.
    pub crtc_y: u32,
    /// X.
    pub x: u32,
    /// Y.
    pub y: u32,
    /// Possible CRTCs bitmask.
    pub possible_crtcs: u32,
    /// Gamma size.
    pub gamma_size: u32,
}

/// Plane resource list (`drmModePlaneRes`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmModePlaneRes {
    /// Number of planes.
    pub count_planes: u32,
    /// Plane ids.
    pub planes: *mut u32,
}

/// Version block (`drmVersion`).
///
/// Layout matches `xf86drm.h`: all lengths are `int` (not `size_t`).
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drmVersion {
    /// Major.
    pub version_major: c_int,
    /// Minor.
    pub version_minor: c_int,
    /// Patch level.
    pub version_patchlevel: c_int,
    /// Name length.
    pub name_len: c_int,
    /// Name.
    pub name: *mut c_char,
    /// Date length.
    pub date_len: c_int,
    /// Date.
    pub date: *mut c_char,
    /// Description length.
    pub desc_len: c_int,
    /// Description.
    pub desc: *mut c_char,
}

unsafe extern "C" {
    /// Call `drmIoctl(fd, request, arg)`.
    pub fn drmIoctl(fd: c_int, request: c_ulong, arg: *mut c_void) -> c_int;
    /// Return a `drmVersion` for `fd`, or NULL on error.
    pub fn drmGetVersion(fd: c_int) -> *mut drmVersion;
    /// Free a `drmVersion`.
    pub fn drmFreeVersion(v: *mut drmVersion);
    /// Query a device capability.
    pub fn drmGetCap(fd: c_int, capability: u64, value: *mut u64) -> c_int;
    /// Set a client capability.
    pub fn drmSetClientCap(fd: c_int, capability: u64, value: u64) -> c_int;
    /// Convert a PRIME fd to a GEM handle.
    pub fn drmPrimeFDToHandle(fd: c_int, prime_fd: c_int, handle: *mut u32) -> c_int;
    /// Convert a GEM handle to a PRIME fd.
    pub fn drmPrimeHandleToFD(fd: c_int, handle: u32, flags: u32, prime_fd: *mut c_int) -> c_int;

    /// Retrieve KMS resources.
    pub fn drmModeGetResources(fd: c_int) -> *mut drmModeRes;
    /// Free KMS resources.
    pub fn drmModeFreeResources(res: *mut drmModeRes);
    /// Retrieve a connector.
    pub fn drmModeGetConnector(fd: c_int, connector_id: u32) -> *mut drmModeConnector;
    /// Free a connector.
    pub fn drmModeFreeConnector(conn: *mut drmModeConnector);
    /// Retrieve an encoder.
    pub fn drmModeGetEncoder(fd: c_int, encoder_id: u32) -> *mut drmModeEncoder;
    /// Free an encoder.
    pub fn drmModeFreeEncoder(enc: *mut drmModeEncoder);
    /// Retrieve a CRTC.
    pub fn drmModeGetCrtc(fd: c_int, crtc_id: u32) -> *mut drmModeCrtc;
    /// Free a CRTC.
    pub fn drmModeFreeCrtc(crtc: *mut drmModeCrtc);
    /// Retrieve plane resources.
    pub fn drmModeGetPlaneResources(fd: c_int) -> *mut drmModePlaneRes;
    /// Free plane resources.
    pub fn drmModeFreePlaneResources(res: *mut drmModePlaneRes);
    /// Retrieve a plane.
    pub fn drmModeGetPlane(fd: c_int, plane_id: u32) -> *mut drmModePlane;
    /// Free a plane.
    pub fn drmModeFreePlane(plane: *mut drmModePlane);
    /// Retrieve a property.
    pub fn drmModeGetProperty(fd: c_int, property_id: u32) -> *mut drmModePropertyRes;
    /// Free a property.
    pub fn drmModeFreeProperty(prop: *mut drmModePropertyRes);
    /// Retrieve a property blob.
    pub fn drmModeGetPropertyBlob(fd: c_int, blob_id: u32) -> *mut drmModePropertyBlobRes;
    /// Free a property blob.
    pub fn drmModeFreePropertyBlob(blob: *mut drmModePropertyBlobRes);
    /// Retrieve generic object properties.
    pub fn drmModeObjectGetProperties(
        fd: c_int,
        object_id: u32,
        object_type: u32,
    ) -> *mut drmModeObjectProperties;
    /// Free generic object properties.
    pub fn drmModeFreeObjectProperties(props: *mut drmModeObjectProperties);

    /// Allocate an atomic request.
    pub fn drmModeAtomicAlloc() -> *mut drmModeAtomicReq;
    /// Free an atomic request.
    pub fn drmModeAtomicFree(req: *mut drmModeAtomicReq);
    /// Add a property to an atomic request.
    pub fn drmModeAtomicAddProperty(
        req: *mut drmModeAtomicReq,
        object_id: u32,
        property_id: u32,
        value: u64,
    ) -> c_int;
    /// Commit an atomic request.
    pub fn drmModeAtomicCommit(
        fd: c_int,
        req: *mut drmModeAtomicReq,
        flags: u32,
        user_data: *mut c_void,
    ) -> c_int;
    /// Create a property blob.
    pub fn drmModeCreatePropertyBlob(
        fd: c_int,
        data: *const c_void,
        size: usize,
        id: *mut u32,
    ) -> c_int;
    /// Destroy a property blob.
    pub fn drmModeDestroyPropertyBlob(fd: c_int, id: u32) -> c_int;

    /// Add a framebuffer (v2).
    pub fn drmModeAddFB2(
        fd: c_int,
        width: u32,
        height: u32,
        pixel_format: u32,
        bo_handles: *const u32,
        pitches: *const u32,
        offsets: *const u32,
        buf_id: *mut u32,
        flags: u32,
    ) -> c_int;
    /// Remove a framebuffer.
    pub fn drmModeRmFB(fd: c_int, buffer_id: u32) -> c_int;
    /// Retrieve a framebuffer (v2).
    pub fn drmModeGetFB2(fd: c_int, buffer_id: u32) -> *mut drmModeFB2;
    /// Free a framebuffer.
    pub fn drmModeFreeFB2(fb: *mut drmModeFB2);
    /// Legacy page flip.
    pub fn drmModePageFlip(
        fd: c_int,
        crtc_id: u32,
        fb_id: u32,
        flags: u32,
        user_data: *mut c_void,
    ) -> c_int;

    /// Create a dumb buffer.
    pub fn drmModeCreateDumbBuffer(
        fd: c_int,
        width: u32,
        height: u32,
        bpp: u32,
        flags: u32,
        handle: *mut u32,
        pitch: *mut u32,
        size: *mut u64,
    ) -> c_int;
    /// Destroy a dumb buffer.
    pub fn drmModeDestroyDumbBuffer(fd: c_int, handle: u32) -> c_int;
    /// Prepare a dumb buffer for mapping.
    pub fn drmModeMapDumbBuffer(fd: c_int, handle: u32, offset: *mut u64) -> c_int;

    /// Connector type name helper (libdrm utility).
    pub fn drmModeGetConnectorTypeName(connector_type: c_uint) -> *const c_char;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn struct_sizes_are_nonzero() {
        assert!(std::mem::size_of::<drmModeRes>() > 0);
        assert!(std::mem::size_of::<drmModeConnector>() > 0);
        assert!(std::mem::size_of::<drmModeCrtc>() > 0);
        assert!(std::mem::size_of::<drmModePlane>() > 0);
        assert!(std::mem::size_of::<drmModeModeInfo>() > 0);
    }

    #[test]
    fn drm_version_layout_matches_xf86drm_h() {
        // xf86drm.h uses `int` for all lengths (not size_t). With the wrong
        // `usize` widths this struct grows and every pointer field shifts,
        // segfaulting `drmGetVersion` (regression: amdgpu card1 crashed).
        assert_eq!(std::mem::size_of::<drmVersion>(), 56);
        assert_eq!(
            std::mem::offset_of!(drmVersion, name),
            16,
            "name pointer must follow 4×int (12+4)"
        );
    }
}
