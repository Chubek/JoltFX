//! Buffer management: dumb buffers, framebuffers, PRIME dma-buf.
//!
//! `DumbBuffer::new` performs `CREATE_DUMB → MAP_DUMB → mmap → ADDFB2` and
//! unwinds each step on failure. `Drop` reverses the order:
//! `RMFB → munmap → DESTROY_DUMB`.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// CPU-mappable dumb buffers.
pub mod dumb;
/// Framebuffer wrappers.
pub mod framebuffer;
/// PRIME dma-buf import/export.
pub mod prime;

pub use dumb::DumbBuffer;
pub use framebuffer::{Framebuffer, FramebufferId};
pub use prime::{export_prime, import_prime};
