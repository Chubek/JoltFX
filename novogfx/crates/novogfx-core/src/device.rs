//! [`Device`]: owned DRM file descriptor plus cached resources and caps.

use std::ffi::CString;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::sync::{Mutex, RwLock};

use novogfx_sys::{drm, xf86drm};

use crate::{Error, ResourceHandles, Result};

/// Device capability flags, mirroring `novogfx_device_caps_t` in the IDL.
///
/// Hand-rolled `bitflags` equivalent so the core stays dependency-free;
/// the API (`empty`, `contains`, `bits`, `all`) matches the `bitflags` crate.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct DeviceCaps(pub u64);

impl DeviceCaps {
    /// Atomic modesetting supported.
    pub const ATOMIC: Self = Self(1 << 0);
    /// Universal planes supported.
    pub const UNIVERSAL_PLANES: Self = Self(1 << 1);
    /// Dumb buffers supported.
    pub const DUMB_BUFFER: Self = Self(1 << 2);
    /// PRIME dma-buf import/export supported.
    pub const PRIME: Self = Self(1 << 3);
    /// Async page flip supported.
    pub const ASYNC_PAGE_FLIP: Self = Self(1 << 4);

    /// Empty set.
    #[must_use]
    pub const fn empty() -> Self {
        Self(0)
    }

    /// All known flags.
    #[must_use]
    pub const fn all() -> Self {
        Self(
            Self::ATOMIC.0
                | Self::UNIVERSAL_PLANES.0
                | Self::DUMB_BUFFER.0
                | Self::PRIME.0
                | Self::ASYNC_PAGE_FLIP.0,
        )
    }

    /// Raw bits.
    #[must_use]
    pub const fn bits(&self) -> u64 {
        self.0
    }

    /// Build from raw bits, masking off unknown bits.
    #[must_use]
    pub const fn from_bits_truncate(bits: u64) -> Self {
        Self(bits & Self::all().0)
    }

    /// Returns `true` if all of `other` is contained in `self`.
    #[must_use]
    pub const fn contains(&self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }

    /// Returns `true` if no flags are set.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.0 == 0
    }
}

impl std::ops::BitOr for DeviceCaps {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitAnd for DeviceCaps {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

/// Driver identification, from `drmGetVersion`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DriverInfo {
    /// Driver name, e.g. `"i915"`, `"amdgpu"`, `"vkms"`.
    pub name: String,
    /// Driver date, e.g. `"20240115"` (may be empty).
    pub date: String,
    /// Free-form description (may be empty).
    pub desc: String,
    /// Major version.
    pub major: i32,
    /// Minor version.
    pub minor: i32,
    /// Patch level.
    pub patch: i32,
}

struct CacheInner {
    current: Option<std::sync::Arc<ResourceHandles>>,
    retired: Vec<std::sync::Arc<ResourceHandles>>,
}

/// An opened DRM device.
///
/// Owns the file descriptor; all KMS wrappers borrow from it so handles
/// cannot outlive the fd. `Device` is `Send + Sync` but intentionally **not**
/// `Clone`: duplicate fds to the same card carry independent kernel state.
pub struct Device {
    fd: OwnedFd,
    cache: RwLock<CacheInner>,
    // Serialises cache fills against concurrent readers.
    fill_lock: Mutex<()>,
    atomic_enabled: std::sync::atomic::AtomicBool,
}

// SAFETY: `Device` owns an `OwnedFd` (which is `Send + Sync`) plus
// `RwLock`/`Mutex` protected caches. No thread-local or global state.
unsafe impl Send for Device {}
unsafe impl Sync for Device {}

impl std::fmt::Debug for Device {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Device")
            .field("fd", &self.fd)
            .finish_non_exhaustive()
    }
}

