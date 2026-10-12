//! Last-committed atomic state snapshot.
//!
//! Tracks which CRTC/plane bindings were last committed so debuggers and
//! higher layers can reason about display state without re-querying the
//! kernel (which may change underneath us).

use std::collections::HashMap;

/// Snapshot of the last successful atomic commit.
#[derive(Debug, Clone, Default)]
pub struct AtomicState {
    /// CRTC id → framebuffer id (None = disabled).
    crtc_fb: HashMap<u32, Option<u32>>,
    /// Connector id → CRTC id (None = unbound).
    connector_crtc: HashMap<u32, Option<u32>>,
    /// Plane id → (framebuffer, CRTC) binding.
    plane_binding: HashMap<u32, (Option<u32>, Option<u32>)>,
    /// Number of successful commits recorded.
    commit_count: u64,
}

impl AtomicState {
    /// Empty state.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Record a CRTC binding.
    pub fn set_crtc(&mut self, crtc: u32, fb: Option<u32>) {
        self.crtc_fb.insert(crtc, fb);
    }

    /// Record a connector binding.
    pub fn set_connector(&mut self, connector: u32, crtc: Option<u32>) {
        self.connector_crtc.insert(connector, crtc);
    }

    /// Record a plane binding.
    pub fn set_plane(&mut self, plane: u32, fb: Option<u32>, crtc: Option<u32>) {
        self.plane_binding.insert(plane, (fb, crtc));
    }

    /// Look up the last-committed framebuffer for a CRTC.
    #[must_use]
    pub fn crtc_fb(&self, crtc: u32) -> Option<Option<u32>> {
        self.crtc_fb.get(&crtc).copied()
    }

    /// Look up the last-committed CRTC for a connector.
    #[must_use]
    pub fn connector_crtc(&self, connector: u32) -> Option<Option<u32>> {
        self.connector_crtc.get(&connector).copied()
    }

    /// Number of commits recorded.
    #[must_use]
    pub fn commit_count(&self) -> u64 {
        self.commit_count
    }

    /// Record that a commit succeeded.
    pub fn note_commit(&mut self) {
        self.commit_count = self.commit_count.saturating_add(1);
    }

    /// Clear all tracked bindings (e.g. after device loss).
    pub fn clear(&mut self) {
        self.crtc_fb.clear();
        self.connector_crtc.clear();
        self.plane_binding.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn state_tracks_bindings() {
        let mut s = AtomicState::new();
        assert_eq!(s.commit_count(), 0);
        s.set_crtc(10, Some(100));
        s.set_connector(30, Some(10));
        s.set_plane(50, Some(100), Some(10));
        assert_eq!(s.crtc_fb(10), Some(Some(100)));
        assert_eq!(s.connector_crtc(30), Some(Some(10)));
        assert_eq!(s.crtc_fb(99), None);
        s.note_commit();
        assert_eq!(s.commit_count(), 1);
        s.set_crtc(10, None);
        assert_eq!(s.crtc_fb(10), Some(None));
        s.clear();
        assert_eq!(s.crtc_fb(10), None);
        assert_eq!(s.commit_count(), 1);
    }
}
