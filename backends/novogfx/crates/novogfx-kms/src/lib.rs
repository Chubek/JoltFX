//! Typed KMS object wrappers.
//!
//! Wrappers borrow the [`Device`](novogfx_core::Device) so handles cannot
//! outlive the fd. Raw `u32` handles never appear in the public API; each
//! class has a `*Id` newtype.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// Connector wrappers, status, and type.
pub mod connector;
/// CRTC wrappers.
pub mod crtc;
/// EDID parsing (fuzzable in isolation, no device required).
pub mod edid;
/// Encoder wrappers.
pub mod encoder;
/// Display-mode snapshots.
pub mod mode;
/// Plane wrappers.
pub mod plane;
/// Property handles and well-known names.
pub mod property;

pub use connector::{Connector, ConnectorId, ConnectorStatus, ConnectorType};
pub use crtc::{Crtc, CrtcId};
pub use edid::Edid;
pub use encoder::{Encoder, EncoderId, EncoderType};
pub use mode::Mode;
pub use plane::{Plane, PlaneId, PlaneType};
pub use property::{Property, PropertyId};