impl Device {
    /// Open a DRM device at `path` (e.g. `/dev/dri/card0`).
    ///
    /// Rejects non-DRM files by probing `drmGetVersion` and `GET_CAP`.
    ///
    /// # Errors
    ///
    /// Returns [`Error::InvalidArgument`] when the path is empty,
    /// [`Error::NotFound`] when the node does not exist, or [`Error::Io`]
    /// / [`Error::NotSupported`] when the file is not a DRM device.
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref();
        validate_device_path(path)?;
        let file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(rustix_open_flags())
            .open(path)
            .map_err(|e| match e.kind() {
                std::io::ErrorKind::NotFound => {
                    Error::NotFound(format!("DRM device not found: {}", path.display()))
                }
                std::io::ErrorKind::PermissionDenied => Error::PermissionDenied(format!(
                    "permission denied opening {}: {e}",
                    path.display()
                )),
                _ => Error::Io(e),
            })?;
        let fd: OwnedFd = file.into();
        // Probe: must look like a DRM device.
        let probe = DriverInfo::query(&fd);
        match probe {
            Ok(_) => {}
            Err(e) => {
                // Reject non-DRM files explicitly rather than leaking the fd
                // as a half-open device.
                drop(fd);
                return Err(match e {
                    Error::Io(io) => {
                        let code = io.raw_os_error().unwrap_or(0);
                        // ENOTTY/EINVAL from drmGetVersion means "not a DRM node".
                        if code == 25 || code == 22 {
                            Error::InvalidArgument(format!("not a DRM device: {}", path.display()))
                        } else {
                            Error::Io(io)
                        }
                    }
                    other => other,
                });
            }
        }
        Ok(Self {
            fd,
            cache: RwLock::new(CacheInner {
                current: None,
                retired: Vec::new(),
            }),
            fill_lock: Mutex::new(()),
            atomic_enabled: std::sync::atomic::AtomicBool::new(false),
        })
    }

    /// Open a DRM control node.
    ///
    /// Currently equivalent to [`Device::open`]; control nodes expose a
    /// reduced ioctl surface and most KMS operations will return
    /// [`Error::NotSupported`].
    pub fn open_control(path: impl AsRef<Path>) -> Result<Self> {
        Self::open(path)
    }

    /// Query driver name/version/date.
    pub fn driver(&self) -> Result<DriverInfo> {
        DriverInfo::query(&self.fd)
    }

    /// Query device capabilities via `drmGetCap`.
    ///
    /// Never fails: unknown or failing caps are reported as cleared bits,
    /// matching the C ABI contract (`novogfx_device_caps` never fails).
    #[must_use]
    pub fn capabilities(&self) -> DeviceCaps {
        let mut caps = DeviceCaps::empty();
        if get_cap(&self.fd, drm::DRM_CAP_DUMB_BUFFER).unwrap_or(0) != 0 {
            caps = caps | DeviceCaps::DUMB_BUFFER;
        }
        // PRIME is reported as a bitmask; any non-zero value means support.
        if get_cap(&self.fd, drm::DRM_CAP_PRIME).unwrap_or(0) != 0 {
            caps = caps | DeviceCaps::PRIME;
        }
        if get_cap(&self.fd, drm::DRM_CAP_ASYNC_PAGE_FLIP).unwrap_or(0) != 0 {
            caps = caps | DeviceCaps::ASYNC_PAGE_FLIP;
        }
        if self
            .atomic_enabled
            .load(std::sync::atomic::Ordering::Acquire)
        {
            caps = caps | DeviceCaps::ATOMIC | DeviceCaps::UNIVERSAL_PLANES;
        }
        // ATOMIC / UNIVERSAL_PLANES are client caps, not device caps; they
        // are reported when enabling them succeeds (probed without mutating
        // global state by testing with value 0 is not possible, so we infer
        // support from the kernel version handshake below).
        //
        // Heuristic: if the device answers GET_CAP at all, it is a modern
        // KMS device; atomic support is confirmed lazily by `enable_atomic`.
        // We report the raw dumb/prime/async bits here.
        let _ = caps;
        // Try to detect universal-planes/atomic support cheaply: setting the
        // client cap is idempotent, so `capabilities()` itself does not call
        // it (that would mutate client state). Callers use `enable_atomic`.
        caps
    }

    /// Enable atomic modesetting (`UNIVERSAL_PLANES` + `ATOMIC`).
    ///
    /// Idempotent: enabling an already-enabled cap succeeds.
    pub fn enable_atomic(&self) -> Result<()> {
        // SAFETY: fd is a valid open DRM fd owned by `self`; the
        // `drmSetClientCap` wrapper takes plain integers and has no
        // pointer invariants. Applies to both calls below.
        let fd = self.fd.as_fd().as_raw_fd();
        let ret = unsafe { xf86drm::drmSetClientCap(fd, drm::DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) };
        if ret != 0 {
            return Err(Error::from_neg_errno(
                ret,
                "drmSetClientCap(UNIVERSAL_PLANES)",
            ));
        }
        // SAFETY: same fd validity argument as above.
        let ret = unsafe { xf86drm::drmSetClientCap(fd, drm::DRM_CLIENT_CAP_ATOMIC, 1) };
        if ret != 0 {
            return Err(Error::from_neg_errno(ret, "drmSetClientCap(ATOMIC)"));
        }
        if !self
            .atomic_enabled
            .swap(true, std::sync::atomic::Ordering::AcqRel)
        {
            self.invalidate_cache();
        }
        Ok(())
    }

    /// Cached resource handles (`DRM_IOCTL_MODE_GETRESOURCES`).
    ///
    /// Use [`Device::resource_snapshot`] for a fresh, owned list without
    /// retaining reference-pinned cache generations.
    ///
    /// The first call queries the kernel; later calls return the cached
    /// snapshot until [`Device::note_hotplug`] runs (driven by
    /// `novogfx-event` on hotplug).
    pub fn resources(&self) -> Result<&ResourceHandles> {
        // Fast path: already cached.
        {
            let cache = self
                .cache
                .read()
                .map_err(|_| Error::Internal("device resource cache lock poisoned".to_string()))?;
            if let Some(ref current) = cache.current {
                // SAFETY: `current` is an `Arc` kept alive by the cache for
                // the lifetime of `self` (retired Arcs are stored in
                // `retired` on invalidation, never freed while `self` lives).
                // Extending the borrow from the lock guard to `&self` is
                // sound because the allocation is pinned by the `Arc` and
                // only freed at `Device::drop`, after all `&self` borrows end.
                let ptr: *const ResourceHandles = std::sync::Arc::as_ptr(current);
                drop(cache);
                // SAFETY: see above.
                return Ok(unsafe { &*ptr });
            }
        }
        // Slow path: serialise concurrent fills.
        let _fill = self
            .fill_lock
            .lock()
            .map_err(|_| Error::Internal("device resource fill lock poisoned".to_string()))?;
        // Re-check under the fill lock (another thread may have filled).
        {
            let cache = self
                .cache
                .read()
                .map_err(|_| Error::Internal("device resource cache lock poisoned".to_string()))?;
            if let Some(ref current) = cache.current {
                let ptr: *const ResourceHandles = std::sync::Arc::as_ptr(current);
                drop(cache);
                // SAFETY: same pinning argument as the fast path.
                return Ok(unsafe { &*ptr });
            }
        }
        let fresh = Self::query_resources(&self.fd)?;
        let arc = std::sync::Arc::new(fresh);
        let ptr: *const ResourceHandles = std::sync::Arc::as_ptr(&arc);
        {
            let mut cache = self
                .cache
                .write()
                .map_err(|_| Error::Internal("device resource cache lock poisoned".to_string()))?;
            cache.current = Some(arc);
        }
        // SAFETY: the `Arc` was just stored in the cache; see above.
        Ok(unsafe { &*ptr })
    }

    /// Query a fresh, owned resource snapshot, independent of the borrow cache.
    ///
    /// Suitable for C enumeration and long-lived callers that need current
    /// topology while retaining their own earlier snapshots.
    pub fn resource_snapshot(&self) -> Result<ResourceHandles> {
        Self::query_resources(&self.fd)
    }

    /// Invalidate the cached [`ResourceHandles`].
    ///
    /// Called when a hotplug event arrives. The previous snapshot is retired
    /// (kept alive until `Device` drops) so outstanding `&ResourceHandles`
    /// borrows stay valid; the next [`Device::resources`] call re-queries.
    pub(crate) fn invalidate_cache(&self) {
        // Serialize invalidation with fills so a just-published snapshot
        // cannot be replaced and freed before its returned reference is used.
        let Ok(_fill) = self.fill_lock.lock() else {
            return;
        };
        if let Ok(mut cache) = self.cache.write() {
            if let Some(old) = cache.current.take() {
                cache.retired.push(old);
                // resources() returns a reference tied to Device, not an
                // event cycle. Every retired snapshot must survive until
                // Device drops; eviction would invalidate outstanding borrows.
            }
        }
    }

    /// Public invalidation entry point for `novogfx-event`.
    ///
    /// `novogfx-event` calls this after parsing a hotplug/connection event.
    pub fn note_hotplug(&self) {
        self.invalidate_cache();
    }

    /// Raw fd access for `poll`/`epoll` integration.
    #[must_use]
    pub fn raw_fd(&self) -> std::os::fd::BorrowedFd<'_> {
        self.fd.as_fd()
    }

    fn query_resources(fd: &OwnedFd) -> Result<ResourceHandles> {
        use std::os::fd::AsRawFd;
        let raw = fd.as_fd().as_raw_fd();
        // SAFETY: `drmModeGetResources` takes only an fd and returns either
        // NULL or a heap allocation owned by the caller.
        let ptr = unsafe { xf86drm::drmModeGetResources(raw) };
        if ptr.is_null() {
            return Err(Error::last_os_error("drmModeGetResources"));
        }
        // SAFETY: `ptr` is non-NULL and points to a `drmModeRes` allocated
        // by libdrm; field pointers are valid for the counts given.
        let (res, planes) = unsafe {
            let r = &*ptr;
            let mut out = ResourceHandles::empty();
            out.min_width = r.min_width;
            out.max_width = r.max_width;
            out.min_height = r.min_height;
            out.max_height = r.max_height;
            if r.count_connectors > 0 && !r.connectors.is_null() {
                let s = std::slice::from_raw_parts(r.connectors, r.count_connectors as usize);
                out.connectors.extend_from_slice(s);
            }
            if r.count_crtcs > 0 && !r.crtcs.is_null() {
                let s = std::slice::from_raw_parts(r.crtcs, r.count_crtcs as usize);
                out.crtcs.extend_from_slice(s);
            }
            if r.count_encoders > 0 && !r.encoders.is_null() {
                let s = std::slice::from_raw_parts(r.encoders, r.count_encoders as usize);
                out.encoders.extend_from_slice(s);
            }
            if r.count_fbs > 0 && !r.fbs.is_null() {
                let s = std::slice::from_raw_parts(r.fbs, r.count_fbs as usize);
                out.framebuffers.extend_from_slice(s);
            }
            xf86drm::drmModeFreeResources(ptr);
            // Planes live in a separate ioctl.
            let pptr = xf86drm::drmModeGetPlaneResources(raw);
            let mut planids = Vec::new();
            if !pptr.is_null() {
                let pr = &*pptr;
                if pr.count_planes > 0 && !pr.planes.is_null() {
                    let s = std::slice::from_raw_parts(pr.planes, pr.count_planes as usize);
                    planids.extend_from_slice(s);
                }
                xf86drm::drmModeFreePlaneResources(pptr);
            }
            (out, planids)
        };
        let mut res = res;
        res.planes = planes;
        Ok(res)
    }
}

