//! Shared helpers for vkms integration tests.

use std::path::Path;

/// Returns the DRM device path to test against.
///
/// Honours `NOVOGFX_TEST_DEVICE` (default `/dev/dri/card0`).
#[must_use]
pub fn test_device_path() -> String {
    std::env::var("NOVOGFX_TEST_DEVICE").unwrap_or_else(|_| "/dev/dri/card0".to_string())
}

/// Returns `true` when a DRM device is present and openable.
#[must_use]
pub fn device_available() -> bool {
    let path = test_device_path();
    Path::new(&path).exists() && novogfx::Device::open(&path).is_ok()
}

/// Skip helper: prints a TAP-style skip and returns early from the test.
#[macro_export]
macro_rules! require_device {
    () => {
        if !$crate::common::device_available() {
            eprintln!(
                "SKIP: no DRM device at {} (set NOVOGFX_TEST_DEVICE or load vkms)",
                $crate::common::test_device_path()
            );
            return;
        }
    };
}
