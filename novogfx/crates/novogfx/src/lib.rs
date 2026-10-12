//! `novogfx`: safe, idiomatic DRM/KMS backend.
//!
//! Facade crate re-exporting the layered implementation with pinned versions
//! so downstream users get reproducible builds. See `novogfx/AGENTS.md` for
//! the architecture and `NovoGFX-API.idl` for the C ABI contract.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// Atomic modesetting.
pub use novogfx_atomic as atomic;
/// Buffers and framebuffers.
pub use novogfx_buffer as buffer;
/// Device ownership, driver info, resources, errors.
pub use novogfx_core as core;
/// Event reading.
pub use novogfx_event as event;
/// Typed KMS wrappers.
pub use novogfx_kms as kms;
/// Raw FFI bindings (re-exported for advanced use).
pub use novogfx_sys as sys;

/// Stable crate version.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// ABI version as `MAJOR*10000 + MINOR*100 + PATCH` (mirrors the C ABI).
#[must_use]
pub fn abi_version() -> u64 {
    300 // 0.3.0 → 0*10000 + 3*100 + 0
}

/// Human-readable library version.
#[must_use]
pub fn version_string() -> &'static str {
    VERSION
}

// Direct re-exports for ergonomic use.
pub use novogfx_atomic::{AtomicRequest, AtomicState, CommitFlags, PlaneUpdate};
pub use novogfx_buffer::{export_prime, import_prime, DumbBuffer, Framebuffer, FramebufferId};
pub use novogfx_core::{Device, DeviceCaps, DriverInfo, Error, ResourceHandles, Result};
pub use novogfx_event::{
    CrtcSequenceEvent, Event, EventReader, HotplugEvent, PageFlipEvent, VblankEvent,
};
pub use novogfx_kms::{
    Connector, ConnectorId, ConnectorStatus, ConnectorType, Crtc, CrtcId, Edid, Encoder, EncoderId,
    EncoderType, Mode, Plane, PlaneId, PlaneType, Property, PropertyId,
};

/// Prelude for glob imports.
pub mod prelude {
    pub use crate::{
        AtomicRequest, CommitFlags, Connector, ConnectorStatus, Device, DeviceCaps, DumbBuffer,
        Edid, Error, Event, EventReader, FramebufferId, Mode, PlaneUpdate, Result,
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_parses() {
        assert!(!version_string().is_empty());
        assert_eq!(abi_version(), 300);
    }

    #[test]
    fn prelude_exports_device_and_error() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<Device>();
        let _ = Error::Again.status_code();
    }
}
