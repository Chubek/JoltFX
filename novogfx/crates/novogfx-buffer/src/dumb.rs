//! CPU-mappable dumb buffers.
//!
//! Lifecycle (see crate docs):
//! `CREATE_DUMB → MAP_DUMB → mmap → ADDFB2` on creation, unwound on failure;
//! `RMFB → munmap → DESTROY_DUMB` on drop.

use std::os::fd::{AsFd, AsRawFd, OwnedFd};
use std::ptr::NonNull;

#[cfg(test)]
use mock as xf86drm;
use novogfx_core::{Device, Error, Result};
#[cfg(not(test))]
use novogfx_sys::xf86drm;

use crate::FramebufferId;

/// A CPU-mappable dumb buffer with its registered framebuffer.
///
/// `Send` but not `Sync`: the mmap region is mutable through `&mut self`
/// only. Never expose `&[u8]` aliasing the mapping.
pub struct DumbBuffer {
    fd: OwnedFd,
    handle: u32,
    fb_id: u32,
    width: u32,
    height: u32,
    pitch: u32,
    size: u64,
    map: NonNull<u8>,
    map_len: usize,
    bpp: u32,
}

// SAFETY: `DumbBuffer` owns its GEM handle, framebuffer id, and mmap region;
// the OwnedFd duplicates the same file description, retaining GEM ownership
// even when Device drops. Exclusive mapping access requires &mut self.
unsafe impl Send for DumbBuffer {}

impl std::fmt::Debug for DumbBuffer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DumbBuffer")
            .field("handle", &self.handle)
            .field("fb_id", &self.fb_id)
            .field("width", &self.width)
            .field("height", &self.height)
            .field("pitch", &self.pitch)
            .field("size", &self.size)
            .finish_non_exhaustive()
    }
}

impl DumbBuffer {
    /// Create a dumb buffer of `width × height` at `bpp` bits per pixel.
    ///
    /// Only `bpp` 16 and 32 are accepted (the kernel supports 8/16/24/32,
    /// but 8-bit modes are driver-dependent and 24-bit has pitch pitfalls).
    pub fn new(device: &Device, width: u32, height: u32, bpp: u32) -> Result<Self> {
        validate_geometry(width, height, bpp)?;
        Self::create(device.as_fd().try_clone_to_owned()?, width, height, bpp)
    }

    fn create(fd: OwnedFd, width: u32, height: u32, bpp: u32) -> Result<Self> {
        // Step 1: CREATE_DUMB.
        let mut handle: u32 = 0;
        let mut pitch: u32 = 0;
        let mut size: u64 = 0;
        // SAFETY: fd valid; out-params on the stack.
        let ret = unsafe {
            xf86drm::drmModeCreateDumbBuffer(
                fd.as_raw_fd(),
                width,
                height,
                bpp,
                0,
                &mut handle,
                &mut pitch,
                &mut size,
            )
        };
        if ret != 0 {
            return Err(Error::from_neg_errno(ret, "drmModeCreateDumbBuffer"));
        }
        let mut this = Self {
            fd,
            handle,
            fb_id: 0,
            width,
            height,
            pitch,
            size,
            map: NonNull::dangling(),
            map_len: 0,
            bpp,
        };
        // Step 2..4 with unwind on failure.
        // Drop is the single cleanup owner, including every failure path.
        this.map_and_register(bpp)?;
        Ok(this)
    }

    fn map_and_register(&mut self, bpp: u32) -> Result<()> {
        self.map_region()?;
        self.register_framebuffer(bpp)
    }

    fn map_region(&mut self) -> Result<()> {
        // Step 2: MAP_DUMB → fake offset.
        let mut offset: u64 = 0;
        // SAFETY: fd/handle valid; offset is a stack out-param.
        let ret =
            unsafe { xf86drm::drmModeMapDumbBuffer(self.fd.as_raw_fd(), self.handle, &mut offset) };
        if ret != 0 {
            return Err(Error::from_neg_errno(ret, "drmModeMapDumbBuffer"));
        }
        // Step 3: mmap the fake offset.
        let len = validate_layout(self.width, self.height, self.bpp, self.pitch, self.size)?;
        if offset > i64::MAX as u64 {
            return Err(Error::InvalidArgument(
                "mapping offset exceeds off_t".into(),
            ));
        }
        // SAFETY: `offset` came from the kernel for this GEM handle; `len`
        // is the kernel-reported size. PROT_READ|PROT_WRITE, MAP_SHARED.
        let ptr = unsafe { mmap_dumb(self.fd.as_raw_fd(), offset, len) }?;
        self.map = ptr;
        self.map_len = len;
        Ok(())
    }

