//! PRIME dma-buf import/export (`drmPrimeHandleToFD` / `drmPrimeFDToHandle`).

use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};

use novogfx_core::{Device, Error, Result};
use novogfx_sys::xf86drm;

/// Export a GEM handle as a PRIME dma-buf fd.
///
/// The returned [`OwnedFd`] is a new file description owning the dma-buf;
/// closing it does not affect the GEM handle.
pub fn export_prime(device: &Device, handle: u32) -> Result<OwnedFd> {
    use std::os::fd::AsFd;
    let fd = device.as_fd().as_raw_fd();
    let mut prime_fd: std::os::raw::c_int = -1;
    // SAFETY: fd is owned by `device`; `prime_fd` is a stack out-param.
    // Flags 0 = default (read/write).
    let ret = unsafe { xf86drm::drmPrimeHandleToFD(fd, handle, 0, &mut prime_fd) };
    if ret != 0 {
        return Err(Error::from_neg_errno(ret, "drmPrimeHandleToFD"));
    }
    if prime_fd < 0 {
        return Err(Error::Internal(
            "drmPrimeHandleToFD returned a negative fd".to_string(),
        ));
    }
    // SAFETY: `prime_fd` was just returned by the kernel and is owned by us.
    Ok(unsafe { OwnedFd::from_raw_fd(prime_fd) })
}

/// Import a PRIME dma-buf fd as a GEM handle.
///
/// The caller retains ownership of `prime_fd`.
pub fn import_prime(device: &Device, prime_fd: &OwnedFd) -> Result<u32> {
    use std::os::fd::AsFd;
    let fd = device.as_fd().as_raw_fd();
    let mut handle: u32 = 0;
    // SAFETY: both fds are valid; `handle` is a stack out-param.
    let ret = unsafe { xf86drm::drmPrimeFDToHandle(fd, prime_fd.as_raw_fd(), &mut handle) };
    if ret != 0 {
        return Err(Error::from_neg_errno(ret, "drmPrimeFDToHandle"));
    }
    if handle == 0 {
        return Err(Error::Internal(
            "drmPrimeFDToHandle returned handle 0".to_string(),
        ));
    }
    Ok(handle)
}

/// Validate PRIME flags without a device (unit-testable).
///
/// Only flag `0` (default read/write) is accepted by the current kernel ABI;
/// any other value is rejected client-side before reaching the ioctl.
#[must_use]
pub fn validate_export_flags(flags: u32) -> bool {
    flags == 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn export_flags_validation() {
        assert!(validate_export_flags(0));
        assert!(!validate_export_flags(1));
        assert!(!validate_export_flags(u32::MAX));
    }
}
