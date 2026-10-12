//! Typed errors for novogfx.
//!
//! Mirrors the `novogfx_status_t` codes in `NovoGFX-API.idl`. `errno` values
//! from `libdrm` are preserved via [`Error::Io`] using
//! `std::io::Error::from_raw_os_error`.

use std::fmt;

/// Result alias used across all novogfx crates.
pub type Result<T> = std::result::Result<T, Error>;

/// Typed error for all novogfx operations.
#[derive(Debug)]
pub enum Error {
    /// An invalid argument was supplied.
    InvalidArgument(String),
    /// An OS / `libdrm` error, preserving the original `errno`.
    Io(std::io::Error),
    /// The requested object was not found.
    NotFound(String),
    /// The operation is not supported by this device/driver.
    NotSupported(String),
    /// Permission denied (e.g. not DRM master, wrong file permissions).
    PermissionDenied(String),
    /// Device or resource is busy.
    Busy(String),
    /// Non-blocking operation would block (no events pending).
    Again,
    /// Out of memory.
    OutOfMemory(String),
    /// The device was lost (e.g. GPU reset, hot-unplug).
    DeviceLost(String),
    /// The operation was cancelled.
    Cancelled(String),
    /// Internal error (bug); carries a context string.
    Internal(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidArgument(s) => write!(f, "invalid argument: {s}"),
            Self::Io(e) => write!(f, "I/O error: {e}"),
            Self::NotFound(s) => write!(f, "not found: {s}"),
            Self::NotSupported(s) => write!(f, "not supported: {s}"),
            Self::PermissionDenied(s) => write!(f, "permission denied: {s}"),
            Self::Busy(s) => write!(f, "busy: {s}"),
            Self::Again => write!(f, "try again"),
            Self::OutOfMemory(s) => write!(f, "out of memory: {s}"),
            Self::DeviceLost(s) => write!(f, "device lost: {s}"),
            Self::Cancelled(s) => write!(f, "cancelled: {s}"),
            Self::Internal(s) => write!(f, "internal error: {s}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Self::Io(e)
    }
}

impl Error {
    /// Convert a negative-`errno` libdrm return into an [`Error`].
    ///
    /// libdrm helpers return `0` on success and `-errno` on failure.
    /// Positive `ioctl(2)` errors surface as `-1` with `errno` set; callers
    /// should use [`Error::last_os_error`] for that path.
    #[must_use]
    pub fn from_neg_errno(ret: i32, context: &str) -> Self {
        debug_assert!(ret < 0, "from_neg_errno called with non-negative return");
        let errno = -ret;
        if errno == errno_eacces() || errno == errno_eperm() {
            Self::PermissionDenied(context.to_string())
        } else if errno == errno_ebusy() {
            Self::Busy(context.to_string())
        } else if errno == errno_eagain() {
            Self::Again
        } else if errno == errno_enomem() {
            Self::OutOfMemory(context.to_string())
        } else if errno == errno_enodev() {
            Self::DeviceLost(context.to_string())
        } else if errno == errno_eopnotsupp() || errno == errno_enosys() {
            Self::NotSupported(format!("{context} (errno {errno})"))
        } else if errno == errno_enoent() {
            Self::NotFound(format!("{context} (errno {errno})"))
        } else {
            Self::Io(std::io::Error::from_raw_os_error(errno))
        }
    }

    /// Capture the current `errno` after a `-1` return into an [`Error`].
    pub fn last_os_error(context: &str) -> Self {
        let e = std::io::Error::last_os_error();
        match e.raw_os_error() {
            Some(code) => Self::from_neg_errno(-code, context),
            None => Self::Io(e),
        }
    }

