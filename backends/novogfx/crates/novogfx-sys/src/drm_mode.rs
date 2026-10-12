//! Raw bindings for `include/drm/drm_mode.h` and `drm_fourcc.h`.
//!
//! Integer handles, `DRM_MODE_*` constants, fourcc codes, and `#[repr(C)]`
//! kernel structs. No safe abstractions.

/// Length of a mode name (`DRM_DISPLAY_MODE_LEN`).
pub const DRM_DISPLAY_MODE_LEN: usize = 32;
/// Length of a property name (`DRM_PROP_NAME_LEN`).
pub const DRM_PROP_NAME_LEN: usize = 32;
/// Length of a connector name (`DRM_CONNECTOR_NAME_LEN`).
pub const DRM_CONNECTOR_NAME_LEN: usize = 32;

/// Mode type: builtin (deprecated).
pub const DRM_MODE_TYPE_BUILTIN: u32 = 1 << 0;
/// Mode type: preferred.
pub const DRM_MODE_TYPE_PREFERRED: u32 = 1 << 3;
/// Mode type: default (deprecated).
pub const DRM_MODE_TYPE_DEFAULT: u32 = 1 << 4;
/// Mode type: user defined.
pub const DRM_MODE_TYPE_USERDEF: u32 = 1 << 5;
/// Mode type: driver provided.
pub const DRM_MODE_TYPE_DRIVER: u32 = 1 << 6;

/// Connector status: connected.
pub const DRM_MODE_CONNECTED: u32 = 1;
/// Connector status: disconnected.
pub const DRM_MODE_DISCONNECTED: u32 = 2;
/// Connector status: unknown.
pub const DRM_MODE_UNKNOWNCONNECTION: u32 = 3;

/// Connector type ids (`DRM_MODE_CONNECTOR_*`).
pub const DRM_MODE_CONNECTOR_UNKNOWN: u32 = 0;
/// Connector type: VGA.
pub const DRM_MODE_CONNECTOR_VGA: u32 = 1;
/// Connector type: DVI-I.
pub const DRM_MODE_CONNECTOR_DVII: u32 = 2;
/// Connector type: DVI-D.
pub const DRM_MODE_CONNECTOR_DVID: u32 = 3;
/// Connector type: DVI-A.
pub const DRM_MODE_CONNECTOR_DVIA: u32 = 4;
/// Connector type: composite.
pub const DRM_MODE_CONNECTOR_COMPOSITE: u32 = 5;
/// Connector type: s-video.
pub const DRM_MODE_CONNECTOR_SVIDEO: u32 = 6;
/// Connector type: LVDS.
pub const DRM_MODE_CONNECTOR_LVDS: u32 = 7;
/// Connector type: component.
pub const DRM_MODE_CONNECTOR_COMPONENT: u32 = 8;
/// Connector type: 9-pin DIN.
pub const DRM_MODE_CONNECTOR_9PIN_DIN: u32 = 9;
/// Connector type: DisplayPort.
pub const DRM_MODE_CONNECTOR_DISPLAYPORT: u32 = 10;
/// Connector type: HDMI-A.
pub const DRM_MODE_CONNECTOR_HDMIA: u32 = 11;
/// Connector type: HDMI-B.
pub const DRM_MODE_CONNECTOR_HDMIB: u32 = 12;
/// Connector type: TV.
pub const DRM_MODE_CONNECTOR_TV: u32 = 13;
/// Connector type: eDP.
pub const DRM_MODE_CONNECTOR_EDP: u32 = 14;
/// Connector type: virtual.
pub const DRM_MODE_CONNECTOR_VIRTUAL: u32 = 15;
/// Connector type: DSI.
pub const DRM_MODE_CONNECTOR_DSI: u32 = 16;
/// Connector type: DPI.
pub const DRM_MODE_CONNECTOR_DPI: u32 = 17;
/// Connector type: writeback.
pub const DRM_MODE_CONNECTOR_WRITEBACK: u32 = 18;
/// Connector type: SPI.
pub const DRM_MODE_CONNECTOR_SPI: u32 = 19;
/// Connector type: USB.
pub const DRM_MODE_CONNECTOR_USB: u32 = 20;

