//! Cached KMS resource handle lists.
//!
//! [`ResourceHandles`] is a snapshot of `DRM_IOCTL_MODE_GETRESOURCES` plus
//! plane resources. The [`Device`](crate::Device) caches it and invalidates
//! the cache on hotplug (driven by `novogfx-event`).

/// Snapshot of KMS object ids for a device.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ResourceHandles {
    /// Framebuffer ids (`drmModeRes.fbs`).
    pub framebuffers: Vec<u32>,
    /// CRTC ids.
    pub crtcs: Vec<u32>,
    /// Connector ids.
    pub connectors: Vec<u32>,
    /// Encoder ids.
    pub encoders: Vec<u32>,
    /// Plane ids (from `drmModeGetPlaneResources`).
    pub planes: Vec<u32>,
    /// Minimum supported width.
    pub min_width: u32,
    /// Maximum supported width.
    pub max_width: u32,
    /// Minimum supported height.
    pub min_height: u32,
    /// Maximum supported height.
    pub max_height: u32,
}

impl ResourceHandles {
    /// Create an empty handle set.
    #[must_use]
    pub fn empty() -> Self {
        Self::default()
    }

    /// Returns `true` if there are no KMS objects at all.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.framebuffers.is_empty()
            && self.crtcs.is_empty()
            && self.connectors.is_empty()
            && self.encoders.is_empty()
            && self.planes.is_empty()
    }

    /// Total number of KMS objects across all classes.
    #[must_use]
    pub fn len(&self) -> usize {
        self.framebuffers.len()
            + self.crtcs.len()
            + self.connectors.len()
            + self.encoders.len()
            + self.planes.len()
    }

    /// Returns `true` if the given connector id is in this snapshot.
    #[must_use]
    pub fn contains_connector(&self, id: u32) -> bool {
        self.connectors.contains(&id)
    }

    /// Returns `true` if the given CRTC id is in this snapshot.
    #[must_use]
    pub fn contains_crtc(&self, id: u32) -> bool {
        self.crtcs.contains(&id)
    }

    /// Returns `true` if the given plane id is in this snapshot.
    #[must_use]
    pub fn contains_plane(&self, id: u32) -> bool {
        self.planes.contains(&id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_handle_set_reports_zero() {
        let h = ResourceHandles::empty();
        assert!(h.is_empty());
        assert_eq!(h.len(), 0);
        assert!(!h.contains_connector(1));
    }

    #[test]
    fn contains_queries_hit_each_class() {
        let h = ResourceHandles {
            framebuffers: vec![10],
            crtcs: vec![20],
            connectors: vec![30],
            encoders: vec![40],
            planes: vec![50],
            min_width: 320,
            max_width: 7680,
            min_height: 200,
            max_height: 4320,
        };
        assert_eq!(h.len(), 5);
        assert!(!h.is_empty());
        assert!(h.contains_connector(30));
        assert!(h.contains_crtc(20));
        assert!(h.contains_plane(50));
        assert!(!h.contains_plane(51));
    }
}