    fn register_framebuffer(&mut self, bpp: u32) -> Result<()> {
        // Step 4: ADDFB2.
        let format = if bpp == 32 {
            novogfx_sys::drm_mode::fourcc_code(b'X', b'R', b'2', b'4')
        } else {
            novogfx_sys::drm_mode::fourcc_code(b'R', b'G', b'1', b'6')
        };
        let handles = [self.handle, 0, 0, 0];
        let pitches = [self.pitch, 0, 0, 0];
        let offsets = [0u32, 0, 0, 0];
        let mut fb_id: u32 = 0;
        // SAFETY: arrays are stack-resident for the duration of the call.
        let ret = unsafe {
            xf86drm::drmModeAddFB2(
                self.fd.as_raw_fd(),
                self.width,
                self.height,
                format,
                handles.as_ptr(),
                pitches.as_ptr(),
                offsets.as_ptr(),
                &mut fb_id,
                0,
            )
        };
        if ret != 0 {
            return Err(Error::from_neg_errno(ret, "drmModeAddFB2"));
        }
        self.fb_id = fb_id;
        Ok(())
    }

    /// GEM handle (informational).
    #[must_use]
    pub fn handle(&self) -> u32 {
        self.handle
    }

    /// Framebuffer id for KMS commits.
    #[must_use]
    pub fn framebuffer_id(&self) -> FramebufferId {
        FramebufferId(self.fb_id)
    }

    /// Width in pixels.
    #[must_use]
    pub fn width(&self) -> u32 {
        self.width
    }

    /// Height in pixels.
    #[must_use]
    pub fn height(&self) -> u32 {
        self.height
    }

    /// Pitch (bytes per row).
    #[must_use]
    pub fn pitch(&self) -> u32 {
        self.pitch
    }

    /// Total mapped size in bytes.
    #[must_use]
    pub fn size(&self) -> u64 {
        self.size
    }

    /// Map (or reuse) the CPU view without recreating the framebuffer.
    ///
    /// The returned borrow prevents unmapping while Rust callers use it.
    pub fn map(&mut self) -> Result<&mut [u8]> {
        if self.map_len == 0 {
            self.map_region()?;
        }
        Ok(self.as_mut_slice())
    }

    /// Release the CPU mapping while retaining the GEM handle and framebuffer.
    /// Idempotent; a later [`DumbBuffer::map`] recreates the mapping.
    pub fn unmap(&mut self) -> Result<()> {
        if self.map_len != 0 {
            // SAFETY: this object exclusively owns the current mapping.
            unsafe { munmap_dumb(self.map, self.map_len) }?;
            self.map = NonNull::dangling();
            self.map_len = 0;
        }
        Ok(())
    }

    /// Full mapped region, or an empty slice after [`DumbBuffer::unmap`].
    ///
    /// Callers must respect [`DumbBuffer::pitch`] when writing rows: only
    /// `width * bpp/8` bytes per row are visible; pitch padding must not be
    /// treated as pixels.
    #[must_use]
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        // SAFETY: `map`/`map_len` describe the live mmap region owned by
        // `self`; `&mut self` guarantees exclusive access.
        unsafe { std::slice::from_raw_parts_mut(self.map.as_ptr(), self.map_len) }
    }

    /// Fill the visible region with a solid ARGB32/XRGB32 colour.
    ///
    /// Pure stride math over the mapping; respects pitch.
    pub fn fill_solid(&mut self, argb: u32, bytes_per_pixel: usize) -> Result<()> {
        if bytes_per_pixel != (self.bpp / 8) as usize {
            return Err(Error::InvalidArgument(format!(
                "bytes_per_pixel does not match buffer depth: {bytes_per_pixel}"
            )));
        }
        let pitch = self.pitch as usize;
        let row_bytes = self.width as usize * bytes_per_pixel;
        if row_bytes > pitch {
            return Err(Error::Internal(format!(
                "row width {row_bytes} exceeds pitch {pitch}"
            )));
        }
        let (w, h) = (self.width as usize, self.height as usize);
        let slice = self.map()?;
        if bytes_per_pixel == 4 {
            let words = argb.to_le_bytes();
            for y in 0..h {
                let base = y * pitch;
                for x in 0..w {
                    let off = base + x * 4;
                    slice[off..off + 4].copy_from_slice(&words);
                }
            }
        } else {
            let v = (argb & 0xFFFF) as u16;
            let words = v.to_le_bytes();
            for y in 0..h {
                let base = y * pitch;
                for x in 0..w {
                    let off = base + x * 2;
                    slice[off..off + 2].copy_from_slice(&words);
                }
            }
        }
        Ok(())
    }
}

