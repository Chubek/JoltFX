//! Non-blocking DRM event reader with partial-read buffering.

use std::os::fd::{AsFd, AsRawFd};

use novogfx_core::{Device, Error, Result};
use novogfx_sys::drm;

use crate::{HotplugEvent, PageFlipEvent, VblankEvent};

/// A DRM CRTC-sequence notification, distinct from page-flip completion.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct CrtcSequenceEvent {
    /// Full 64-bit vblank sequence number.
    pub sequence: u64,
    /// Kernel monotonic timestamp in nanoseconds.
    pub time_ns: i64,
    /// Request cookie, never dereferenced by the parser.
    pub user_data: u64,
}

impl CrtcSequenceEvent {
    /// Construct from the kernel record's fields.
    #[must_use]
    pub fn from_raw(sequence: u64, time_ns: i64, user_data: u64) -> Self {
        Self {
            sequence,
            time_ns,
            user_data,
        }
    }
}

/// A parsed DRM event.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Event {
    /// Vertical blanking.
    Vblank(VblankEvent),
    /// Page-flip completion.
    PageFlip(PageFlipEvent),
    /// A 64-bit CRTC-sequence notification (not a completed page flip).
    CrtcSequence(CrtcSequenceEvent),
    /// Connection change (derived; see [`HotplugEvent`]).
    Hotplug(HotplugEvent),
}

impl Event {
    /// Monotonic sequence for ordering (vblank/page-flip sequence or hotplug counter).
    #[must_use]
    pub fn sequence(&self) -> u64 {
        match *self {
            Self::Vblank(e) => u64::from(e.sequence),
            Self::PageFlip(e) => u64::from(e.sequence),
            Self::CrtcSequence(e) => e.sequence,
            Self::Hotplug(e) => e.sequence,
        }
    }
}

/// Parse raw `drm_event` bytes into [`Event`]s (pure, fuzzable, no fd).
///
/// Returns the parsed events and the number of bytes consumed. Trailing
/// partial events are left unconsumed for the caller to buffer.
/// Malformed input returns no events and zero consumed bytes; readers should
/// use [`parse_events_checked`] to distinguish corruption from partial reads.
#[must_use]
pub fn parse_events(buf: &[u8], base_sequence: u64) -> (Vec<Event>, usize) {
    parse_events_checked(buf, base_sequence).unwrap_or_default()
}

/// Parse complete records while rejecting corrupt lengths and short known records.
/// Partial valid records are retained for a later read; unknown types are skipped.
pub fn parse_events_checked(buf: &[u8], base_sequence: u64) -> Result<(Vec<Event>, usize)> {
    let mut out = Vec::new();
    let mut off = 0usize;
    while buf.len() - off >= std::mem::size_of::<drm::drm_event>() {
        // SAFETY: bounds checked above; `drm_event` is `#[repr(C)]` u32×2.
        let (type_, length) = unsafe {
            let ptr = buf.as_ptr().add(off).cast::<drm::drm_event>();
            // Use unaligned read: the kernel packs events without padding
            // guarantees beyond the 4-byte header.
            let hdr = ptr.read_unaligned();
            (hdr.type_, hdr.length as usize)
        };
        if length < std::mem::size_of::<drm::drm_event>() || length > 4096 {
            return Err(Error::InvalidArgument("invalid DRM event length".into()));
        }
        let minimum = match type_ {
            drm::DRM_EVENT_VBLANK | drm::DRM_EVENT_FLIP_COMPLETE => {
                std::mem::size_of::<drm::drm_event_vblank>()
            }
            drm::DRM_EVENT_CRTC_SEQUENCE => std::mem::size_of::<drm::drm_event_crtc_sequence>(),
            _ => std::mem::size_of::<drm::drm_event>(),
        };
        if length < minimum {
            return Err(Error::InvalidArgument("short DRM event record".into()));
        }
        if buf.len() - off < length {
            // Partial event: wait for more bytes.
            break;
        }
        match type_ {
            drm::DRM_EVENT_VBLANK => {
                if length >= std::mem::size_of::<drm::drm_event_vblank>() {
                    // SAFETY: length validated above; slice is in-bounds.
                    let ev = unsafe {
                        buf.as_ptr()
                            .add(off)
                            .cast::<drm::drm_event_vblank>()
                            .read_unaligned()
                    };
                    out.push(Event::Vblank(VblankEvent::from_raw(
                        ev.sequence,
                        ev.tv_sec,
                        ev.tv_usec,
                        ev.crtc_id,
                        ev.user_data,
                    )));
                }
            }
            drm::DRM_EVENT_FLIP_COMPLETE => {
                if length >= std::mem::size_of::<drm::drm_event_vblank>() {
                    // SAFETY: same bounds argument as above.
                    let ev = unsafe {
                        buf.as_ptr()
                            .add(off)
                            .cast::<drm::drm_event_vblank>()
                            .read_unaligned()
                    };
                    out.push(Event::PageFlip(PageFlipEvent::from_raw(
                        ev.sequence,
                        ev.tv_sec,
                        ev.tv_usec,
                        ev.crtc_id,
                        ev.user_data,
                        0,
                    )));
                }
            }
            drm::DRM_EVENT_CRTC_SEQUENCE => {
                if length >= std::mem::size_of::<drm::drm_event_crtc_sequence>() {
                    // SAFETY: length validated; slice in-bounds.
                    let ev = unsafe {
                        buf.as_ptr()
                            .add(off)
                            .cast::<drm::drm_event_crtc_sequence>()
                            .read_unaligned()
                    };
                    out.push(Event::CrtcSequence(CrtcSequenceEvent::from_raw(
                        ev.sequence,
                        ev.time_ns,
                        ev.user_data,
                    )));
                }
            }
            _ => {
                // Unknown / chipset-specific event: skip by length. Hotplug is
                // derived at a higher layer (see `HotplugEvent` docs).
                let _ = base_sequence;
            }
        }
        off += length;
    }
    Ok((out, off))
}

