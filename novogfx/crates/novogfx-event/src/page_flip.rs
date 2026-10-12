//! Page-flip completion events.

/// A page-flip completion event.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PageFlipEvent {
    /// Vblank sequence number at completion.
    pub sequence: u32,
    /// Seconds component of the timestamp.
    pub tv_sec: u32,
    /// Microseconds component of the timestamp.
    pub tv_usec: u32,
    /// CRTC id.
    pub crtc_id: u32,
    /// User data passed at flip time.
    pub user_data: u64,
    /// Completion status: 0 = success, otherwise an `errno`.
    pub status: i32,
}

impl PageFlipEvent {
    /// Build from raw kernel fields (pure, unit-testable).
    #[must_use]
    pub fn from_raw(
        sequence: u32,
        tv_sec: u32,
        tv_usec: u32,
        crtc_id: u32,
        user_data: u64,
        status: i32,
    ) -> Self {
        Self {
            sequence,
            tv_sec,
            tv_usec,
            crtc_id,
            user_data,
            status,
        }
    }

    /// Returns `true` when the flip succeeded.
    #[must_use]
    pub fn is_success(&self) -> bool {
        self.status == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn success_predicate() {
        assert!(PageFlipEvent::from_raw(1, 0, 0, 10, 0, 0).is_success());
        assert!(!PageFlipEvent::from_raw(1, 0, 0, 10, 0, -5).is_success());
    }
}
