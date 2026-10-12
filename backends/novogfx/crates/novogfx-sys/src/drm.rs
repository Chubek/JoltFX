//! Raw bindings for `include/drm/drm.h`.
//!
//! Only integer handles, constants, and `#[repr(C)]` structs are exposed.
//! All values match the vendored `third_party/libdrm` headers and the
//! system `libdrm` 2.4.x ABI.

use std::os::raw::{c_char, c_int, c_ulong};

/// Capability id for dumb buffers (`DRM_CAP_DUMB_BUFFER`).
pub const DRM_CAP_DUMB_BUFFER: u64 = 0x1;
/// Capability id for high-crtc vblank (`DRM_CAP_VBLANK_HIGH_CRTC`).
pub const DRM_CAP_VBLANK_HIGH_CRTC: u64 = 0x2;
/// Capability id for dumb preferred depth (`DRM_CAP_DUMB_PREFERRED_DEPTH`).
pub const DRM_CAP_DUMB_PREFERRED_DEPTH: u64 = 0x3;
/// Capability id for dumb shadow preference (`DRM_CAP_DUMB_PREFER_SHADOW`).
pub const DRM_CAP_DUMB_PREFER_SHADOW: u64 = 0x4;
/// Capability id for PRIME dma-buf support (`DRM_CAP_PRIME`).
pub const DRM_CAP_PRIME: u64 = 0x5;
/// Capability id for monotonic timestamps (`DRM_CAP_TIMESTAMP_MONOTONIC`).
pub const DRM_CAP_TIMESTAMP_MONOTONIC: u64 = 0x6;
/// Capability id for async page flip (`DRM_CAP_ASYNC_PAGE_FLIP`).
pub const DRM_CAP_ASYNC_PAGE_FLIP: u64 = 0x7;
/// Capability id for cursor width (`DRM_CAP_CURSOR_WIDTH`).
pub const DRM_CAP_CURSOR_WIDTH: u64 = 0x8;
/// Capability id for cursor height (`DRM_CAP_CURSOR_HEIGHT`).
pub const DRM_CAP_CURSOR_HEIGHT: u64 = 0x9;
/// Capability id for ADDFB2 modifiers (`DRM_CAP_ADDFB2_MODIFIERS`).
pub const DRM_CAP_ADDFB2_MODIFIERS: u64 = 0x10;
/// Capability id for page-flip targets (`DRM_CAP_PAGE_FLIP_TARGET`).
pub const DRM_CAP_PAGE_FLIP_TARGET: u64 = 0x11;
/// Capability id for CRTC in vblank events (`DRM_CAP_CRTC_IN_VBLANK_EVENT`).
pub const DRM_CAP_CRTC_IN_VBLANK_EVENT: u64 = 0x12;

/// Client capability: stereo 3D (`DRM_CLIENT_CAP_STEREO_3D`).
pub const DRM_CLIENT_CAP_STEREO_3D: u64 = 1;
/// Client capability: universal planes (`DRM_CLIENT_CAP_UNIVERSAL_PLANES`).
pub const DRM_CLIENT_CAP_UNIVERSAL_PLANES: u64 = 2;
/// Client capability: atomic modesetting (`DRM_CLIENT_CAP_ATOMIC`).
pub const DRM_CLIENT_CAP_ATOMIC: u64 = 3;
/// Client capability: aspect ratio (`DRM_CLIENT_CAP_ASPECT_RATIO`).
pub const DRM_CLIENT_CAP_ASPECT_RATIO: u64 = 4;
/// Client capability: writeback connectors.
pub const DRM_CLIENT_CAP_WRITEBACK_CONNECTORS: u64 = 5;

/// Generic DRM event: vertical blanking.
pub const DRM_EVENT_VBLANK: u32 = 0x01;
/// Generic DRM event: page-flip completion.
pub const DRM_EVENT_FLIP_COMPLETE: u32 = 0x02;
/// Generic DRM event: CRTC sequence.
pub const DRM_EVENT_CRTC_SEQUENCE: u32 = 0x03;

/// Argument for `DRM_IOCTL_VERSION`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_version {
    /// Major version.
    pub version_major: c_int,
    /// Minor version.
    pub version_minor: c_int,
    /// Patch level.
    pub version_patchlevel: c_int,
    /// Length of the name buffer.
    pub name_len: usize,
    /// Driver name buffer.
    pub name: *mut c_char,
    /// Length of the date buffer.
    pub date_len: usize,
    /// Driver date buffer.
    pub date: *mut c_char,
    /// Length of the desc buffer.
    pub desc_len: usize,
    /// Driver description buffer.
    pub desc: *mut c_char,
}

/// Argument for `DRM_IOCTL_GET_CAP`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_get_cap {
    /// Capability id (one of `DRM_CAP_*`).
    pub capability: u64,
    /// Returned value.
    pub value: u64,
}

/// Argument for `DRM_IOCTL_SET_CLIENT_CAP`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_set_client_cap {
    /// Capability id (one of `DRM_CLIENT_CAP_*`).
    pub capability: u64,
    /// Value to set (0 or 1).
    pub value: u64,
}

/// PRIME handle conversion argument.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_prime_handle {
    /// GEM handle.
    pub handle: u32,
    /// Flags (only for handle-to-fd).
    pub flags: u32,
    /// PRIME fd (for fd-to-handle input / handle-to-fd output).
    pub fd: c_int,
}

/// Header for DRM events read from the device fd.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_event {
    /// Event type (`DRM_EVENT_*`).
    pub type_: u32,
    /// Total length in bytes including this header.
    pub length: u32,
}

/// Payload for vblank and page-flip-complete events.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_event_vblank {
    /// Generic header.
    pub base: drm_event,
    /// User data passed at request time.
    pub user_data: u64,
    /// Seconds component of the timestamp.
    pub tv_sec: u32,
    /// Microseconds component of the timestamp.
    pub tv_usec: u32,
    /// Vblank sequence number.
    pub sequence: u32,
    /// CRTC id (0 on older kernels).
    pub crtc_id: u32,
}

/// Payload for CRTC sequence events.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_event_crtc_sequence {
    /// Generic header.
    pub base: drm_event,
    /// User data passed at request time.
    pub user_data: u64,
    /// Timestamp in nanoseconds.
    pub time_ns: i64,
    /// Sequence number.
    pub sequence: u64,
}

/// Request a 32-bit sequence number size at compile time.
#[allow(dead_code)]
const _: () = {
    let _ = std::mem::size_of::<c_ulong>();
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn event_vblank_layout_matches_kernel() {
        // struct drm_event_vblank {
        //   struct drm_event { u32, u32 } (8) + u64 (8) + u32*4 (16) = 32 bytes.
        assert_eq!(std::mem::size_of::<drm_event>(), 8);
        assert_eq!(std::mem::size_of::<drm_event_vblank>(), 32);
    }

    #[test]
    fn cap_constants_match_libdrm() {
        assert_eq!(DRM_CAP_DUMB_BUFFER, 0x1);
        assert_eq!(DRM_CAP_PRIME, 0x5);
        assert_eq!(DRM_CAP_ASYNC_PAGE_FLIP, 0x7);
        assert_eq!(DRM_CLIENT_CAP_ATOMIC, 3);
        assert_eq!(DRM_CLIENT_CAP_UNIVERSAL_PLANES, 2);
    }

    #[test]
    fn event_constants_match_libdrm() {
        assert_eq!(DRM_EVENT_VBLANK, 0x01);
        assert_eq!(DRM_EVENT_FLIP_COMPLETE, 0x02);
        assert_eq!(DRM_EVENT_CRTC_SEQUENCE, 0x03);
    }
}
