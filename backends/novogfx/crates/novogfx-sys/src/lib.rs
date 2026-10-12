//! Raw, unsafe FFI bindings to `libdrm`.
//!
//! This crate exposes C structs, ioctl constants, and `extern "C"` function
//! declarations only. It provides **no** safe abstractions; safe wrappers live
//! in `novogfx-core` and above.
//!
//! Linking is performed via `pkg-config libdrm` (see `build.rs`).

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

/// Raw `drm.h` types: caps, version, events, client caps.
pub mod drm;
/// Raw `drm_mode.h` types: modesetting structs and `DRM_MODE_*` constants.
pub mod drm_mode;
/// Raw `xf86drm.h` / `xf86drmMode.h` function declarations and buffer structs.
pub mod xf86drm;