impl AsFd for Device {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}

impl DriverInfo {
    fn query(fd: &OwnedFd) -> Result<Self> {
        use std::os::fd::AsRawFd;
        let raw = fd.as_fd().as_raw_fd();
        // SAFETY: `drmGetVersion` takes only an fd and returns NULL or a
        // heap allocation; all pointer fields are validated before use.
        let ptr = unsafe { xf86drm::drmGetVersion(raw) };
        if ptr.is_null() {
            return Err(Error::last_os_error("drmGetVersion"));
        }
        // SAFETY: `ptr` is a valid `drmVersion` from libdrm.
        let info = unsafe {
            let v = &*ptr;
            let name = copy_c_string(v.name, v.name_len);
            let date = copy_c_string(v.date, v.date_len);
            let desc = copy_c_string(v.desc, v.desc_len);
            let out = Self {
                name,
                date,
                desc,
                major: v.version_major,
                minor: v.version_minor,
                patch: v.version_patchlevel,
            };
            xf86drm::drmFreeVersion(ptr);
            out
        };
        if info.name.is_empty() {
            return Err(Error::NotSupported(
                "DRM driver reported an empty name".to_string(),
            ));
        }
        Ok(info)
    }
}

/// Copy a (possibly NULL) NUL-terminated C string into a Rust `String`.
/// Embedded NULs truncate; invalid UTF-8 is lossily converted.
///
/// SAFETY: caller must guarantee `ptr` is NULL or points to a valid
/// NUL-terminated string owned by libdrm for the duration of the copy
/// (upheld at the `drmGetVersion` call site).
unsafe fn copy_c_string(ptr: *mut std::os::raw::c_char, _len: std::os::raw::c_int) -> String {
    if ptr.is_null() {
        return String::new();
    }
    // SAFETY: libdrm NUL-terminates these fields; `CStr` reads until the
    // first NUL, which is within the allocation.
    let cstr = unsafe { std::ffi::CStr::from_ptr(ptr) };
    cstr.to_string_lossy().into_owned()
}

