//! Vblank events.

/// A vertical-blanking event.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct VblankEvent {
    /// Vblank sequence number.
    pub sequence: u32,
    /// Seconds component of the timestamp.
    pub tv_sec: u32,
    /// Microseconds component of the timestamp.
    pub tv_usec: u32,
    /// CRTC id (0 on older kernels).
    pub crtc_id: u32,
    /// User data passed at request time.
    pub user_data: u64,
}

impl VblankEvent {
    /// Build from raw kernel fields (pure, unit-testable).
    #[must_use]
    pub fn from_raw(
        sequence: u32,
        tv_sec: u32,
        tv_usec: u32,
        crtc_id: u32,
        user_data: u64,
    ) -> Self {
        Self {
            sequence,
            tv_sec,
            tv_usec,
            crtc_id,
            user_data,
        }
    }

    /// Timestamp as `(sec, usec)`.
    #[must_use]
    pub fn timestamp(&self) -> (u32, u32) {
        (self.tv_sec, self.tv_usec)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_raw_preserves_fields() {
        let e = VblankEvent::from_raw(42, 1000, 500, 33, 0xdead);
        assert_eq!(e.sequence, 42);
        assert_eq!(e.timestamp(), (1000, 500));
        assert_eq!(e.crtc_id, 33);
        assert_eq!(e.user_data, 0xdead);
    }
}