/// Encoder type: none.
pub const DRM_MODE_ENCODER_NONE: u32 = 0;
/// Encoder type: DAC.
pub const DRM_MODE_ENCODER_DAC: u32 = 1;
/// Encoder type: TMDS.
pub const DRM_MODE_ENCODER_TMDS: u32 = 2;
/// Encoder type: LVDS.
pub const DRM_MODE_ENCODER_LVDS: u32 = 3;
/// Encoder type: TVDAC.
pub const DRM_MODE_ENCODER_TVDAC: u32 = 4;
/// Encoder type: virtual.
pub const DRM_MODE_ENCODER_VIRTUAL: u32 = 5;
/// Encoder type: DSI.
pub const DRM_MODE_ENCODER_DSI: u32 = 6;
/// Encoder type: DisplayPort MST.
pub const DRM_MODE_ENCODER_DPMST: u32 = 7;
/// Encoder type: DPI.
pub const DRM_MODE_ENCODER_DPI: u32 = 8;

/// Object type ids for generic properties.
pub const DRM_MODE_OBJECT_CRTC: u32 = 0xcccc_cccc;
/// Object type: connector.
pub const DRM_MODE_OBJECT_CONNECTOR: u32 = 0xc0c0_c0c0;
/// Object type: encoder.
pub const DRM_MODE_OBJECT_ENCODER: u32 = 0xe0e0_e0e0;
/// Object type: mode.
pub const DRM_MODE_OBJECT_MODE: u32 = 0xdede_dede;
/// Object type: property.
pub const DRM_MODE_OBJECT_PROPERTY: u32 = 0xb0b0_b0b0;
/// Object type: framebuffer.
pub const DRM_MODE_OBJECT_FB: u32 = 0xfbfb_fbfb;
/// Object type: blob.
pub const DRM_MODE_OBJECT_BLOB: u32 = 0xbbbb_bbbb;
/// Object type: plane.
pub const DRM_MODE_OBJECT_PLANE: u32 = 0xeeee_eeee;
/// Object type: any.
pub const DRM_MODE_OBJECT_ANY: u32 = 0;

/// Property flag: pending (deprecated).
pub const DRM_MODE_PROP_PENDING: u32 = 1 << 0;
/// Property flag: range.
pub const DRM_MODE_PROP_RANGE: u32 = 1 << 1;
/// Property flag: immutable.
pub const DRM_MODE_PROP_IMMUTABLE: u32 = 1 << 2;
/// Property flag: enumerated.
pub const DRM_MODE_PROP_ENUM: u32 = 1 << 3;
/// Property flag: blob.
pub const DRM_MODE_PROP_BLOB: u32 = 1 << 4;
/// Property flag: bitmask.
pub const DRM_MODE_PROP_BITMASK: u32 = 1 << 5;
/// Property flag: object.
pub const DRM_MODE_PROP_OBJECT: u32 = 1 << 6;
/// Property flag: signed range.
pub const DRM_MODE_PROP_SIGNED_RANGE: u32 = 1 << 7;
/// Property flag: atomic (hidden from non-atomic clients).
pub const DRM_MODE_PROP_ATOMIC: u32 = 0x8000_0000;

/// Page-flip flag: request a flip-complete event.
pub const DRM_MODE_PAGE_FLIP_EVENT: u32 = 0x01;
/// Page-flip flag: asynchronous flip.
pub const DRM_MODE_PAGE_FLIP_ASYNC: u32 = 0x02;
/// Atomic flag: test only, do not apply.
pub const DRM_MODE_ATOMIC_TEST_ONLY: u32 = 0x0100;
/// Atomic flag: non-blocking commit.
pub const DRM_MODE_ATOMIC_NONBLOCK: u32 = 0x0200;
/// Atomic flag: allow full modeset.
pub const DRM_MODE_ATOMIC_ALLOW_MODESET: u32 = 0x0400;

/// Framebuffer flag: interlaced.
pub const DRM_MODE_FB_INTERLACED: u32 = 1 << 0;
/// Framebuffer flag: modifiers enabled.
pub const DRM_MODE_FB_MODIFIERS: u32 = 1 << 1;

/// Compute a fourcc code from four bytes.
///
/// Matches the `fourcc_code(a, b, c, d)` macro in `drm_fourcc.h`.
#[inline]
#[must_use]
pub const fn fourcc_code(a: u8, b: u8, c: u8, d: u8) -> u32 {
    (a as u32) | ((b as u32) << 8) | ((c as u32) << 16) | ((d as u32) << 24)
}