impl Drop for DumbBuffer {
    fn drop(&mut self) {
        // Reverse order: RMFB → munmap → DESTROY_DUMB. Errors are ignored
        // (destructors must not fail); the fds/handles are best-effort
        // released. OwnedFd closes after all kernel objects are released.
        if self.fb_id != 0 {
            // SAFETY: `fb_id` was registered by us on `fd`.
            let _ = unsafe { xf86drm::drmModeRmFB(self.fd.as_raw_fd(), self.fb_id) };
            self.fb_id = 0;
        }
        let _ = self.unmap();
        if self.handle != 0 {
            // SAFETY: handle owned by `self`.
            let _ = unsafe { xf86drm::drmModeDestroyDumbBuffer(self.fd.as_raw_fd(), self.handle) };
            self.handle = 0;
        }
    }
}

/// Validate geometry without a device (unit-testable).
fn validate_layout(width: u32, height: u32, bpp: u32, pitch: u32, size: u64) -> Result<usize> {
    let row = u64::from(width) * u64::from(bpp / 8);
    let required = u64::from(pitch) * u64::from(height);
    if u64::from(pitch) < row || size < required || size == 0 || size > isize::MAX as u64 {
        return Err(Error::InvalidArgument(
            "invalid kernel buffer layout".into(),
        ));
    }
    usize::try_from(size)
        .map_err(|_| Error::InvalidArgument("mapping size exceeds address space".into()))
}

/// Validate geometry without a device (unit-testable).
pub(crate) fn validate_geometry(width: u32, height: u32, bpp: u32) -> Result<()> {
    if width == 0 || height == 0 {
        return Err(Error::InvalidArgument(
            "width and height must be non-zero".to_string(),
        ));
    }
    if width > 16384 || height > 16384 {
        return Err(Error::InvalidArgument(format!(
            "dimensions {width}x{height} exceed the 16384px limit"
        )));
    }
    if bpp != 16 && bpp != 32 {
        return Err(Error::InvalidArgument(format!(
            "bpp must be 16 or 32, got {bpp}"
        )));
    }
    Ok(())
}

// --- mmap/munmap via the system C library ------------------------------------
// Linked directly (no `libc` crate needed): `mmap`/`munmap` are part of the
// platform ABI on all Unix targets novogfx supports.

#[cfg(unix)]
unsafe extern "C" {
    fn mmap(
        addr: *mut std::ffi::c_void,
        len: usize,
        prot: std::ffi::c_int,
        flags: std::ffi::c_int,
        fd: std::os::fd::RawFd,
        offset: i64,
    ) -> *mut std::ffi::c_void;
    fn munmap(addr: *mut std::ffi::c_void, len: usize) -> std::ffi::c_int;
}

#[cfg(unix)]
unsafe fn mmap_dumb(fd: std::os::fd::RawFd, offset: u64, len: usize) -> Result<NonNull<u8>> {
    const PROT_READ: std::ffi::c_int = 0x1;
    const PROT_WRITE: std::ffi::c_int = 0x2;
    const MAP_SHARED: std::ffi::c_int = 0x01;
    // SAFETY: `offset` came from MAP_DUMB for this GEM handle; `len` is the
    // kernel-reported size. PROT_READ|PROT_WRITE, MAP_SHARED, offset page-aligned
    // by the kernel.
    let ptr = unsafe {
        mmap(
            std::ptr::null_mut(),
            len,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            offset as i64,
        )
    };
    if ptr as isize == -1 {
        return Err(Error::last_os_error("mmap(dumb)"));
    }
    if ptr.is_null() {
        return Err(Error::OutOfMemory("mmap returned NULL".to_string()));
    }
    // SAFETY: kernel returned a live mapping of `len` bytes.
    Ok(unsafe { NonNull::new_unchecked(ptr.cast::<u8>()) })
}

#[cfg(unix)]
unsafe fn munmap_dumb(ptr: NonNull<u8>, len: usize) -> Result<()> {
    // SAFETY: `ptr`/`len` are a live mapping owned by the caller.
    let ret = unsafe { munmap(ptr.as_ptr().cast::<std::ffi::c_void>(), len) };
    if ret == 0 {
        Ok(())
    } else {
        Err(Error::last_os_error("munmap(dumb)"))
    }
}

#[cfg(not(unix))]
unsafe fn mmap_dumb(_fd: std::os::fd::RawFd, _offset: u64, _len: usize) -> Result<NonNull<u8>> {
    Err(Error::NotSupported(
        "dumb-buffer mmap is only supported on Unix".to_string(),
    ))
}

