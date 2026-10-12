//! Atomic modesetting: builder, commit flags, and state tracking.
//!
//! Only this crate may call `DRM_IOCTL_MODE_ATOMIC` (via
//! `drmModeAtomicCommit`). Blob properties (`MODE_ID`, `IN_FORMATS`) are
//! created and destroyed within a single [`AtomicRequest::commit`] call and
//! never leak across commits.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// Commit flags (`DRM_MODE_ATOMIC_*`).
pub mod commit;
/// [`AtomicRequest`] builder.
pub mod request;
/// Atomic state tracking (last-committed snapshot).
pub mod state;

pub use commit::CommitFlags;
pub use request::{AtomicRequest, PlaneUpdate};
pub use state::AtomicState;
