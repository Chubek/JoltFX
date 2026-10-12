//! Reading and parsing DRM events from the device fd.
//!
//! [`EventReader::poll`] performs a single non-blocking `read(2)` and parses
//! zero or more `drm_event` structs. Partial reads are buffered. The reader
//! never blocks; callers use `poll`/`epoll` on `Device::as_fd`.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// Hotplug / connection events.
pub mod hotplug;
/// Page-flip completion events.
pub mod page_flip;
/// Event parsing and buffering.
pub mod reader;
/// Vblank events.
pub mod vblank;

pub use hotplug::HotplugEvent;
pub use page_flip::PageFlipEvent;
pub use reader::{CrtcSequenceEvent, Event, EventReader};
pub use vblank::VblankEvent;