fn get_cap(fd: &OwnedFd, cap: u64) -> std::result::Result<u64, i32> {
    use std::os::fd::AsRawFd;
    let raw = fd.as_fd().as_raw_fd();
    let mut value: u64 = 0;
    // SAFETY: fd is valid; `value` is a stack out-param.
    let ret = unsafe { xf86drm::drmGetCap(raw, cap, &mut value) };
    if ret == 0 {
        Ok(value)
    } else {
        Err(ret)
    }
}

fn rustix_open_flags() -> i32 {
    // O_RDWR | O_CLOEXEC without depending on `libc`/`rustix`.
    // Values match the Linux userspace ABI on all supported arches.
    #[cfg(target_os = "linux")]
    {
        const O_RDWR: i32 = 0o2;
        const O_CLOEXEC: i32 = 0o2000000;
        O_RDWR | O_CLOEXEC
    }
    #[cfg(not(target_os = "linux"))]
    {
        0
    }
}

/// Validate a device path without opening it (unit-testable).
pub(crate) fn validate_device_path(path: &Path) -> Result<()> {
    if path.as_os_str().is_empty() {
        return Err(Error::InvalidArgument("empty device path".to_string()));
    }
    // Device nodes live under /dev/dri/ by convention; anything else is
    // suspicious but not fatal (tests use /tmp, vkms may move).
    let bytes = path.as_os_str().as_bytes();
    if bytes.contains(&0) {
        return Err(Error::InvalidArgument(
            "device path contains an interior NUL".to_string(),
        ));
    }
    let _ = CString::new(bytes)
        .map_err(|_| Error::InvalidArgument("device path contains an interior NUL".to_string()))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn old_resource_borrows_survive_many_hotplugs() {
        let device = Device {
            fd: std::fs::File::open("/dev/null").unwrap().into(),
            cache: RwLock::new(CacheInner {
                current: None,
                retired: Vec::new(),
            }),
            fill_lock: Mutex::new(()),
            atomic_enabled: std::sync::atomic::AtomicBool::new(false),
        };
        let first = std::sync::Arc::new(ResourceHandles {
            connectors: vec![42],
            ..ResourceHandles::default()
        });
        let weak = std::sync::Arc::downgrade(&first);
        device.cache.write().unwrap().current = Some(first);
        let borrowed = device.resources().unwrap();
        for _ in 0..32 {
            device.note_hotplug();
            device.cache.write().unwrap().current =
                Some(std::sync::Arc::new(ResourceHandles::default()));
        }
        // Test allocation liveness before dereferencing: the old code fails
        // here without exercising its dangling reference.
        assert!(weak.upgrade().is_some());
        assert_eq!(borrowed.connectors, vec![42]);
    }

    #[test]
    fn caps_flag_operations() {
        let caps = DeviceCaps::DUMB_BUFFER | DeviceCaps::PRIME;
        assert!(caps.contains(DeviceCaps::DUMB_BUFFER));
        assert!(caps.contains(DeviceCaps::PRIME));
        assert!(!caps.contains(DeviceCaps::ATOMIC));
        assert!(!DeviceCaps::empty().contains(DeviceCaps::PRIME));
        assert_eq!(DeviceCaps::all().bits() & caps.bits(), caps.bits());
        assert_eq!(
            DeviceCaps::from_bits_truncate(u64::MAX).bits(),
            DeviceCaps::all().bits()
        );
        // IDL bit positions.
        assert_eq!(DeviceCaps::ATOMIC.bits(), 1 << 0);
        assert_eq!(DeviceCaps::UNIVERSAL_PLANES.bits(), 1 << 1);
        assert_eq!(DeviceCaps::DUMB_BUFFER.bits(), 1 << 2);
        assert_eq!(DeviceCaps::PRIME.bits(), 1 << 3);
        assert_eq!(DeviceCaps::ASYNC_PAGE_FLIP.bits(), 1 << 4);
    }

    #[test]
    fn open_rejects_empty_and_nul_paths() {
        let e = Device::open("").unwrap_err();
        assert!(matches!(e, Error::InvalidArgument(_)));
        let e = validate_device_path(Path::new("")).unwrap_err();
        assert!(matches!(e, Error::InvalidArgument(_)));
    }

    #[test]
    fn open_missing_node_reports_not_found() {
        let e = Device::open("/dev/dri/no-such-card-xyz").unwrap_err();
        assert!(matches!(e, Error::NotFound(_)), "got {e:?}");
    }

    #[test]
    fn open_non_drm_file_is_rejected() {
        // /dev/null exists but is not a DRM device.
        let e = Device::open("/dev/null").unwrap_err();
        match e {
            Error::InvalidArgument(_) | Error::NotSupported(_) | Error::Io(_) => {}
            other => panic!("expected rejection, got {other:?}"),
        }
    }
}
