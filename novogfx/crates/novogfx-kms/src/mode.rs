//! Display-mode snapshots parsed once at connector construction.

use novogfx_sys::drm_mode;

/// A display mode snapshot (e.g. `1920x1080` @ 60 Hz).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Mode {
    /// Pixel clock in kHz.
    pub clock_khz: u32,
    /// Visible horizontal pixels.
    pub hdisplay: u16,
    /// Horizontal sync start.
    pub hsync_start: u16,
    /// Horizontal sync end.
    pub hsync_end: u16,
    /// Horizontal total.
    pub htotal: u16,
    /// Horizontal skew.
    pub hskew: u16,
    /// Visible vertical lines.
    pub vdisplay: u16,
    /// Vertical sync start.
    pub vsync_start: u16,
    /// Vertical sync end.
    pub vsync_end: u16,
    /// Vertical total.
    pub vtotal: u16,
    /// Vertical scan (stereo doubling).
    pub vscan: u16,
    /// Approximate refresh in Hz.
    pub vrefresh: u32,
    /// Misc flags (`DRM_MODE_FLAG_*`).
    pub flags: u32,
    /// Type flags (`DRM_MODE_TYPE_*`).
    pub kind: u32,
    /// Mode name, e.g. `"1920x1080"`.
    pub name: String,
}

impl Mode {
    /// Build from a raw kernel `drm_mode_modeinfo`.
    ///
    /// Pure and unit-testable; performs no I/O.
    #[must_use]
    pub fn from_raw(raw: &drm_mode::drm_mode_modeinfo) -> Self {
        Self::from_parts(
            raw.clock,
            raw.hdisplay,
            raw.hsync_start,
            raw.hsync_end,
            raw.htotal,
            raw.hskew,
            raw.vdisplay,
            raw.vsync_start,
            raw.vsync_end,
            raw.vtotal,
            raw.vscan,
            raw.vrefresh,
            raw.flags,
            raw.type_,
            c_chars_to_string(&raw.name),
        )
    }

    /// Build from a libdrm `drmModeModeInfo`.
    #[must_use]
    pub fn from_libdrm(raw: &novogfx_sys::xf86drm::drmModeModeInfo) -> Self {
        Self::from_parts(
            raw.clock,
            raw.hdisplay,
            raw.hsync_start,
            raw.hsync_end,
            raw.htotal,
            raw.hskew,
            raw.vdisplay,
            raw.vsync_start,
            raw.vsync_end,
            raw.vtotal,
            raw.vscan,
            raw.vrefresh,
            raw.flags,
            raw.type_,
            c_chars_to_string(&raw.name),
        )
    }

    /// Construct directly (used by tests and mocks).
    #[allow(clippy::too_many_arguments)]
    #[must_use]
    pub fn from_parts(
        clock_khz: u32,
        hdisplay: u16,
        hsync_start: u16,
        hsync_end: u16,
        htotal: u16,
        hskew: u16,
        vdisplay: u16,
        vsync_start: u16,
        vsync_end: u16,
        vtotal: u16,
        vscan: u16,
        vrefresh: u32,
        flags: u32,
        kind: u32,
        name: String,
    ) -> Self {
        Self {
            clock_khz,
            hdisplay,
            hsync_start,
            hsync_end,
            htotal,
            hskew,
            vdisplay,
            vsync_start,
            vsync_end,
            vtotal,
            vscan,
            vrefresh,
            flags,
            kind,
            name,
        }
    }

    /// Returns `true` if this mode is marked preferred.
    #[must_use]
    pub fn is_preferred(&self) -> bool {
        self.kind & drm_mode::DRM_MODE_TYPE_PREFERRED != 0
    }

    /// Returns `true` if this mode is driver-provided.
    #[must_use]
    pub fn is_driver(&self) -> bool {
        // `DRM_MODE_TYPE_DRIVER = 1 << 6`.
        self.kind & (1 << 6) != 0
    }

    /// Width × height as `(hdisplay, vdisplay)`.
    #[must_use]
    pub fn resolution(&self) -> (u16, u16) {
        (self.hdisplay, self.vdisplay)
    }

    /// Estimated refresh × 1000 (millihertz) from the pixel clock when
    /// `vrefresh` is zero, otherwise `vrefresh * 1000`.
    ///
    /// Pure arithmetic over the snapshot; no device required.
    #[must_use]
    pub fn refresh_millihertz(&self) -> u32 {
        if self.vrefresh != 0 {
            return self.vrefresh.saturating_mul(1000);
        }
        let htotal = u64::from(self.htotal);
        let vtotal = u64::from(self.vtotal);
        if htotal == 0 || vtotal == 0 {
            return 0;
        }
        let clock_hz = u64::from(self.clock_khz).saturating_mul(1000);
        let millihz = clock_hz
            .saturating_mul(1000)
            .checked_div(htotal.saturating_mul(vtotal))
            .unwrap_or(0);
        u32::try_from(millihz).unwrap_or(u32::MAX)
    }
}

fn c_chars_to_string(buf: &[std::os::raw::c_char]) -> String {
    let bytes: Vec<u8> = buf
        .iter()
        .take_while(|&&c| c != 0)
        .map(|&c| c as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Mode {
        Mode::from_parts(
            148_500,
            1920,
            2008,
            2052,
            2200,
            0,
            1080,
            1084,
            1089,
            1125,
            0,
            60,
            0,
            drm_mode::DRM_MODE_TYPE_PREFERRED | drm_mode::DRM_MODE_TYPE_DRIVER,
            "1920x1080".to_string(),
        )
    }

    #[test]
    fn preferred_flag_is_detected() {
        assert!(sample().is_preferred());
        let mut m = sample();
        m.kind = 0;
        assert!(!m.is_preferred());
    }

    #[test]
    fn resolution_and_refresh_helpers() {
        let m = sample();
        assert_eq!(m.resolution(), (1920, 1080));
        assert_eq!(m.refresh_millihertz(), 60_000);
    }

    #[test]
    fn zero_vrefresh_falls_back_to_clock_math() {
        let mut m = sample();
        m.vrefresh = 0;
        // 148.5 MHz / (2200 * 1125) = 60 Hz.
        assert_eq!(m.refresh_millihertz(), 60_000);
    }

    #[test]
    fn degenerate_timings_yield_zero_refresh() {
        let mut m = sample();
        m.vrefresh = 0;
        m.htotal = 0;
        assert_eq!(m.refresh_millihertz(), 0);
    }

    #[test]
    fn from_raw_truncates_at_nul() {
        let mut raw = drm_mode::drm_mode_modeinfo {
            clock: 148_500,
            hdisplay: 1920,
            hsync_start: 2008,
            hsync_end: 2052,
            htotal: 2200,
            hskew: 0,
            vdisplay: 1080,
            vsync_start: 1084,
            vsync_end: 1089,
            vtotal: 1125,
            vscan: 0,
            vrefresh: 60,
            flags: 0,
            type_: drm_mode::DRM_MODE_TYPE_PREFERRED,
            name: [0; drm_mode::DRM_DISPLAY_MODE_LEN],
        };
        let name = b"1920x1080";
        for (i, b) in name.iter().enumerate() {
            raw.name[i] = *b as std::os::raw::c_char;
        }
        let m = Mode::from_raw(&raw);
        assert_eq!(m.name, "1920x1080");
        assert!(m.is_preferred());
    }
}
