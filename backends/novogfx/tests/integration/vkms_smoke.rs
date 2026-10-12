//! vkms smoke tests.
//!
//! Requires a Virtual KMS device (or any DRM node):
//!
//! ```sh
//! sudo modprobe vkms
//! cargo test -p novogfx --features vkms --test vkms_smoke -- --test-threads=1
//! ```
//!
//! Every test skips (rather than fails) when no DRM device is present, so
//! `cargo test --workspace` stays green on headless CI. Only this suite
//! requires elevated privileges / a vkms device; unit tests never do.

#[path = "common/mod.rs"]
mod common;

use novogfx::prelude::*;

#[test]
fn vkms_device_opens_and_reports_driver() {
    if !common::device_available() {
        eprintln!(
            "SKIP: no DRM device at {} (set NOVOGFX_TEST_DEVICE or load vkms)",
            common::test_device_path()
        );
        return;
    }
    let device = Device::open(common::test_device_path()).expect("device must open");
    let info = device.driver().expect("driver info must query");
    assert!(!info.name.is_empty(), "driver name must be non-empty");
}

#[test]
fn vkms_resources_enumerate() {
    if !common::device_available() {
        eprintln!("SKIP: no DRM device");
        return;
    }
    let device = Device::open(common::test_device_path()).unwrap();
    let res = device.resources().unwrap().clone();
    let owned = device
        .resource_snapshot()
        .expect("fresh owned resources must query");
    assert!(!owned.is_empty());
    device.note_hotplug();
    assert!(!res.is_empty(), "retained snapshots survive invalidation");
    // vkms exposes at least one connector/CRTC/plane; a real GPU does too.
    // We assert non-emptiness loosely so new kernel configs don't break us.
    let total = res.len();
    assert!(total > 0, "expected at least one KMS object, got none");
}

#[test]
fn vkms_atomic_test_commit() {
    if !common::device_available() {
        eprintln!("SKIP: no DRM device");
        return;
    }
    let mut device = Device::open(common::test_device_path()).unwrap();
    if device.enable_atomic().is_err() {
        eprintln!("SKIP: device does not support atomic modesetting");
        return;
    }
    // An empty atomic request must test-commit cleanly (validates nothing).
    let mut req = AtomicRequest::new(&mut device);
    assert!(req.is_empty());
    req.commit(CommitFlags::TEST_ONLY | CommitFlags::ALLOW_MODESET)
        .expect("empty test-commit must succeed");
}

#[test]
fn vkms_dumb_buffer_roundtrip() {
    if !common::device_available() {
        eprintln!("SKIP: no DRM device");
        return;
    }
    let device = Device::open(common::test_device_path()).unwrap();
    if !device.capabilities().contains(DeviceCaps::DUMB_BUFFER) {
        eprintln!("SKIP: device lacks dumb-buffer support");
        return;
    }
    let mut buf = DumbBuffer::new(&device, 64, 64, 32).expect("dumb buffer must create");
    buf.fill_solid(0x00_FF_FF_FF, 4).expect("fill must succeed");
    assert_eq!(buf.width(), 64);
    assert!(buf.pitch() >= 64 * 4);
}

#[test]
fn vkms_event_reader_is_nonblocking() {
    if !common::device_available() {
        eprintln!("SKIP: no DRM device");
        return;
    }
    let device = Device::open(common::test_device_path()).unwrap();
    let mut reader = EventReader::new(&device).expect("event reader must attach");
    // With no flips/vblanks requested, poll must return AGAIN, never block.
    match reader.poll() {
        Ok(_) => {}
        Err(novogfx::Error::Again) => {}
        Err(e) => panic!("expected Ok or Again, got {e:?}"),
    }
}