    /// Map a status-code integer (C ABI) back to an [`Error`].
    ///
    /// Used by the C wrapper and tested in isolation (no device required).
    #[must_use]
    pub fn from_status_code(code: u32, context: String) -> Self {
        match code {
            0 => Self::Internal(format!("from_status_code(OK) with context: {context}")),
            1 => Self::InvalidArgument(context),
            2 => Self::Io(std::io::Error::other(context)),
            3 => Self::NotFound(context),
            4 => Self::NotSupported(context),
            5 => Self::PermissionDenied(context),
            6 => Self::Busy(context),
            7 => Self::Again,
            8 => Self::OutOfMemory(context),
            9 => Self::DeviceLost(context),
            10 => Self::Cancelled(context),
            _ => Self::Internal(context),
        }
    }

    /// Convert to the C ABI status code (`novogfx_status_t`).
    #[must_use]
    pub fn status_code(&self) -> u32 {
        match self {
            Self::InvalidArgument(_) => 1,
            Self::Io(_) => 2,
            Self::NotFound(_) => 3,
            Self::NotSupported(_) => 4,
            Self::PermissionDenied(_) => 5,
            Self::Busy(_) => 6,
            Self::Again => 7,
            Self::OutOfMemory(_) => 8,
            Self::DeviceLost(_) => 9,
            Self::Cancelled(_) => 10,
            Self::Internal(_) => 11,
        }
    }
}

// Portable errno constants without depending on `libc` (std only).
// Values match the Linux userspace ABI on all supported arches.
fn errno_eacces() -> i32 {
    13
}
fn errno_eperm() -> i32 {
    1
}
fn errno_ebusy() -> i32 {
    16
}
fn errno_eagain() -> i32 {
    11
}
fn errno_enomem() -> i32 {
    12
}
fn errno_enodev() -> i32 {
    19
}
fn errno_eopnotsupp() -> i32 {
    95
}
fn errno_enosys() -> i32 {
    38
}
fn errno_enoent() -> i32 {
    2
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn errno_mapping_covers_common_cases() {
        let e = Error::from_neg_errno(-13, "open");
        assert!(matches!(e, Error::PermissionDenied(_)));
        let e = Error::from_neg_errno(-16, "commit");
        assert!(matches!(e, Error::Busy(_)));
        let e = Error::from_neg_errno(-11, "poll");
        assert!(matches!(e, Error::Again));
        let e = Error::from_neg_errno(-12, "alloc");
        assert!(matches!(e, Error::OutOfMemory(_)));
        let e = Error::from_neg_errno(-19, "ioctl");
        assert!(matches!(e, Error::DeviceLost(_)));
        let e = Error::from_neg_errno(-95, "atomic");
        assert!(matches!(e, Error::NotSupported(_)));
        let e = Error::from_neg_errno(-2, "lookup");
        assert!(matches!(e, Error::NotFound(_)));
        // Unknown errno preserves the raw code via Io.
        let e = Error::from_neg_errno(-5, "read");
        match e {
            Error::Io(io) => assert_eq!(io.raw_os_error(), Some(5)),
            other => panic!("expected Io, got {other:?}"),
        }
    }

    #[test]
    fn status_code_roundtrip() {
        let cases = [
            (Error::InvalidArgument("x".into()), 1u32),
            (Error::NotFound("x".into()), 3u32),
            (Error::NotSupported("x".into()), 4u32),
            (Error::PermissionDenied("x".into()), 5u32),
            (Error::Busy("x".into()), 6u32),
            (Error::Again, 7u32),
            (Error::OutOfMemory("x".into()), 8u32),
            (Error::DeviceLost("x".into()), 9u32),
            (Error::Cancelled("x".into()), 10u32),
            (Error::Internal("x".into()), 11u32),
        ];
        for (e, code) in cases {
            assert_eq!(e.status_code(), code);
        }
        let e = Error::from_status_code(3, "missing".to_string());
        assert!(matches!(e, Error::NotFound(_)));
        let e = Error::from_status_code(7, String::new());
        assert!(matches!(e, Error::Again));
    }

    #[test]
    fn io_error_context_is_preserved() {
        let io = std::io::Error::from_raw_os_error(5);
        let e = Error::from(io);
        assert_eq!(e.status_code(), 2);
        assert!(format!("{e}").contains("I/O error"));
    }
}
