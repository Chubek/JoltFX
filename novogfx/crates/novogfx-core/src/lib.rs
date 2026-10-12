//! Core device ownership, driver info, and resource enumeration.
//!
//! [`Device`] owns the DRM file descriptor. All other novogfx objects borrow
//! from it, so handles can never outlive the fd they were obtained from.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// [`Device`]: opening, capabilities, atomic enablement, resource cache.
pub mod device;
/// Typed errors shared by the whole workspace.
pub mod error;
/// Cached KMS resource handle lists.
pub mod resources;

pub use device::{Device, DeviceCaps, DriverInfo};
pub use error::{Error, Result};
pub use resources::ResourceHandles;