/// Kernel display-mode descriptor.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_mode_modeinfo {
    /// Pixel clock in kHz.
    pub clock: u32,
    /// Horizontal display size.
    pub hdisplay: u16,
    /// Horizontal sync start.
    pub hsync_start: u16,
    /// Horizontal sync end.
    pub hsync_end: u16,
    /// Horizontal total size.
    pub htotal: u16,
    /// Horizontal skew.
    pub hskew: u16,
    /// Vertical display size.
    pub vdisplay: u16,
    /// Vertical sync start.
    pub vsync_start: u16,
    /// Vertical sync end.
    pub vsync_end: u16,
    /// Vertical total size.
    pub vtotal: u16,
    /// Vertical scan.
    pub vscan: u16,
    /// Approximate vertical refresh in Hz.
    pub vrefresh: u32,
    /// Misc flags (`DRM_MODE_FLAG_*`).
    pub flags: u32,
    /// Type flags (`DRM_MODE_TYPE_*`).
    pub type_: u32,
    /// Human-readable name, NUL-padded.
    pub name: [std::os::raw::c_char; DRM_DISPLAY_MODE_LEN],
}

/// Framebuffer metadata for `ADDFB2` / `GETFB2`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_mode_fb_cmd2 {
    /// Framebuffer object id.
    pub fb_id: u32,
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
    /// Fourcc pixel format.
    pub pixel_format: u32,
    /// Flags (`DRM_MODE_FB_*`).
    pub flags: u32,
    /// GEM handles, one per plane.
    pub handles: [u32; 4],
    /// Pitches in bytes, one per plane.
    pub pitches: [u32; 4],
    /// Offsets in bytes, one per plane.
    pub offsets: [u32; 4],
    /// Format modifiers, one per plane.
    pub modifier: [u64; 4],
}

/// Dumb-buffer creation arguments.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_mode_create_dumb {
    /// Height in pixels (input).
    pub height: u32,
    /// Width in pixels (input).
    pub width: u32,
    /// Bits per pixel (input).
    pub bpp: u32,
    /// Flags, must be zero (input).
    pub flags: u32,
    /// GEM handle (output).
    pub handle: u32,
    /// Pitch in bytes (output).
    pub pitch: u32,
    /// Total size in bytes (output).
    pub size: u64,
}

/// Dumb-buffer mapping arguments.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_mode_map_dumb {
    /// GEM handle (input).
    pub handle: u32,
    /// Fake offset for `mmap(2)` (output).
    pub offset: u64,
}

/// Dumb-buffer destruction arguments.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_mode_destroy_dumb {
    /// GEM handle (input).
    pub handle: u32,
}

/// Property enum entry.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct drm_mode_property_enum {
    /// Value.
    pub value: u64,
    /// Name, NUL-padded.
    pub name: [std::os::raw::c_char; DRM_PROP_NAME_LEN],
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fourcc_matches_idl_subset() {
        // Values taken from NovoGFX-API.idl (little-endian fourcc codes).
        assert_eq!(fourcc_code(b'C', b'8', b' ', b' '), 0x2020_3843);
        assert_eq!(fourcc_code(b'X', b'R', b'2', b'4'), 0x3432_5258);
        assert_eq!(fourcc_code(b'A', b'R', b'2', b'4'), 0x3432_5241);
        assert_eq!(fourcc_code(b'Y', b'U', b'Y', b'V'), 0x5659_5559);
        assert_eq!(fourcc_code(b'N', b'V', b'1', b'2'), 0x3231_564e);
    }

    #[test]
    fn connector_constants_match_libdrm() {
        assert_eq!(DRM_MODE_CONNECTOR_HDMIA, 11);
        assert_eq!(DRM_MODE_CONNECTOR_DISPLAYPORT, 10);
        assert_eq!(DRM_MODE_CONNECTOR_VIRTUAL, 15);
        assert_eq!(DRM_MODE_CONNECTED, 1);
        assert_eq!(DRM_MODE_DISCONNECTED, 2);
    }

    #[test]
    fn atomic_flag_constants_match_kernel() {
        assert_eq!(DRM_MODE_ATOMIC_TEST_ONLY, 0x0100);
        assert_eq!(DRM_MODE_ATOMIC_NONBLOCK, 0x0200);
        assert_eq!(DRM_MODE_ATOMIC_ALLOW_MODESET, 0x0400);
        assert_eq!(DRM_MODE_PAGE_FLIP_EVENT, 0x01);
        assert_eq!(DRM_MODE_PAGE_FLIP_ASYNC, 0x02);
    }

    #[test]
    fn modeinfo_layout_is_c_compatible() {
        // clock(4) + 10×u16 timings(20) + vrefresh(4) + flags(4) + type(4) + name(32) = 68.
        assert_eq!(std::mem::size_of::<drm_mode_modeinfo>(), 68);
    }
}