/// Reads DRM events from the device fd without blocking.
pub struct EventReader<'a> {
    device: &'a Device,
    pending: Vec<u8>,
    events: Vec<Event>,
    sequence: u64,
}

impl<'a> EventReader<'a> {
    /// Attach to `device`.
    ///
    /// The fd is switched to non-blocking mode (shared with the device;
    /// `ioctl`s are unaffected, only `read(2)` semantics change).
    pub fn new(device: &'a Device) -> Result<Self> {
        set_nonblocking(device.as_fd().as_raw_fd())?;
        Ok(Self {
            device,
            pending: Vec::new(),
            events: Vec::new(),
            sequence: 0,
        })
    }

    /// Read zero or more events without blocking.
    ///
    /// On success returns the queued events, valid until the next `poll`.
    /// Returns [`Error::Again`] when no events are pending.
    pub fn poll(&mut self) -> Result<&[Event]> {
        // Results from a previous poll are never replayed on EAGAIN.
        self.events.clear();
        let fd = self.device.as_fd().as_raw_fd();
        let mut buf = [0u8; 4096];
        // SAFETY: `buf` is a stack buffer; `read` writes at most its length.
        let n = unsafe { raw_read(fd, buf.as_mut_ptr(), buf.len()) };
        if n < 0 {
            let errno = last_errno();
            // EAGAIN/EWOULDBLOCK = no events pending.
            if errno == 11 {
                // Buffered partial data cannot complete without new bytes.
                return Err(Error::Again);
            } else {
                return Err(Error::Io(std::io::Error::from_raw_os_error(errno)));
            }
        } else if n > 0 {
            self.pending.extend_from_slice(&buf[..n as usize]);
        }
        // Parse what we can; retain the trailing partial event.
        let (parsed, consumed) = match parse_events_checked(&self.pending, self.sequence) {
            Ok(parsed) => parsed,
            Err(error) => {
                self.pending.clear();
                return Err(error);
            }
        };
        self.pending.drain(..consumed);
        // Bound the pending buffer against garbage floods.
        if self.pending.len() > 65536 {
            self.pending.clear();
            return Err(Error::Internal(
                "DRM event stream desynchronised; buffer reset".to_string(),
            ));
        }
        self.sequence = self.sequence.saturating_add(parsed.len() as u64);
        self.events = parsed;
        if self.events.is_empty() {
            return Err(Error::Again);
        }
        Ok(&self.events)
    }

    /// Number of unparsed bytes buffered from partial reads.
    #[must_use]
    pub fn pending_bytes(&self) -> usize {
        self.pending.len()
    }
}

#[cfg(unix)]
fn set_nonblocking(fd: std::os::fd::RawFd) -> Result<()> {
    unsafe extern "C" {
        fn fcntl(fd: std::os::fd::RawFd, cmd: std::ffi::c_int, ...) -> std::ffi::c_int;
    }
    const F_GETFL: std::ffi::c_int = 3;
    const F_SETFL: std::ffi::c_int = 4;
    // SAFETY: fd is owned by the Device and valid for the reader lifetime.
    let flags = unsafe { fcntl(fd, F_GETFL) };
    if flags < 0 {
        return Err(Error::last_os_error("fcntl(F_GETFL)"));
    }
    const O_NONBLOCK: std::ffi::c_int = 0o4000;
    // SAFETY: setting O_NONBLOCK on a DRM fd affects only read(2), not ioctls.
    let ret = unsafe { fcntl(fd, F_SETFL, flags | O_NONBLOCK) };
    if ret < 0 {
        return Err(Error::last_os_error("fcntl(F_SETFL, O_NONBLOCK)"));
    }
    Ok(())
}

#[cfg(not(unix))]
fn set_nonblocking(_fd: std::os::fd::RawFd) -> Result<()> {
    Err(novogfx_core::Error::NotSupported(
        "non-blocking events require Unix".to_string(),
    ))
}

#[cfg(unix)]
unsafe fn raw_read(fd: std::os::fd::RawFd, buf: *mut u8, len: usize) -> isize {
    unsafe extern "C" {
        fn read(fd: std::os::fd::RawFd, buf: *mut std::ffi::c_void, count: usize) -> isize;
    }
    // SAFETY: `buf`/`len` describe a valid stack buffer at the call site.
    unsafe { read(fd, buf.cast::<std::ffi::c_void>(), len) }
}

