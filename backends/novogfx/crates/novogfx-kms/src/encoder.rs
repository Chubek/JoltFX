//! Encoder wrappers.

use std::os::fd::{AsFd, AsRawFd};

use novogfx_core::{Device, Error, Result};
use novogfx_sys::{drm_mode, xf86drm};

/// Opaque encoder id.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EncoderId(pub u32);

impl EncoderId {
    /// Raw kernel id.
    #[must_use]
    pub fn raw(&self) -> u32 {
        self.0
    }
}

impl From<u32> for EncoderId {
    fn from(v: u32) -> Self {
        Self(v)
    }
}

/// Encoder type.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum EncoderType {
    /// No encoder.
    None,
    /// DAC.
    Dac,
    /// TMDS.
    Tmds,
    /// LVDS.
    Lvds,
    /// TV DAC.
    TvDac,
    /// Virtual.
    Virtual,
    /// DSI.
    Dsi,
    /// DisplayPort MST.
    Dpmst,
    /// DPI.
    Dpi,
    /// Unknown / future.
    Unknown(u32),
}

impl EncoderType {
    /// Map the kernel encoder type.
    #[must_use]
    pub fn from_raw(v: u32) -> Self {
        match v {
            drm_mode::DRM_MODE_ENCODER_NONE => Self::None,
            drm_mode::DRM_MODE_ENCODER_DAC => Self::Dac,
            drm_mode::DRM_MODE_ENCODER_TMDS => Self::Tmds,
            drm_mode::DRM_MODE_ENCODER_LVDS => Self::Lvds,
            drm_mode::DRM_MODE_ENCODER_TVDAC => Self::TvDac,
            drm_mode::DRM_MODE_ENCODER_VIRTUAL => Self::Virtual,
            drm_mode::DRM_MODE_ENCODER_DSI => Self::Dsi,
            drm_mode::DRM_MODE_ENCODER_DPMST => Self::Dpmst,
            drm_mode::DRM_MODE_ENCODER_DPI => Self::Dpi,
            other => Self::Unknown(other),
        }
    }
}

/// A KMS encoder bound to a [`Device`] lifetime.
#[derive(Debug)]
pub struct Encoder<'a> {
    device: &'a Device,
    id: EncoderId,
    kind: EncoderType,
    crtc_id: Option<u32>,
    possible_crtcs: u32,
    possible_clones: u32,
}

impl<'a> Encoder<'a> {
    /// Query an encoder by id.
    pub fn get(device: &'a Device, id: EncoderId) -> Result<Self> {
        let fd = device.as_fd().as_raw_fd();
        // SAFETY: fd owned by `device`; NULL or libdrm allocation.
        let ptr = unsafe { xf86drm::drmModeGetEncoder(fd, id.0) };
        if ptr.is_null() {
            return Err(Error::last_os_error("drmModeGetEncoder"));
        }
        // SAFETY: valid encoder; copy out then free.
        let enc = unsafe {
            let e = &*ptr;
            let out = Self {
                device,
                id,
                kind: EncoderType::from_raw(e.encoder_type),
                crtc_id: if e.crtc_id == 0 {
                    None
                } else {
                    Some(e.crtc_id)
                },
                possible_crtcs: e.possible_crtcs,
                possible_clones: e.possible_clones,
            };
            xf86drm::drmModeFreeEncoder(ptr);
            out
        };
        Ok(enc)
    }

    /// Encoder id.
    #[must_use]
    pub fn id(&self) -> EncoderId {
        self.id
    }

    /// Encoder type.
    #[must_use]
    pub fn encoder_type(&self) -> EncoderType {
        self.kind
    }

    /// Bound CRTC, if any.
    #[must_use]
    pub fn current_crtc(&self) -> Option<u32> {
        self.crtc_id
    }

    /// Bitmask of compatible CRTCs.
    #[must_use]
    pub fn possible_crtcs(&self) -> u32 {
        self.possible_crtcs
    }

    /// Bitmask of possible clones.
    #[must_use]
    pub fn possible_clones(&self) -> u32 {
        self.possible_clones
    }

    /// Borrowed device.
    #[must_use]
    pub fn device(&self) -> &'a Device {
        self.device
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encoder_type_mapping() {
        assert_eq!(
            EncoderType::from_raw(drm_mode::DRM_MODE_ENCODER_TMDS),
            EncoderType::Tmds
        );
        assert_eq!(
            EncoderType::from_raw(drm_mode::DRM_MODE_ENCODER_VIRTUAL),
            EncoderType::Virtual
        );
        assert_eq!(EncoderType::from_raw(999), EncoderType::Unknown(999));
    }
}
