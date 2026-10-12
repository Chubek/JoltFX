//! Hotplug / connection-change events.
//!
//! The kernel does not emit a dedicated hotplug `drm_event`; connection
//! changes are discovered by re-probing connectors after a `DRM_EVENT_VBLANK`
//! / uevent or an `EPOLLHUP`. This type represents the novogfx-level
//! notification derived from such probes.

/// A connector connection change.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct HotplugEvent {
    /// Connector id whose status changed (0 = unknown / any).
    pub connector_id: u32,
    /// Monotonic sequence number for this device.
    pub sequence: u64,
}

impl HotplugEvent {
    /// Build from raw fields (pure, unit-testable).
    #[must_use]
    pub fn from_raw(connector_id: u32, sequence: u64) -> Self {
        Self {
            connector_id,
            sequence,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_raw_preserves_fields() {
        let e = HotplugEvent::from_raw(30, 7);
        assert_eq!(e.connector_id, 30);
        assert_eq!(e.sequence, 7);
    }
}