#[cfg(not(unix))]
unsafe fn raw_read(_fd: std::os::fd::RawFd, _buf: *mut u8, _len: usize) -> isize {
    -1
}

#[cfg(unix)]
fn last_errno() -> i32 {
    std::io::Error::last_os_error().raw_os_error().unwrap_or(11)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checked_parser_distinguishes_corruption_from_partial_records() {
        let partial = vblank_bytes(1, 2, 3);
        assert_eq!(parse_events_checked(&partial[..17], 0).unwrap().1, 0);
        for length in [0u32, 7, 8, 4097] {
            let mut bytes = 1u32.to_ne_bytes().to_vec();
            bytes.extend_from_slice(&length.to_ne_bytes());
            assert!(parse_events_checked(&bytes, 0).is_err());
        }
        let mut unknown = 1234u32.to_ne_bytes().to_vec();
        unknown.extend_from_slice(&8u32.to_ne_bytes());
        assert_eq!(parse_events_checked(&unknown, 0).unwrap().1, 8);
    }

    #[test]
    fn crtc_sequences_preserve_64_bits_and_are_not_page_flips() {
        let sequence = u64::from(u32::MAX) + 42;
        let mut bytes = drm::DRM_EVENT_CRTC_SEQUENCE.to_ne_bytes().to_vec();
        bytes.extend_from_slice(&32u32.to_ne_bytes());
        bytes.extend_from_slice(&0xdeadbeefu64.to_ne_bytes());
        bytes.extend_from_slice(&1234567890i64.to_ne_bytes());
        bytes.extend_from_slice(&sequence.to_ne_bytes());
        let (events, consumed) = parse_events_checked(&bytes, 0).unwrap();
        assert_eq!(consumed, bytes.len());
        assert_eq!(
            events,
            vec![Event::CrtcSequence(CrtcSequenceEvent::from_raw(
                sequence, 1234567890, 0xdeadbeef
            ))]
        );
        assert_eq!(events[0].sequence(), sequence);
    }

    fn vblank_bytes(seq: u32, crtc: u32, user: u64) -> Vec<u8> {
        let ev = drm::drm_event_vblank {
            base: drm::drm_event {
                type_: drm::DRM_EVENT_VBLANK,
                length: std::mem::size_of::<drm::drm_event_vblank>() as u32,
            },
            user_data: user,
            tv_sec: 1000,
            tv_usec: 500,
            sequence: seq,
            crtc_id: crtc,
        };
        // SAFETY: `ev` is a plain `#[repr(C)]` value; byte-copy is exact.
        unsafe {
            std::slice::from_raw_parts(
                std::ptr::from_ref(&ev).cast::<u8>(),
                std::mem::size_of_val(&ev),
            )
            .to_vec()
        }
    }

    #[test]
    fn parses_single_vblank() {
        let b = vblank_bytes(42, 33, 0xdead);
        let (events, consumed) = parse_events(&b, 0);
        assert_eq!(consumed, b.len());
        assert_eq!(events.len(), 1);
        match events[0] {
            Event::Vblank(e) => {
                assert_eq!(e.sequence, 42);
                assert_eq!(e.crtc_id, 33);
                assert_eq!(e.user_data, 0xdead);
            }
            other => panic!("expected vblank, got {other:?}"),
        }
    }

    #[test]
    fn partial_reads_are_buffered() {
        let b = vblank_bytes(7, 1, 0);
        let half = b.len() / 2;
        let (e1, c1) = parse_events(&b[..half], 0);
        assert!(e1.is_empty());
        assert_eq!(c1, 0);
        let (e2, c2) = parse_events(&b, 0);
        assert_eq!(e2.len(), 1);
        assert_eq!(c2, b.len());
    }

    #[test]
    fn corrupt_length_does_not_panic() {
        // Zero-length header: parser must stop, not loop or panic.
        let b = [0u8; 64];
        let (events, consumed) = parse_events(&b, 0);
        assert!(events.is_empty());
        assert_eq!(consumed, 0);
        // Truncated header.
        let (events, consumed) = parse_events(&[1u8, 2, 3], 0);
        assert!(events.is_empty());
        assert_eq!(consumed, 0);
    }

    #[test]
    fn fuzz_bytes_never_panic() {
        let base = vblank_bytes(1, 2, 3);
        for i in 0..base.len() {
            let mut m = base.clone();
            m[i] = m[i].wrapping_add(17);
            let _ = parse_events(&m, 0);
        }
        let _ = parse_events(&[], 0);
        let _ = parse_events(&[0xFF; 128], 0);
    }

    #[test]
    fn event_sequence_orders() {
        let v = Event::Vblank(VblankEvent::from_raw(5, 0, 0, 0, 0));
        let p = Event::PageFlip(PageFlipEvent::from_raw(9, 0, 0, 0, 0, 0));
        let h = Event::Hotplug(HotplugEvent::from_raw(0, 100));
        assert_eq!(v.sequence(), 5);
        assert_eq!(p.sequence(), 9);
        assert_eq!(h.sequence(), 100);
    }
}
