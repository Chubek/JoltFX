//! Page-flip between two dumb buffers, waiting for completion events.
//!
//! Usage: `cargo run -p novogfx --example page_flip -- /dev/dri/card0`

use novogfx::prelude::*;
use std::os::fd::{AsFd, AsRawFd};
use std::os::unix::io::RawFd;

fn wait_for_flip(
    device: &Device,
    timeout_ms: i32,
) -> std::result::Result<(), Box<dyn std::error::Error + Send + Sync>> {
    // Poll the device fd until readable, then drain events.
    let fd = device.as_fd().as_raw_fd();
    let ret = poll_fd(fd, timeout_ms)?;
    if ret == 0 {
        return Err("timed out waiting for page-flip event".into());
    }
    let reader = &mut EventReader::new(device)?;
    match reader.poll() {
        Ok(events) => {
            for e in events {
                println!("event: {e:?}");
            }
            Ok(())
        }
        Err(novogfx::Error::Again) => Err("no events after poll".into()),
        Err(e) => Err(e.into()),
    }
}

#[cfg(unix)]
fn poll_fd(
    fd: RawFd,
    timeout_ms: i32,
) -> std::result::Result<i32, Box<dyn std::error::Error + Send + Sync>> {
    unsafe extern "C" {
        fn poll(fds: *mut PollFd, nfds: usize, timeout: i32) -> i32;
    }
    #[repr(C)]
    struct PollFd {
        fd: i32,
        events: i16,
        revents: i16,
    }
    let mut pfd = PollFd {
        fd,
        events: 1, // POLLIN
        revents: 0,
    };
    // SAFETY: `pfd` is a valid single-element array for the duration of the call.
    let ret = unsafe { poll(std::ptr::from_mut(&mut pfd), 1, timeout_ms) };
    if ret < 0 {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(ret)
}

#[cfg(not(unix))]
fn poll_fd(
    _fd: RawFd,
    _timeout_ms: i32,
) -> std::result::Result<i32, Box<dyn std::error::Error + Send + Sync>> {
    Err("poll requires Unix".into())
}

fn main() -> std::result::Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/dri/card0".to_string());
    let mut device = Device::open(&path)?;
    device.enable_atomic()?;
    let res = device.resources()?.clone();
    let crtc_id = res.crtcs.first().copied().ok_or("no CRTCs")?;
    let crtc = novogfx::Crtc::get(&device, novogfx::CrtcId(crtc_id))?;
    let (w, h) = crtc
        .current_mode()
        .map(|m| (u32::from(m.hdisplay), u32::from(m.vdisplay)))
        .unwrap_or((640, 480));
    let mut front = DumbBuffer::new(&device, w, h, 32)?;
    let mut back = DumbBuffer::new(&device, w, h, 32)?;
    front.fill_solid(0x00_FF_00_00, 4)?;
    back.fill_solid(0x00_00_00_FF, 4)?;
    println!(
        "flipping crtc {crtc_id} to front fb {}",
        front.framebuffer_id()
    );
    {
        let mut req = AtomicRequest::new(&mut device);
        req.set_crtc(crtc_id, Some(front.framebuffer_id().raw()), None, None)?;
        req.commit(CommitFlags::ALLOW_MODESET | CommitFlags::PAGE_FLIP_EVENT)?;
    }
    let _ = wait_for_flip(&device, 1000);
    println!("flipping to back fb {}", back.framebuffer_id());
    {
        let mut req = AtomicRequest::new(&mut device);
        req.set_crtc(crtc_id, Some(back.framebuffer_id().raw()), None, None)?;
        req.commit(CommitFlags::ALLOW_MODESET | CommitFlags::PAGE_FLIP_EVENT)?;
    }
    let _ = wait_for_flip(&device, 1000);
    println!("done");
    Ok(())
}
