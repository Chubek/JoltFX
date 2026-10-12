//! Framebuffer wrappers (`drmModeAddFB2` / `drmModeRmFB`).

/// Opaque framebuffer id.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct FramebufferId(pub u32);

impl FramebufferId {
    /// Raw kernel id.
    #[must_use]
    pub fn raw(&self) -> u32 {
        self.0
    }
}

impl From<u32> for FramebufferId {
    fn from(v: u32) -> Self {
        Self(v)
    }
}

impl std::fmt::Display for FramebufferId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// A framebuffer handle bound to a device fd.
///
/// The dumb-buffer path (`DumbBuffer`) owns its framebuffer; this type is
/// for framebuffers created by other means (e.g. imported PRIME buffers).
/// Dropping a `Framebuffer` does **not** remove the kernel object, so copies
/// of the id stay meaningful.
/// (The dumb-buffer `Drop` path calls `drmModeRmFB` itself.)
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Framebuffer {
    /// Kernel id.
    id: FramebufferId,
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
    /// Fourcc pixel format.
    pub format: u32,
}

impl Framebuffer {
    /// Wrap a known framebuffer id with its metadata.
    #[must_use]
    pub fn from_raw(id: FramebufferId, width: u32, height: u32, format: u32) -> Self {
        Self {
            id,
            width,
            height,
            format,
        }
    }

    /// Framebuffer id.
    #[must_use]
    pub fn id(&self) -> FramebufferId {
        self.id
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn framebuffer_id_roundtrips() {
        let id = FramebufferId::from(77);
        assert_eq!(id.raw(), 77);
        let fb = Framebuffer::from_raw(id, 1920, 1080, 0x3432_5241);
        assert_eq!(fb.id(), id);
        assert_eq!(fb.width, 1920);
    }
}
