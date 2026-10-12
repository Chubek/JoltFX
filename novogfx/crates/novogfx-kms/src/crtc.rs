//! CRTC wrappers.

use std::os::fd::{AsFd, AsRawFd};

use novogfx_core::{Device, Error, Result};
use novogfx_sys::xf86drm;

use crate::Mode;

/// Opaque CRTC id.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct CrtcId(pub u32);

impl CrtcId {
    /// Raw kernel id.
    #[must_use]
    pub fn raw(&self) -> u32 {
        self.0
    }
}

impl From<u32> for CrtcId {
    fn from(v: u32) -> Self {
        Self(v)
    }
}

impl std::fmt::Display for CrtcId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// A KMS CRTC (display pipeline) bound to a [`Device`] lifetime.
#[derive(Debug)]
pub struct Crtc<'a> {
    device: &'a Device,
    id: CrtcId,
    fb_id: Option<u32>,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    mode: Option<Mode>,
}

impl<'a> Crtc<'a> {
    /// Query a CRTC by id.
    ///
    /// # Errors
    ///
    /// Returns [`Error::NotFound`] when the id does not exist.
    pub fn get(device: &'a Device, id: CrtcId) -> Result<Self> {
        let fd = device.as_fd().as_raw_fd();
        // SAFETY: fd owned by `device`; NULL or libdrm allocation.
        let ptr = unsafe { xf86drm::drmModeGetCrtc(fd, id.0) };
        if ptr.is_null() {
            return Err(Error::last_os_error("drmModeGetCrtc"));
        }
        // SAFETY: valid CRTC; copy out then free.
        let crtc = unsafe {
            let c = &*ptr;
            let out = Self {
                device,
                id,
                fb_id: if c.buffer_id == 0 {
                    None
                } else {
                    Some(c.buffer_id)
                },
                x: c.x,
                y: c.y,
                width: c.width,
                height: c.height,
                mode: if c.mode_valid != 0 {
                    Some(Mode::from_libdrm(&c.mode))
                } else {
                    None
                },
            };
            xf86drm::drmModeFreeCrtc(ptr);
            out
        };
        Ok(crtc)
    }

    /// CRTC id.
    #[must_use]
    pub fn id(&self) -> CrtcId {
        self.id
    }

    /// Currently bound framebuffer id, if any.
    #[must_use]
    pub fn current_framebuffer(&self) -> Option<u32> {
        self.fb_id
    }

    /// Current mode, if the CRTC is enabled.
    #[must_use]
    pub fn current_mode(&self) -> Option<&Mode> {
        self.mode.as_ref()
    }

    /// Position on the framebuffer.
    #[must_use]
    pub fn position(&self) -> (u32, u32) {
        (self.x, self.y)
    }

    /// Size of the CRTC viewport.
    #[must_use]
    pub fn size(&self) -> (u32, u32) {
        (self.width, self.height)
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
    fn crtc_id_newtype_roundtrips() {
        let id = CrtcId::from(42);
        assert_eq!(id.raw(), 42);
        assert_eq!(format!("{id}"), "42");
    }
}