#[cfg(not(unix))]
unsafe fn munmap_dumb(_ptr: NonNull<u8>, _len: usize) -> Result<()> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_truncated_or_unaddressable_mapping() {
        assert_eq!(validate_layout(64, 64, 32, 256, 16384).unwrap(), 16384);
        assert!(validate_layout(64, 64, 32, 255, 16384).is_err());
        assert!(validate_layout(64, 64, 32, 256, 16383).is_err());
        assert!(validate_layout(64, 64, 32, 256, u64::MAX).is_err());
    }

    #[test]
    fn lifecycle_unwinds_once_and_retains_fd() {
        for failure in ["create", "map", "add", ""] {
            *mock::STATE.lock().unwrap() = (failure, Vec::new());
            let file = std::fs::OpenOptions::new()
                .read(true)
                .write(true)
                .open("/dev/zero")
                .unwrap();
            let duplicate = file.as_fd().try_clone_to_owned().unwrap();
            drop(file);
            let result = DumbBuffer::create(duplicate, 64, 64, 32);
            if failure.is_empty() {
                let mut buffer = result.unwrap();
                buffer.fill_solid(0x12345678, 4).unwrap();
                assert_eq!(&buffer.as_mut_slice()[..4], &[0x78, 0x56, 0x34, 0x12]);
                assert!(buffer.fill_solid(0, 2).is_err());
                let old_fb = buffer.framebuffer_id();
                buffer.unmap().unwrap();
                assert!(buffer.as_mut_slice().is_empty());
                buffer.unmap().unwrap();
                assert_eq!(buffer.map().unwrap().len(), 16384);
                assert_eq!(buffer.framebuffer_id(), old_fb);
                buffer.fill_solid(0xabcdef, 4).unwrap();
                drop(buffer);
            } else {
                assert!(result.is_err());
            }
            let state = mock::STATE.lock().unwrap();
            let expected: &[&str] = match failure {
                "create" => &["create"],
                "map" => &["create", "map", "destroy"],
                "add" => &["create", "map", "add", "destroy"],
                _ => &["create", "map", "add", "map", "rm", "destroy"],
            };
            assert_eq!(state.1, expected);
        }
    }

    #[test]
    fn geometry_validation() {
        assert!(validate_geometry(1920, 1080, 32).is_ok());
        assert!(validate_geometry(1920, 1080, 16).is_ok());
        assert!(validate_geometry(0, 1080, 32).is_err());
        assert!(validate_geometry(1920, 0, 32).is_err());
        assert!(validate_geometry(20000, 1080, 32).is_err());
        assert!(validate_geometry(1920, 1080, 24).is_err());
        assert!(validate_geometry(1920, 1080, 8).is_err());
    }
}

#[cfg(test)]
#[allow(non_snake_case)]
mod mock {
    pub static STATE: std::sync::Mutex<(&str, Vec<&str>)> = std::sync::Mutex::new(("", Vec::new()));
    fn call(name: &'static str) -> i32 {
        let mut state = STATE.lock().unwrap();
        state.1.push(name);
        if state.0 == name {
            -5
        } else {
            0
        }
    }
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn drmModeCreateDumbBuffer(
        fd: i32,
        _: u32,
        _: u32,
        _: u32,
        _: u32,
        handle: *mut u32,
        pitch: *mut u32,
        size: *mut u64,
    ) -> i32 {
        assert!(fd >= 0);
        let ret = call("create");
        if ret == 0 {
            // SAFETY: out-pointers are stack locals provided by create().
            unsafe {
                *handle = 42;
                *pitch = 256;
                *size = 16384;
            }
        }
        ret
    }
    pub unsafe fn drmModeMapDumbBuffer(_: i32, _: u32, offset: *mut u64) -> i32 {
        // SAFETY: offset is a valid stack out-parameter.
        unsafe {
            *offset = 0;
        }
        call("map")
    }
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn drmModeAddFB2(
        _: i32,
        _: u32,
        _: u32,
        _: u32,
        _: *const u32,
        _: *const u32,
        _: *const u32,
        fb: *mut u32,
        _: u32,
    ) -> i32 {
        // SAFETY: fb is a valid stack out-parameter.
        unsafe {
            *fb = 7;
        }
        call("add")
    }
    pub unsafe fn drmModeRmFB(_: i32, _: u32) -> i32 {
        call("rm")
    }
    pub unsafe fn drmModeDestroyDumbBuffer(_: i32, _: u32) -> i32 {
        call("destroy")
    }
}
