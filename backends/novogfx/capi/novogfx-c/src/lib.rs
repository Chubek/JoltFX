//! C ABI wrapper for novogfx, following `NovoGFX-API.idl`.
//!
//! All handles are opaque pointers. Every constructor has a matching
//! destructor. A single device must be used from one thread at a time.

#![forbid(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]
#![allow(non_camel_case_types)]
//! # Pointer safety
//! Non-null arguments must be live API handles or valid buffers of the
//! documented length. Devices require exclusive, non-reentrant access.
//! Borrowed handles must not be destroyed separately from their owners.

use std::any::Any;
use std::ffi::{CStr, CString};
use std::os::fd::{AsFd, AsRawFd};
use std::os::raw::{c_char, c_int, c_ulong, c_void};
use std::ptr;
use std::rc::Rc;

mod handles;
use handles::HandleCache;

use novogfx::Mode;

// ---------------------------------------------------------------------------
// Status codes (must match the IDL)
// ---------------------------------------------------------------------------

/// Success.
pub const NOVOGFX_OK: u32 = 0;
/// Invalid argument.
pub const NOVOGFX_ERR_INVALID_ARGUMENT: u32 = 1;
/// I/O error.
pub const NOVOGFX_ERR_IO: u32 = 2;
/// Not found.
pub const NOVOGFX_ERR_NOT_FOUND: u32 = 3;
/// Not supported.
pub const NOVOGFX_ERR_NOT_SUPPORTED: u32 = 4;
/// Permission denied.
pub const NOVOGFX_ERR_PERMISSION_DENIED: u32 = 5;
/// Busy.
pub const NOVOGFX_ERR_BUSY: u32 = 6;
/// Try again (no events pending).
pub const NOVOGFX_ERR_AGAIN: u32 = 7;
/// Out of memory.
pub const NOVOGFX_ERR_OUT_OF_MEMORY: u32 = 8;
/// Device lost.
pub const NOVOGFX_ERR_DEVICE_LOST: u32 = 9;
/// Cancelled.
pub const NOVOGFX_ERR_CANCELLED: u32 = 10;
/// Internal error.
pub const NOVOGFX_ERR_INTERNAL: u32 = 11;

fn status_of(r: Result<(), novogfx::Error>) -> u32 {
    match r {
        Ok(()) => NOVOGFX_OK,
        Err(e) => e.status_code(),
    }
}

fn checked_u32(value: c_ulong) -> Result<u32, u32> {
    u32::try_from(value).map_err(|_| NOVOGFX_ERR_INVALID_ARGUMENT)
}

fn translate_commit_flags(flags: c_ulong) -> Result<novogfx::CommitFlags, u32> {
    use novogfx::CommitFlags as F;
    if flags & !0xf != 0 || (flags & 8 != 0 && flags & 6 != 0) {
        return Err(NOVOGFX_ERR_INVALID_ARGUMENT);
    }
    let mut out = F::empty();
    for (bit, flag) in [
        (1, F::ALLOW_MODESET),
        (2, F::NONBLOCK),
        (4, F::PAGE_FLIP_EVENT),
        (8, F::TEST_ONLY),
    ] {
        if flags & bit != 0 {
            out = out | flag;
        }
    }
    Ok(out)
}

// ---------------------------------------------------------------------------
// Opaque handle bodies
// ---------------------------------------------------------------------------

/// Opened DRM device.
#[repr(C)]
pub struct novogfx_device_t {
    inner: novogfx::Device,
    handles: DeviceHandles,
}

#[derive(Default)]
struct DeviceHandles {
    connectors: HandleCache<novogfx_connector_t>,
    crtcs: HandleCache<novogfx_crtc_t>,
    encoders: HandleCache<novogfx_encoder_t>,
    planes: HandleCache<novogfx_plane_t>,
    framebuffers: HandleCache<novogfx_framebuffer_t>,
}

/// KMS connector handle (device-non-owning + snapshot).
pub struct novogfx_connector_t {
    device: *mut novogfx_device_t,
    id: u32,
    status: u32,
    conn_type: u32,
    modes: Vec<ModeC>,
    edid: Vec<u8>,
}

/// KMS CRTC handle.
pub struct novogfx_crtc_t {
    device: *mut novogfx_device_t,
    id: u32,
    mode: Option<ModeC>,
}

/// KMS encoder handle.
#[allow(dead_code)]
pub struct novogfx_encoder_t {
    device: *mut novogfx_device_t,
    id: u32,
}

/// KMS plane handle.
pub struct novogfx_plane_t {
    device: *mut novogfx_device_t,
    id: u32,
}

/// Framebuffer handle.
pub struct novogfx_framebuffer_t {
    id: u32,
    device: *mut novogfx_device_t,
}

/// Dumb-buffer handle (owns the Rust buffer + associated fb handle).
pub struct novogfx_dumb_buffer_t {
    buffer: Option<novogfx::DumbBuffer>,
    fb_handle: *mut novogfx_framebuffer_t,
    width: u32,
    height: u32,
    pitch: u32,
    size: u64,
    handle: u32,
}

/// Pending atomic commit (queued Rust-level ops; committed via libdrm).
pub struct novogfx_atomic_request_t {
    device: *mut novogfx_device_t,
    crtcs: Vec<AtomicCrtc>,
    planes: Vec<novogfx::PlaneUpdate>,
    connectors: Vec<(u32, u32, bool)>,
}

/// Event reader (raw-fd based, independent of Rust borrow).
pub struct novogfx_event_reader_t {
    fd: std::os::fd::OwnedFd,
    pending: Vec<u8>,
    events: Vec<novogfx_event_c>,
    sequence: u64,
}

/// Property blob handle.
#[allow(dead_code)]
pub struct novogfx_blob_t {
    id: u32,
}

/// Property handle.
#[allow(dead_code)]
pub struct novogfx_property_t {
    id: u32,
}

struct AtomicCrtc {
    crtc: u32,
    fb: u32,
    has_fb: bool,
    mode: Option<Mode>,
    connector: u32,
    has_connector: bool,
}

// ---------------------------------------------------------------------------
// C-visible structs (must match novogfx.h)
// ---------------------------------------------------------------------------

/// C-visible mode snapshot.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct novogfx_mode_t {
    /// Pixel clock in kHz.
    pub clock_khz: c_ulong,
    /// Visible width.
    pub hdisplay: u16,
    /// H sync start.
    pub hsync_start: u16,
    /// H sync end.
    pub hsync_end: u16,
    /// H total.
    pub htotal: u16,
    /// H skew.
    pub hskew: u16,
    /// Visible height.
    pub vdisplay: u16,
    /// V sync start.
    pub vsync_start: u16,
    /// V sync end.
    pub vsync_end: u16,
    /// V total.
    pub vtotal: u16,
    /// V scan.
    pub vscan: u16,
    /// Flags.
    pub flags: c_ulong,
    /// Type.
    pub type_: c_ulong,
    /// Borrowed NUL-terminated name, owned by the mode snapshot.
    pub name: *const c_char,
}

#[repr(C)]
#[derive(Debug)]
struct ModeC {
    inner: novogfx_mode_t,
    _owned_name: CString,
}

/// C-visible device info.
#[repr(C)]
pub struct novogfx_device_info_t {
    /// Driver name (owned, free with `novogfx_string_free`).
    pub driver_name: *mut c_char,
    /// Driver date (owned).
    pub driver_date: *mut c_char,
    /// Driver description (owned).
    pub driver_desc: *mut c_char,
    /// Major version.
    pub driver_major: c_ulong,
    /// Minor version.
    pub driver_minor: c_ulong,
    /// Patch version.
    pub driver_patch: c_ulong,
    /// Bus type (0 = PCI, 1 = USB, 2 = platform).
    pub bus_type: c_ulong,
    /// Device id.
    pub device_id: c_ulong,
    /// Vendor id.
    pub vendor_id: c_ulong,
}

/// C-visible resource lists.
#[repr(C)]
pub struct novogfx_resources_t {
    /// Number of connectors.
    pub num_connectors: c_ulong,
    /// Number of CRTCs.
    pub num_crtcs: c_ulong,
    /// Number of encoders.
    pub num_encoders: c_ulong,
    /// Number of planes.
    pub num_planes: c_ulong,
    /// Number of framebuffers.
    pub num_framebuffers: c_ulong,
    /// Connector handles.
    pub connectors: *mut *mut novogfx_connector_t,
    /// CRTC handles.
    pub crtcs: *mut *mut novogfx_crtc_t,
    /// Encoder handles.
    pub encoders: *mut *mut novogfx_encoder_t,
    /// Plane handles.
    pub planes: *mut *mut novogfx_plane_t,
    /// Framebuffer handles.
    pub framebuffers: *mut *mut novogfx_framebuffer_t,
}

/// Owned resource allocation ( backing `novogfx_resources_t`).
#[allow(non_camel_case_types)]
#[repr(C)]
pub struct novogfx_resources_owned {
    view: novogfx_resources_t,
    connectors: Vec<*mut novogfx_connector_t>,
    crtcs: Vec<*mut novogfx_crtc_t>,
    encoders: Vec<*mut novogfx_encoder_t>,
    planes: Vec<*mut novogfx_plane_t>,
    framebuffers: Vec<*mut novogfx_framebuffer_t>,
    owners: Vec<Rc<dyn Any>>,
}

/// C-visible dumb-buffer descriptor.
#[repr(C)]
pub struct novogfx_dumb_buffer_desc_t {
    /// Width.
    pub width: c_ulong,
    /// Height.
    pub height: c_ulong,
    /// Bits per pixel.
    pub bpp: c_ulong,
    /// Flags (must be 0).
    pub flags: c_ulong,
}

/// C-visible dumb-buffer info.
#[repr(C)]
pub struct novogfx_dumb_buffer_info_t {
    /// Width.
    pub width: c_ulong,
    /// Height.
    pub height: c_ulong,
    /// Pitch.
    pub pitch: c_ulong,
    /// Size.
    pub size: c_ulong,
    /// GEM handle.
    pub handle: c_ulong,
}

/// C-visible plane update.
#[repr(C)]
pub struct novogfx_plane_update_t {
    /// Plane handle.
    pub plane: *mut novogfx_plane_t,
    /// Framebuffer handle (NULL = disable).
    pub fb: *mut novogfx_framebuffer_t,
    /// CRTC handle (NULL = disable).
    pub crtc: *mut novogfx_crtc_t,
    /// Source X (16.16).
    pub src_x: c_ulong,
    /// Source Y.
    pub src_y: c_ulong,
    /// Source W.
    pub src_w: c_ulong,
    /// Source H.
    pub src_h: c_ulong,
    /// Dest X (signed).
    pub crtc_x: std::os::raw::c_long,
    /// Dest Y.
    pub crtc_y: std::os::raw::c_long,
    /// Dest W.
    pub crtc_w: c_ulong,
    /// Dest H.
    pub crtc_h: c_ulong,
    /// Z-order.
    pub zpos: c_ulong,
    /// Flags (reserved).
    pub flags: c_ulong,
}

/// C-visible event.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct novogfx_event_c {
    /// Type: 0 = vblank, 1 = page flip, 2 = hotplug.
    pub type_: u32,
    /// Sequence.
    pub sequence: c_ulong,
    /// Seconds.
    pub tv_sec: c_ulong,
    /// Microseconds.
    pub tv_usec: c_ulong,
    /// CRTC id (vblank / page flip).
    pub crtc_id: c_ulong,
    /// User data.
    pub user_data: c_ulong,
    /// Flip status (page flip only).
    pub flip_status: c_int,
    /// Connector id (hotplug only).
    pub connector_id: c_ulong,
}

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------

/// Returns the ABI version as `MAJOR*10000 + MINOR*100 + PATCH`.
#[no_mangle]
pub extern "C" fn novogfx_abi_version() -> c_ulong {
    novogfx::abi_version() as c_ulong
}

/// Human-readable library version string (static, never freed).
#[no_mangle]
pub extern "C" fn novogfx_version_string() -> *const c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0")
        .as_ptr()
        .cast::<c_char>()
}

/// Human-readable status string (static, never freed).
#[no_mangle]
pub extern "C" fn novogfx_status_string(status: u32) -> *const c_char {
    match status {
        NOVOGFX_OK => c"OK".as_ptr(),
        NOVOGFX_ERR_INVALID_ARGUMENT => c"INVALID_ARGUMENT".as_ptr(),
        NOVOGFX_ERR_IO => c"IO".as_ptr(),
        NOVOGFX_ERR_NOT_FOUND => c"NOT_FOUND".as_ptr(),
        NOVOGFX_ERR_NOT_SUPPORTED => c"NOT_SUPPORTED".as_ptr(),
        NOVOGFX_ERR_PERMISSION_DENIED => c"PERMISSION_DENIED".as_ptr(),
        NOVOGFX_ERR_BUSY => c"BUSY".as_ptr(),
        NOVOGFX_ERR_AGAIN => c"AGAIN".as_ptr(),
        NOVOGFX_ERR_OUT_OF_MEMORY => c"OUT_OF_MEMORY".as_ptr(),
        NOVOGFX_ERR_DEVICE_LOST => c"DEVICE_LOST".as_ptr(),
        NOVOGFX_ERR_CANCELLED => c"CANCELLED".as_ptr(),
        _ => c"INTERNAL".as_ptr(),
    }
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

/// Open a DRM device. On success writes a handle to `out_device`.
#[no_mangle]
/// # Safety
/// Path must be NUL-terminated and out_device writable.
pub unsafe extern "C" fn novogfx_device_open(
    path: *const c_char,
    out_device: *mut *mut novogfx_device_t,
) -> u32 {
    if path.is_null() || out_device.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: the output is writable even when opening fails.
    unsafe {
        *out_device = ptr::null_mut();
    }
    // SAFETY: `path` is a non-NULL NUL-terminated string per the contract.
    let path_str = unsafe { CStr::from_ptr(path) };
    let path_os = match path_str.to_str() {
        Ok(s) => s,
        Err(_) => return NOVOGFX_ERR_INVALID_ARGUMENT,
    };
    match novogfx::Device::open(path_os) {
        Ok(dev) => {
            let boxed = Box::new(novogfx_device_t {
                inner: dev,
                handles: DeviceHandles::default(),
            });
            // SAFETY: `out_device` is a valid out-pointer per the contract.
            unsafe { *out_device = Box::into_raw(boxed) };
            NOVOGFX_OK
        }
        Err(e) => e.status_code(),
    }
}

/// Query device info (strings owned by the caller).
#[no_mangle]
/// # Safety
/// Device must be live and output writable; see module pointer contract.
pub unsafe extern "C" fn novogfx_device_get_info(
    device: *const novogfx_device_t,
    out_info: *mut novogfx_device_info_t,
) -> u32 {
    if device.is_null() || out_info.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: handles validated non-NULL; `device` outlives the call.
    let dev = unsafe { &(*device).inner };
    match dev.driver() {
        Ok(info) => {
            let name = CString::new(info.name).unwrap_or_default();
            let date = CString::new(info.date).unwrap_or_default();
            let desc = CString::new(info.desc).unwrap_or_default();
            // SAFETY: `out_info` is a valid out-pointer.
            unsafe {
                (*out_info).driver_name = name.into_raw();
                (*out_info).driver_date = date.into_raw();
                (*out_info).driver_desc = desc.into_raw();
                (*out_info).driver_major = info.major as c_ulong;
                (*out_info).driver_minor = info.minor as c_ulong;
                (*out_info).driver_patch = info.patch as c_ulong;
                (*out_info).bus_type = 0;
                (*out_info).device_id = 0;
                (*out_info).vendor_id = 0;
            }
            NOVOGFX_OK
        }
        Err(e) => e.status_code(),
    }
}

/// Capability bitmask (never fails).
#[no_mangle]
/// # Safety
/// Device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_device_caps(device: *const novogfx_device_t) -> c_ulong {
    if device.is_null() {
        return 0;
    }
    // SAFETY: non-NULL per the null check above.
    let dev = unsafe { &(*device).inner };
    dev.capabilities().bits() as c_ulong
}

/// Enable atomic modesetting (idempotent).
#[no_mangle]
/// # Safety
/// Device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_device_enable_atomic(device: *const novogfx_device_t) -> u32 {
    if device.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: non-NULL; single-threaded use per the IDL.
    let dev = unsafe { &(*device).inner };
    status_of(dev.enable_atomic())
}

/// Underlying fd (borrowed; must not be closed).
#[no_mangle]
/// # Safety
/// Device must be live; returned descriptor is borrowed.
pub unsafe extern "C" fn novogfx_device_fd(device: *const novogfx_device_t) -> c_int {
    if device.is_null() {
        return -1;
    }
    // SAFETY: non-NULL.
    let dev = unsafe { &(*device).inner };
    dev.as_fd().as_raw_fd()
}

/// Destroy a device; outstanding handles become invalid.
#[no_mangle]
/// # Safety
/// Device must be exclusively owned, destroyed once, with no live dependents.
pub unsafe extern "C" fn novogfx_device_destroy(device: *mut novogfx_device_t) {
    if device.is_null() {
        return;
    }
    // SAFETY: `device` came from `novogfx_device_open` and is freed once here.
    unsafe { drop(Box::from_raw(device)) };
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

fn make_connector_handle(
    device: *mut novogfx_device_t,
    id: u32,
) -> Result<novogfx_connector_t, novogfx::Error> {
    // SAFETY: `device` is a live open device; the returned Rust borrows end
    // before we box the snapshot (no borrow escapes).
    let dev = unsafe { &(*device).inner };
    let conn = novogfx::Connector::get(dev, novogfx::ConnectorId(id))?;
    let mut modes = Vec::with_capacity(conn.modes().len());
    for m in conn.modes() {
        modes.push(mode_to_c(m));
    }
    let edid = conn.edid_raw().unwrap_or_default();
    let status = match conn.status() {
        novogfx::ConnectorStatus::Connected => 0,
        novogfx::ConnectorStatus::Disconnected => 1,
        novogfx::ConnectorStatus::Unknown => 2,
    };
    let conn_type = match conn.connector_type() {
        novogfx::ConnectorType::Unknown => 0,
        novogfx::ConnectorType::Vga => 1,
        novogfx::ConnectorType::DviI => 2,
        novogfx::ConnectorType::DviD => 3,
        novogfx::ConnectorType::DviA => 4,
        novogfx::ConnectorType::Composite => 5,
        novogfx::ConnectorType::Svideo => 6,
        novogfx::ConnectorType::Lvds => 7,
        novogfx::ConnectorType::Component => 8,
        novogfx::ConnectorType::Din => 9,
        novogfx::ConnectorType::DisplayPort => 10,
        novogfx::ConnectorType::HdmiA => 11,
        novogfx::ConnectorType::HdmiB => 12,
        novogfx::ConnectorType::Tv => 13,
        novogfx::ConnectorType::EmbeddedDisplayPort => 14,
        novogfx::ConnectorType::Virtual => 15,
        novogfx::ConnectorType::Dsi => 16,
        novogfx::ConnectorType::Dpi => 17,
    };
    Ok(novogfx_connector_t {
        device,
        id,
        status,
        conn_type,
        modes,
        edid,
    })
}

fn make_crtc_handle(device: *mut novogfx_device_t, id: u32) -> novogfx::Result<novogfx_crtc_t> {
    // SAFETY: the caller retains a live device for the query.
    let crtc = novogfx::Crtc::get(unsafe { &(*device).inner }, novogfx::CrtcId(id))?;
    Ok(novogfx_crtc_t {
        device,
        id,
        mode: crtc.current_mode().map(mode_to_c),
    })
}

fn mode_to_c(m: &Mode) -> ModeC {
    let name = CString::new(m.name.as_bytes()).unwrap_or_default();
    ModeC {
        inner: novogfx_mode_t {
            clock_khz: m.clock_khz as c_ulong,
            hdisplay: m.hdisplay,
            hsync_start: m.hsync_start,
            hsync_end: m.hsync_end,
            htotal: m.htotal,
            hskew: m.hskew,
            vdisplay: m.vdisplay,
            vsync_start: m.vsync_start,
            vsync_end: m.vsync_end,
            vtotal: m.vtotal,
            vscan: m.vscan,
            flags: m.flags as c_ulong,
            type_: m.kind as c_ulong,
            name: name.as_ptr(),
        },
        _owned_name: name,
    }
}

/// Enumerate resources, reusing handles retained by overlapping snapshots.
#[no_mangle]
/// # Safety
/// Device must be live and output writable; see module pointer contract.
pub unsafe extern "C" fn novogfx_device_get_resources(
    device: *mut novogfx_device_t,
    out_resources: *mut *mut novogfx_resources_t,
) -> u32 {
    if device.is_null() || out_resources.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: output is writable. Failures never expose partially built lists.
    unsafe {
        *out_resources = ptr::null_mut();
    }
    // SAFETY: `device` is live.
    let dev = unsafe { &(*device).inner };
    let handles = match dev.resource_snapshot() {
        Ok(h) => h,
        Err(e) => return e.status_code(),
    };
    let mut owned = Box::new(novogfx_resources_owned {
        view: novogfx_resources_t {
            num_connectors: 0,
            num_crtcs: 0,
            num_encoders: 0,
            num_planes: 0,
            num_framebuffers: 0,
            connectors: ptr::null_mut(),
            crtcs: ptr::null_mut(),
            encoders: ptr::null_mut(),
            planes: ptr::null_mut(),
            framebuffers: ptr::null_mut(),
        },
        connectors: Vec::new(),
        crtcs: Vec::new(),
        encoders: Vec::new(),
        planes: Vec::new(),
        framebuffers: Vec::new(),
        owners: Vec::new(),
    });
    // SAFETY: exclusive, non-reentrant device access is required by this ABI.
    // Only the handles field is borrowed mutably; query helpers borrow inner.
    let cache = unsafe { &mut (*device).handles };
    for id in &handles.connectors {
        let h = match cache
            .connectors
            .get_or_try_insert(*id, || make_connector_handle(device, *id))
        {
            Ok(h) => h,
            Err(e) => return e.status_code(),
        };
        owned.connectors.push(Rc::as_ptr(&h).cast_mut());
        owned.owners.push(h);
    }
    for id in &handles.crtcs {
        let h = match cache
            .crtcs
            .get_or_try_insert(*id, || make_crtc_handle(device, *id))
        {
            Ok(h) => h,
            Err(e) => return e.status_code(),
        };
        owned.crtcs.push(Rc::as_ptr(&h).cast_mut());
        owned.owners.push(h);
    }
    for id in &handles.encoders {
        let h = match cache
            .encoders
            .get_or_try_insert(*id, || Ok(novogfx_encoder_t { device, id: *id }))
        {
            Ok(h) => h,
            Err(e) => return e.status_code(),
        };
        owned.encoders.push(Rc::as_ptr(&h).cast_mut());
        owned.owners.push(h);
    }
    for id in &handles.planes {
        let h = match cache
            .planes
            .get_or_try_insert(*id, || Ok(novogfx_plane_t { device, id: *id }))
        {
            Ok(h) => h,
            Err(e) => return e.status_code(),
        };
        owned.planes.push(Rc::as_ptr(&h).cast_mut());
        owned.owners.push(h);
    }
    for id in &handles.framebuffers {
        let h = match cache
            .framebuffers
            .get_or_try_insert(*id, || Ok(novogfx_framebuffer_t { id: *id, device }))
        {
            Ok(h) => h,
            Err(e) => return e.status_code(),
        };
        owned.framebuffers.push(Rc::as_ptr(&h).cast_mut());
        owned.owners.push(h);
    }
    owned.view.num_connectors = owned.connectors.len() as c_ulong;
    owned.view.num_crtcs = owned.crtcs.len() as c_ulong;
    owned.view.num_encoders = owned.encoders.len() as c_ulong;
    owned.view.num_planes = owned.planes.len() as c_ulong;
    owned.view.num_framebuffers = owned.framebuffers.len() as c_ulong;
    owned.view.connectors = array_pointer(&mut owned.connectors);
    owned.view.crtcs = array_pointer(&mut owned.crtcs);
    owned.view.encoders = array_pointer(&mut owned.encoders);
    owned.view.planes = array_pointer(&mut owned.planes);
    owned.view.framebuffers = array_pointer(&mut owned.framebuffers);
    let view_ptr: *mut novogfx_resources_t = ptr::from_mut(&mut owned.view);
    std::mem::forget(owned);
    // SAFETY: `out_resources` is a valid out-pointer.
    unsafe { *out_resources = view_ptr };
    NOVOGFX_OK
}

fn array_pointer<T>(array: &mut [T]) -> *mut T {
    if array.is_empty() {
        ptr::null_mut()
    } else {
        array.as_mut_ptr()
    }
}

/// Destroy resources and all handles exposed through them.
#[no_mangle]
/// # Safety
/// Resources must be exclusively owned and destroyed exactly once.
pub unsafe extern "C" fn novogfx_resources_destroy(resources: *mut novogfx_resources_t) {
    if resources.is_null() {
        return;
    }
    // SAFETY: `resources` is the view pointer inside a leaked
    // `novogfx_resources_owned`. Reconstruct the owner via field offset.
    unsafe {
        // repr(C) guarantees view is the first field (offset zero).
        let owned_ptr = resources.cast::<novogfx_resources_owned>();
        drop(Box::from_raw(owned_ptr));
    }
}

// ---------------------------------------------------------------------------
// Connectors
// ---------------------------------------------------------------------------

/// Connector status (0/1/2).
#[no_mangle]
/// # Safety
/// Connector and its device must be live.
pub unsafe extern "C" fn novogfx_connector_status(
    connector: *const novogfx_connector_t,
    out_status: *mut u32,
) -> u32 {
    if connector.is_null() || out_status.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: connector and device are live for this query.
    let connector = unsafe { &*connector };
    // SAFETY: device remains live while its resource handles are accessed.
    let current = match novogfx::Connector::get(
        unsafe { &(*connector.device).inner },
        novogfx::ConnectorId(connector.id),
    ) {
        Ok(connector) => connector,
        Err(error) => return error.status_code(),
    };
    let status = match current.status() {
        novogfx::ConnectorStatus::Connected => 0,
        novogfx::ConnectorStatus::Disconnected => 1,
        novogfx::ConnectorStatus::Unknown => 2,
    };
    // SAFETY: caller supplied writable status storage.
    unsafe { *out_status = status };
    NOVOGFX_OK
}

/// Connector type id.
#[no_mangle]
/// # Safety
/// Connector and its device must be live.
pub unsafe extern "C" fn novogfx_connector_type(connector: *const novogfx_connector_t) -> u32 {
    if connector.is_null() {
        return 0;
    }
    // SAFETY: non-NULL.
    unsafe { (*connector).conn_type }
}

/// Number of modes (never fails).
#[no_mangle]
/// # Safety
/// Connector and its device must be live.
pub unsafe extern "C" fn novogfx_connector_num_modes(
    connector: *const novogfx_connector_t,
) -> c_ulong {
    if connector.is_null() {
        return 0;
    }
    // SAFETY: non-NULL.
    unsafe { (*connector).modes.len() as c_ulong }
}

/// Mode at `index` (NULL when out of range; owned by the connector).
#[no_mangle]
/// # Safety
/// Connector must be live; returned mode borrows its snapshot.
pub unsafe extern "C" fn novogfx_connector_mode(
    connector: *const novogfx_connector_t,
    index: c_ulong,
) -> *const novogfx_mode_t {
    if connector.is_null() {
        return ptr::null();
    }
    // SAFETY: non-NULL.
    let c = unsafe { &*connector };
    match c.modes.get(index as usize) {
        Some(m) => ptr::from_ref(&m.inner),
        None => ptr::null(),
    }
}

/// Preferred mode (NULL when none).
#[no_mangle]
/// # Safety
/// Connector must be live; returned mode borrows its snapshot.
pub unsafe extern "C" fn novogfx_connector_preferred_mode(
    connector: *const novogfx_connector_t,
) -> *const novogfx_mode_t {
    if connector.is_null() {
        return ptr::null();
    }
    // SAFETY: non-NULL.
    let c = unsafe { &*connector };
    // Preferred = type bit 1<<3.
    for m in &c.modes {
        if (m.inner.type_ as u32) & (1 << 3) != 0 {
            return ptr::from_ref(&m.inner);
        }
    }
    ptr::null()
}

/// Current CRTC (NULL when unbound). Caller owns the returned handle and must
/// free it with `novogfx_crtc_destroy` (provided below for symmetry).
#[no_mangle]
/// # Safety
/// Connector and device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_connector_current_crtc(
    connector: *const novogfx_connector_t,
) -> *mut novogfx_crtc_t {
    if connector.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: non-NULL.
    let c = unsafe { &*connector };
    // Best-effort: the encoder→CRTC hop requires a live query; when it fails
    // we return NULL rather than a stale handle.
    // SAFETY: device pointer stored at handle creation is live while the
    // device outlives its resources (IDL contract).
    let dev = unsafe { &(*c.device).inner };
    let encoder = match novogfx::Connector::get(dev, novogfx::ConnectorId(c.id))
        .ok()
        .and_then(|c| c.current_encoder())
    {
        Some(id) => id,
        None => return ptr::null_mut(),
    };
    let enc = novogfx::Encoder::get(dev, novogfx::EncoderId(encoder));
    match enc.and_then(|e| {
        e.current_crtc()
            .map(|id| make_crtc_handle(c.device, id).map(|handle| Box::into_raw(Box::new(handle))))
            .transpose()
    }) {
        Ok(h) => h.unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

/// Raw EDID bytes (NULL when unavailable; owned by the connector).
#[no_mangle]
/// # Safety
/// Connector must be live and output writable; returned bytes are borrowed.
pub unsafe extern "C" fn novogfx_connector_edid(
    connector: *const novogfx_connector_t,
    out_len: *mut c_ulong,
) -> *const u8 {
    if connector.is_null() || out_len.is_null() {
        return ptr::null();
    }
    // SAFETY: non-NULL.
    let c = unsafe { &*connector };
    // SAFETY: out-pointer valid.
    unsafe { *out_len = c.edid.len() as c_ulong };
    if c.edid.is_empty() {
        return ptr::null();
    }
    c.edid.as_ptr()
}

/// Connector id (stable for the device lifetime).
#[no_mangle]
/// # Safety
/// Connector must be live.
pub unsafe extern "C" fn novogfx_connector_id(connector: *const novogfx_connector_t) -> c_ulong {
    if connector.is_null() {
        return 0;
    }
    // SAFETY: non-NULL.
    unsafe { (*connector).id as c_ulong }
}

/// First connected connector (NULL when none). The caller owns the handle
/// and releases it with `novogfx_connector_destroy`.
#[no_mangle]
/// # Safety
/// Device must be live and output writable.
pub unsafe extern "C" fn novogfx_device_find_connected_connector(
    device: *mut novogfx_device_t,
) -> *mut novogfx_connector_t {
    if device.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: device live.
    let dev = unsafe { &(*device).inner };
    let handles = match dev.resource_snapshot() {
        Ok(h) => h.connectors,
        Err(_) => return ptr::null_mut(),
    };
    for id in handles {
        if let Ok(h) = make_connector_handle(device, id) {
            if h.status == 0 {
                return Box::into_raw(Box::new(h));
            }
        }
    }
    ptr::null_mut()
}

/// Free a connector handle obtained from `find_connected` / `current_crtc`.
#[no_mangle]
/// # Safety
/// Connector must be independently owned, not borrowed, and destroyed once.
pub unsafe extern "C" fn novogfx_connector_destroy(connector: *mut novogfx_connector_t) {
    if connector.is_null() {
        return;
    }
    // SAFETY: handle came from this ABI and is freed once here.
    unsafe { drop(Box::from_raw(connector)) };
}

// ---------------------------------------------------------------------------
// CRTCs / planes / formats
// ---------------------------------------------------------------------------

/// CRTC id.
#[no_mangle]
/// # Safety
/// CRTC must be live.
pub unsafe extern "C" fn novogfx_crtc_id(crtc: *const novogfx_crtc_t) -> c_ulong {
    if crtc.is_null() {
        return 0;
    }
    // SAFETY: non-NULL.
    unsafe { (*crtc).id as c_ulong }
}

/// Current framebuffer (NULL when unbound; owned by the caller).
#[no_mangle]
/// # Safety
/// CRTC must be live.
pub unsafe extern "C" fn novogfx_crtc_current_framebuffer(
    crtc: *const novogfx_crtc_t,
) -> *mut novogfx_framebuffer_t {
    if crtc.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: non-NULL.
    let c = unsafe { &*crtc };
    // SAFETY: the owning device remains live for queries through this handle.
    let dev = unsafe { &(*c.device).inner };
    let fb_id = match novogfx::Crtc::get(dev, novogfx::CrtcId(c.id))
        .ok()
        .and_then(|c| c.current_framebuffer())
    {
        Some(id) => id,
        None => return ptr::null_mut(),
    };
    Box::into_raw(Box::new(novogfx_framebuffer_t {
        id: fb_id,
        device: c.device,
    }))
}

/// Current mode (NULL when disabled; owned by the CRTC handle snapshot).
#[no_mangle]
/// # Safety
/// CRTC must be live; returned mode borrows its snapshot.
pub unsafe extern "C" fn novogfx_crtc_current_mode(
    crtc: *const novogfx_crtc_t,
) -> *const novogfx_mode_t {
    if crtc.is_null() {
        return ptr::null();
    }
    // SAFETY: crtc is a live API handle supplied by the C caller.
    let c = unsafe { &*crtc };
    c.mode
        .as_ref()
        .map_or(ptr::null(), |m| ptr::from_ref(&m.inner))
}

/// Free a CRTC handle.
#[no_mangle]
/// # Safety
/// CRTC must be independently owned, not borrowed, and destroyed once.
pub unsafe extern "C" fn novogfx_crtc_destroy(crtc: *mut novogfx_crtc_t) {
    if crtc.is_null() {
        return;
    }
    // SAFETY: freed once here.
    unsafe { drop(Box::from_raw(crtc)) };
}

/// Plane id.
#[no_mangle]
/// # Safety
/// Plane must be live.
pub unsafe extern "C" fn novogfx_plane_id(plane: *const novogfx_plane_t) -> c_ulong {
    if plane.is_null() {
        return 0;
    }
    // SAFETY: non-NULL.
    unsafe { (*plane).id as c_ulong }
}

/// Plane type (0 = primary, 1 = cursor, 2 = overlay, matching the IDL).
#[no_mangle]
/// # Safety
/// Plane and device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_plane_type(plane: *const novogfx_plane_t) -> u32 {
    if plane.is_null() {
        return 2;
    }
    // SAFETY: non-NULL; device live.
    let p = unsafe { &*plane };
    let dev = unsafe { &(*p.device).inner };
    match novogfx::Plane::get(dev, novogfx::PlaneId(p.id)) {
        Ok(mut pl) => {
            if pl.refresh_type().is_ok() {
                match pl.plane_type() {
                    Some(novogfx::PlaneType::Primary) => 0,
                    Some(novogfx::PlaneType::Cursor) => 1,
                    Some(novogfx::PlaneType::Overlay) => 2,
                    _ => 2,
                }
            } else {
                2
            }
        }
        Err(_) => 2,
    }
}

/// Bound CRTC (NULL when unbound; owned by the caller).
#[no_mangle]
/// # Safety
/// Plane and device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_plane_current_crtc(
    plane: *const novogfx_plane_t,
) -> *mut novogfx_crtc_t {
    if plane.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: non-NULL.
    let p = unsafe { &*plane };
    // SAFETY: device live.
    let dev = unsafe { &(*p.device).inner };
    match novogfx::Plane::get(dev, novogfx::PlaneId(p.id)) {
        Ok(pl) => match pl.current_crtc() {
            Some(id) => make_crtc_handle(p.device, id)
                .map_or(ptr::null_mut(), |h| Box::into_raw(Box::new(h))),
            None => ptr::null_mut(),
        },
        Err(_) => ptr::null_mut(),
    }
}

/// Non-zero when the plane supports `drm_format`.
#[no_mangle]
/// # Safety
/// Plane and device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_plane_supports_format(
    plane: *const novogfx_plane_t,
    drm_format: c_ulong,
) -> c_int {
    if plane.is_null() {
        return 0;
    }
    let drm_format = match checked_u32(drm_format) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    // SAFETY: non-NULL.
    let p = unsafe { &*plane };
    // SAFETY: device live.
    let dev = unsafe { &(*p.device).inner };
    match novogfx::Plane::get(dev, novogfx::PlaneId(p.id)) {
        Ok(pl) => i32::from(pl.supports_format(drm_format)),
        Err(_) => 0,
    }
}

/// Free a plane handle.
#[no_mangle]
/// # Safety
/// Plane must be independently owned, not borrowed, and destroyed once.
pub unsafe extern "C" fn novogfx_plane_destroy(plane: *mut novogfx_plane_t) {
    if plane.is_null() {
        return;
    }
    // SAFETY: freed once here.
    unsafe { drop(Box::from_raw(plane)) };
}

/// Human-readable format name (static).
#[no_mangle]
pub extern "C" fn novogfx_format_name(format: c_ulong) -> *const c_char {
    if format > u32::MAX as c_ulong {
        return c"UNKNOWN".as_ptr();
    }
    match format as u32 {
        0x38424752 => c"RGB332".as_ptr(),
        0x38524742 => c"BGR233".as_ptr(),
        0x32315258 => c"XRGB4444".as_ptr(),
        0x32314258 => c"XBGR4444".as_ptr(),
        0x32315852 => c"RGBX4444".as_ptr(),
        0x32315842 => c"BGRX4444".as_ptr(),
        0x32315241 => c"ARGB4444".as_ptr(),
        0x32314241 => c"ABGR4444".as_ptr(),
        0x32314152 => c"RGBA4444".as_ptr(),
        0x32314142 => c"BGRA4444".as_ptr(),
        0x35315258 => c"XRGB1555".as_ptr(),
        0x35314258 => c"XBGR1555".as_ptr(),
        0x35315852 => c"RGBX5551".as_ptr(),
        0x35315842 => c"BGRX5551".as_ptr(),
        0x35315241 => c"ARGB1555".as_ptr(),
        0x35314241 => c"ABGR1555".as_ptr(),
        0x35314152 => c"RGBA5551".as_ptr(),
        0x35314142 => c"BGRA5551".as_ptr(),
        0x36314752 => c"RGB565".as_ptr(),
        0x36314742 => c"BGR565".as_ptr(),
        0x34324752 => c"RGB888".as_ptr(),
        0x34324742 => c"BGR888".as_ptr(),
        0x34324258 => c"XBGR8888".as_ptr(),
        0x34325852 => c"RGBX8888".as_ptr(),
        0x34325842 => c"BGRX8888".as_ptr(),
        0x34324241 => c"ABGR8888".as_ptr(),
        0x34324152 => c"RGBA8888".as_ptr(),
        0x34324142 => c"BGRA8888".as_ptr(),
        0x30335258 => c"XRGB2101010".as_ptr(),
        0x30334258 => c"XBGR2101010".as_ptr(),
        0x30335852 => c"RGBX1010102".as_ptr(),
        0x30335842 => c"BGRX1010102".as_ptr(),
        0x30335241 => c"ARGB2101010".as_ptr(),
        0x30334241 => c"ABGR2101010".as_ptr(),
        0x30334152 => c"RGBA1010102".as_ptr(),
        0x30334142 => c"BGRA1010102".as_ptr(),
        0x55595659 => c"YVYU".as_ptr(),
        0x59565955 => c"UYVY".as_ptr(),
        0x59555956 => c"VYUY".as_ptr(),
        0x3132564e => c"NV21".as_ptr(),
        0x3631564e => c"NV16".as_ptr(),
        0x3136564e => c"NV61".as_ptr(),
        0x32315559 => c"YUV420".as_ptr(),
        0x36315559 => c"YUV422".as_ptr(),
        0x34325559 => c"YUV444".as_ptr(),
        0x3432_5241 => c"ARGB8888".as_ptr(),
        0x3432_5258 => c"XRGB8888".as_ptr(),
        0x5659_5559 => c"YUYV".as_ptr(),
        0x3231_564e => c"NV12".as_ptr(),
        _ => c"UNKNOWN".as_ptr(),
    }
}

// ---------------------------------------------------------------------------
// Dumb buffers / framebuffers
// ---------------------------------------------------------------------------

/// Create a dumb buffer + framebuffer.
#[no_mangle]
/// # Safety
/// Device must be live and output writable.
pub unsafe extern "C" fn novogfx_dumb_buffer_create(
    device: *mut novogfx_device_t,
    desc: *const novogfx_dumb_buffer_desc_t,
    out_buffer: *mut *mut novogfx_dumb_buffer_t,
    out_fb: *mut *mut novogfx_framebuffer_t,
) -> u32 {
    if device.is_null() || desc.is_null() || out_buffer.is_null() || out_fb.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: both out-pointers are writable; errors publish no handles.
    unsafe {
        *out_buffer = ptr::null_mut();
        *out_fb = ptr::null_mut();
    }
    // SAFETY: pointers validated non-NULL.
    let (w, h, bpp, flags) = unsafe { ((*desc).width, (*desc).height, (*desc).bpp, (*desc).flags) };
    if flags != 0 || checked_u32(w).is_err() || checked_u32(h).is_err() || checked_u32(bpp).is_err()
    {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: device live.
    let dev = unsafe { &(*device).inner };
    match novogfx::DumbBuffer::new(dev, w as u32, h as u32, bpp as u32) {
        Ok(buf) => {
            let fb_id = buf.framebuffer_id().raw();
            let width = buf.width();
            let height = buf.height();
            let pitch = buf.pitch();
            let size = buf.size();
            let handle = buf.handle();
            let fb_box = Box::new(novogfx_framebuffer_t { id: fb_id, device });
            let fb_ptr = Box::into_raw(fb_box);
            let dumb = Box::new(novogfx_dumb_buffer_t {
                buffer: Some(buf),
                fb_handle: fb_ptr,
                width,
                height,
                pitch,
                size,
                handle,
            });
            // SAFETY: out-pointers valid.
            unsafe {
                *out_buffer = Box::into_raw(dumb);
                *out_fb = fb_ptr;
            }
            NOVOGFX_OK
        }
        Err(e) => e.status_code(),
    }
}

/// Query dumb-buffer info.
#[no_mangle]
/// # Safety
/// Buffer must be live and output writable.
pub unsafe extern "C" fn novogfx_dumb_buffer_get_info(
    buffer: *const novogfx_dumb_buffer_t,
    out_info: *mut novogfx_dumb_buffer_info_t,
) -> u32 {
    if buffer.is_null() || out_info.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: non-NULL.
    let b = unsafe { &*buffer };
    // SAFETY: out-pointer valid.
    unsafe {
        (*out_info).width = b.width as c_ulong;
        (*out_info).height = b.height as c_ulong;
        (*out_info).pitch = b.pitch as c_ulong;
        (*out_info).size = b.size as c_ulong;
        (*out_info).handle = b.handle as c_ulong;
    }
    NOVOGFX_OK
}

/// Map the buffer (valid until `unmap`).
#[no_mangle]
/// # Safety
/// Buffer must be exclusively accessed and outputs writable.
pub unsafe extern "C" fn novogfx_dumb_buffer_map(
    buffer: *mut novogfx_dumb_buffer_t,
    out_ptr: *mut *mut u8,
) -> u32 {
    if buffer.is_null() || out_ptr.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: output is writable; a failed remap exposes no stale address.
    unsafe {
        *out_ptr = ptr::null_mut();
    }
    // SAFETY: non-NULL; exclusive `&mut` is sound because the IDL requires
    // single-threaded device use and only one mapping at a time.
    let b = unsafe { &mut *buffer };
    match b.buffer.as_mut() {
        Some(buf) => {
            let slice = match buf.map() {
                Ok(slice) => slice,
                Err(e) => return e.status_code(),
            };
            // SAFETY: out-pointer valid.
            unsafe { *out_ptr = slice.as_mut_ptr() };
            NOVOGFX_OK
        }
        None => NOVOGFX_ERR_DEVICE_LOST,
    }
}

/// Release the CPU mapping; a later map call recreates it.
#[no_mangle]
/// # Safety
/// Buffer must be live; mapped pointer must no longer be accessed.
pub unsafe extern "C" fn novogfx_dumb_buffer_unmap(buffer: *mut novogfx_dumb_buffer_t) {
    if buffer.is_null() {
        return;
    }
    // SAFETY: buffer is live and no mapped pointer is accessed across unmap.
    if let Some(buf) = unsafe { &mut (*buffer).buffer } {
        let _ = buf.unmap();
    }
}

/// Framebuffer for a dumb buffer (borrowed; invalid after destroy).
#[no_mangle]
/// # Safety
/// Buffer must be live; returned framebuffer is borrowed.
pub unsafe extern "C" fn novogfx_dumb_buffer_framebuffer(
    buffer: *const novogfx_dumb_buffer_t,
) -> *mut novogfx_framebuffer_t {
    if buffer.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: non-NULL.
    unsafe { (*buffer).fb_handle }
}

/// Destroy framebuffer + mapping + GEM handle.
#[no_mangle]
/// # Safety
/// Buffer must be exclusively owned and destroyed once, with no live mappings.
pub unsafe extern "C" fn novogfx_dumb_buffer_destroy(buffer: *mut novogfx_dumb_buffer_t) {
    if buffer.is_null() {
        return;
    }
    // SAFETY: handle came from `novogfx_dumb_buffer_create`, freed once here.
    // The associated fb handle is freed first so the caller's fb pointer
    // dangles exactly as the IDL documents ("becomes invalid").
    unsafe {
        let mut dumb = Box::from_raw(buffer);
        dumb.buffer.take();
        if !dumb.fb_handle.is_null() {
            drop(Box::from_raw(dumb.fb_handle));
            dumb.fb_handle = ptr::null_mut();
        }
    }
}

/// Framebuffer id.
#[no_mangle]
/// # Safety
/// Framebuffer must be live.
pub unsafe extern "C" fn novogfx_framebuffer_id(fb: *const novogfx_framebuffer_t) -> c_ulong {
    if fb.is_null() {
        return 0;
    }
    // SAFETY: non-NULL.
    unsafe { (*fb).id as c_ulong }
}

/// Free a framebuffer handle obtained from `current_framebuffer`.
#[no_mangle]
/// # Safety
/// Framebuffer must be independently owned, not borrowed, and destroyed once.
pub unsafe extern "C" fn novogfx_framebuffer_destroy(fb: *mut novogfx_framebuffer_t) {
    if fb.is_null() {
        return;
    }
    // SAFETY: freed once here. Handles aliased by dumb buffers must NOT be
    // passed here (they are freed by `dumb_buffer_destroy`).
    unsafe { drop(Box::from_raw(fb)) };
}

// ---------------------------------------------------------------------------
// Atomic
// ---------------------------------------------------------------------------

/// Create an atomic request.
#[no_mangle]
/// # Safety
/// Device must be live and output writable.
pub unsafe extern "C" fn novogfx_atomic_request_create(
    device: *mut novogfx_device_t,
    out_request: *mut *mut novogfx_atomic_request_t,
) -> u32 {
    if device.is_null() || out_request.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: output writable and device live for the capability query.
    unsafe {
        *out_request = ptr::null_mut();
    }
    // SAFETY: device must be exclusively accessed by the caller.
    if !unsafe { &(*device).inner }
        .capabilities()
        .contains(novogfx::DeviceCaps::ATOMIC)
    {
        return NOVOGFX_ERR_NOT_SUPPORTED;
    }
    let req = Box::new(novogfx_atomic_request_t {
        device,
        crtcs: Vec::new(),
        planes: Vec::new(),
        connectors: Vec::new(),
    });
    // SAFETY: out-pointer valid.
    unsafe { *out_request = Box::into_raw(req) };
    NOVOGFX_OK
}

fn mode_c_to_rust(m: &novogfx_mode_t) -> Result<Mode, u32> {
    let name = if m.name.is_null() {
        String::new()
    } else {
        // SAFETY: the C mode contract requires a live NUL-terminated name.
        unsafe { CStr::from_ptr(m.name) }
            .to_str()
            .map_err(|_| NOVOGFX_ERR_INVALID_ARGUMENT)?
            .to_owned()
    };
    Ok(Mode::from_parts(
        checked_u32(m.clock_khz)?,
        m.hdisplay,
        m.hsync_start,
        m.hsync_end,
        m.htotal,
        m.hskew,
        m.vdisplay,
        m.vsync_start,
        m.vsync_end,
        m.vtotal,
        m.vscan,
        0,
        checked_u32(m.flags)?,
        checked_u32(m.type_)?,
        name,
    ))
}

/// Set connector + mode + framebuffer for a CRTC.
#[no_mangle]
/// # Safety
/// Handles must be live; mode and its name valid; see module pointer contract.
pub unsafe extern "C" fn novogfx_atomic_request_set_crtc(
    request: *mut novogfx_atomic_request_t,
    crtc: *const novogfx_crtc_t,
    fb: *const novogfx_framebuffer_t,
    mode: *const novogfx_mode_t,
    connector: *const novogfx_connector_t,
) -> u32 {
    if request.is_null() || crtc.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: all non-null handles are live; reject cross-device IDs before queuing.
    let same_device = unsafe {
        (*crtc).device == (*request).device
            && (fb.is_null() || (*fb).device == (*request).device)
            && (connector.is_null() || (*connector).device == (*request).device)
    };
    if !same_device {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: non-NULL per the checks above.
    let (crtc_id, fb_id, has_fb, rust_mode, conn_id, has_conn) = unsafe {
        let c = (*crtc).id;
        let (fb_id, has_fb) = if fb.is_null() {
            (0, false)
        } else {
            ((*fb).id, true)
        };
        let mode = if mode.is_null() {
            None
        } else {
            match mode_c_to_rust(&*mode) {
                Ok(mode) => Some(mode),
                Err(status) => return status,
            }
        };
        let (conn_id, has_conn) = if connector.is_null() {
            (0, false)
        } else {
            ((*connector).id, true)
        };
        (c, fb_id, has_fb, mode, conn_id, has_conn)
    };
    // SAFETY: request non-NULL.
    let req = unsafe { &mut *request };
    req.crtcs.push(AtomicCrtc {
        crtc: crtc_id,
        fb: fb_id,
        has_fb,
        mode: rust_mode,
        connector: conn_id,
        has_connector: has_conn,
    });
    NOVOGFX_OK
}

/// Queue a plane update.
#[no_mangle]
/// # Safety
/// Request and update handles must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_atomic_request_add_plane(
    request: *mut novogfx_atomic_request_t,
    update: *const novogfx_plane_update_t,
) -> u32 {
    if request.is_null() || update.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: non-NULL.
    let u = unsafe { &*update };
    if u.plane.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    if u.flags != 0xffff_ffff && u.flags & !31 != 0 {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    let mask = (u.flags as u32) & 31;
    // SAFETY: request and plane are live. Only selected optional handles are read.
    let same_device = unsafe {
        (*u.plane).device == (*request).device
            && (mask & 1 == 0 || u.fb.is_null() || (*u.fb).device == (*request).device)
            && (mask & 2 == 0 || u.crtc.is_null() || (*u.crtc).device == (*request).device)
    };
    if !same_device {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: plane handle non-NULL.
    let plane_id = unsafe { (*u.plane).id };
    let fb = if mask & 1 == 0 || u.fb.is_null() {
        None
    } else {
        // SAFETY: fb handle validated non-NULL.
        Some(unsafe { (*u.fb).id })
    };
    let crtc = if mask & 2 == 0 || u.crtc.is_null() {
        None
    } else {
        // SAFETY: crtc handle validated non-NULL.
        Some(unsafe { (*u.crtc).id })
    };
    let converted: Result<novogfx::PlaneUpdate, u32> = (|| {
        Ok(novogfx::PlaneUpdate {
            plane: plane_id,
            fb,
            crtc,
            src_x: if mask & 4 != 0 {
                checked_u32(u.src_x)?
            } else {
                0
            },
            src_y: if mask & 4 != 0 {
                checked_u32(u.src_y)?
            } else {
                0
            },
            src_w: if mask & 4 != 0 {
                checked_u32(u.src_w)?
            } else {
                0
            },
            src_h: if mask & 4 != 0 {
                checked_u32(u.src_h)?
            } else {
                0
            },
            crtc_x: if mask & 8 != 0 {
                i32::try_from(u.crtc_x).map_err(|_| NOVOGFX_ERR_INVALID_ARGUMENT)?
            } else {
                0
            },
            crtc_y: if mask & 8 != 0 {
                i32::try_from(u.crtc_y).map_err(|_| NOVOGFX_ERR_INVALID_ARGUMENT)?
            } else {
                0
            },
            crtc_w: if mask & 8 != 0 {
                checked_u32(u.crtc_w)?
            } else {
                0
            },
            crtc_h: if mask & 8 != 0 {
                checked_u32(u.crtc_h)?
            } else {
                0
            },
            zpos: if mask & 16 != 0 {
                Some(checked_u32(u.zpos)?)
            } else {
                None
            },
            update_mask: mask,
        })
    })();
    let converted = match converted {
        Ok(update) => update,
        Err(status) => return status,
    };
    if let Err(e) = converted.validate() {
        return e.status_code();
    }
    // SAFETY: request is live, exclusively accessed, and the update is validated.
    unsafe { &mut *request }.planes.push(converted);
    NOVOGFX_OK
}

/// Set a connector's CRTC binding.
#[no_mangle]
/// # Safety
/// All handles and their device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_atomic_request_set_connector_crtc(
    request: *mut novogfx_atomic_request_t,
    connector: *const novogfx_connector_t,
    crtc: *const novogfx_crtc_t,
) -> u32 {
    if request.is_null() || connector.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: handles are live; device association is checked before queuing.
    if unsafe {
        (*connector).device != (*request).device
            || (!crtc.is_null() && (*crtc).device != (*request).device)
    } {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: non-NULL.
    let conn_id = unsafe { (*connector).id };
    let (crtc_id, has) = if crtc.is_null() {
        (0, false)
    } else {
        // SAFETY: crtc handle validated non-NULL.
        (unsafe { (*crtc).id }, true)
    };
    // SAFETY: request non-NULL.
    unsafe { &mut *request }
        .connectors
        .push((conn_id, crtc_id, has));
    NOVOGFX_OK
}

/// Commit (or test-only validate) the request.
#[no_mangle]
/// # Safety
/// Request and device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_atomic_request_commit(
    request: *mut novogfx_atomic_request_t,
    flags: c_ulong,
) -> u32 {
    if request.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: request non-NULL; device outlives the request per the IDL.
    let req = unsafe { &mut *request };
    // SAFETY: device pointer stored at creation; single-threaded use.
    let dev = unsafe { &mut *req.device }.inner_mut_hack();
    let mut rust_req = novogfx::AtomicRequest::new(dev);
    for c in &req.crtcs {
        let fb = if c.has_fb { Some(c.fb) } else { None };
        let conn = if c.has_connector {
            Some(c.connector)
        } else {
            None
        };
        if rust_req
            .set_crtc(c.crtc, fb, c.mode.as_ref(), conn)
            .is_err()
        {
            return NOVOGFX_ERR_INVALID_ARGUMENT;
        }
    }
    for p in req.planes.clone() {
        if rust_req.add_plane(p).is_err() {
            return NOVOGFX_ERR_INVALID_ARGUMENT;
        }
    }
    for (conn, crtc, has) in &req.connectors {
        let c = if *has { Some(*crtc) } else { None };
        if rust_req.set_connector_crtc(*conn, c).is_err() {
            return NOVOGFX_ERR_INVALID_ARGUMENT;
        }
    }
    let cflags = match translate_commit_flags(flags) {
        Ok(flags) => flags,
        Err(status) => return status,
    };
    match rust_req.commit(cflags) {
        Ok(()) => NOVOGFX_OK,
        Err(e) => e.status_code(),
    }
}

/// Reset to empty.
#[no_mangle]
/// # Safety
/// Request must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_atomic_request_reset(request: *mut novogfx_atomic_request_t) {
    if request.is_null() {
        return;
    }
    // SAFETY: non-NULL.
    let req = unsafe { &mut *request };
    req.crtcs.clear();
    req.planes.clear();
    req.connectors.clear();
}

/// Destroy the request.
#[no_mangle]
/// # Safety
/// Request must be exclusively owned and destroyed once.
pub unsafe extern "C" fn novogfx_atomic_request_destroy(request: *mut novogfx_atomic_request_t) {
    if request.is_null() {
        return;
    }
    // SAFETY: freed once here.
    unsafe { drop(Box::from_raw(request)) };
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

/// Create an event reader.
#[no_mangle]
/// # Safety
/// Device must be live and output writable.
pub unsafe extern "C" fn novogfx_event_reader_create(
    device: *mut novogfx_device_t,
    out_reader: *mut *mut novogfx_event_reader_t,
) -> u32 {
    if device.is_null() || out_reader.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: device live.
    let fd = unsafe { (*device).inner.as_fd().as_raw_fd() };
    if set_fd_nonblocking(fd).is_err() {
        return NOVOGFX_ERR_IO;
    }
    // SAFETY: device is live for this call. Keep the file description alive
    // independently of Device, preventing reads from a recycled fd number.
    let fd = match unsafe { (*device).inner.as_fd() }.try_clone_to_owned() {
        Ok(fd) => fd,
        Err(_) => return NOVOGFX_ERR_IO,
    };
    let reader = Box::new(novogfx_event_reader_t {
        fd,
        pending: Vec::new(),
        events: Vec::new(),
        sequence: 0,
    });
    // SAFETY: out-pointer valid.
    unsafe { *out_reader = Box::into_raw(reader) };
    NOVOGFX_OK
}

fn set_fd_nonblocking(fd: std::os::fd::RawFd) -> Result<(), ()> {
    unsafe extern "C" {
        fn fcntl(fd: std::os::fd::RawFd, cmd: std::ffi::c_int, ...) -> std::ffi::c_int;
    }
    // SAFETY: fd valid; varargs fcntl with F_GETFL/F_SETFL.
    unsafe {
        let flags = fcntl(fd, 3);
        if flags < 0 {
            return Err(());
        }
        if fcntl(fd, 4, flags | 0o4000) < 0 {
            return Err(());
        }
    }
    Ok(())
}

/// Poll (never blocks; `AGAIN` when no events pending).
#[no_mangle]
/// # Safety
/// Reader must be exclusively accessed and outputs writable.
pub unsafe extern "C" fn novogfx_event_reader_poll(
    reader: *mut novogfx_event_reader_t,
    out_events: *mut *const novogfx_event_c,
    out_count: *mut c_ulong,
) -> u32 {
    if reader.is_null() || out_events.is_null() || out_count.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: outputs are writable; every failure reports an empty view.
    unsafe {
        *out_events = ptr::null();
        *out_count = 0;
    }
    // SAFETY: reader non-NULL.
    let r = unsafe { &mut *reader };
    r.events.clear();
    let mut buf = [0u8; 4096];
    // SAFETY: stack buffer; `read` writes at most its length.
    let n = unsafe { raw_read_c(r.fd.as_raw_fd(), buf.as_mut_ptr(), buf.len()) };
    if n < 0 {
        let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(11);
        if errno != 11 {
            return NOVOGFX_ERR_IO;
        }
    } else if n > 0 {
        r.pending.extend_from_slice(&buf[..n as usize]);
    }
    let (parsed, consumed) = match novogfx_event_parse(&r.pending, r.sequence) {
        Ok(parsed) => parsed,
        Err(error) => {
            r.pending.clear();
            return error.status_code();
        }
    };
    r.pending.drain(..consumed);
    if r.pending.len() > 65536 {
        r.pending.clear();
        return NOVOGFX_ERR_INTERNAL;
    }
    r.sequence = r.sequence.saturating_add(parsed.len() as u64);
    r.events = parsed;
    if r.events.is_empty() {
        // SAFETY: out-pointers valid; report empty.
        unsafe {
            *out_events = std::ptr::null();
            *out_count = 0;
        }
        return NOVOGFX_ERR_AGAIN;
    }
    // SAFETY: out-pointers valid; `events` lives until the next poll/destroy.
    unsafe {
        *out_events = r.events.as_ptr();
        *out_count = r.events.len() as c_ulong;
    }
    NOVOGFX_OK
}

fn novogfx_event_parse(buf: &[u8], base: u64) -> novogfx::Result<(Vec<novogfx_event_c>, usize)> {
    let (events, consumed) = novogfx::event::reader::parse_events_checked(buf, base)?;
    let events = events
        .into_iter()
        .map(event_to_c)
        .collect::<novogfx::Result<Vec<_>>>()?;
    Ok((events, consumed))
}

fn event_to_c(event: novogfx::Event) -> novogfx::Result<novogfx_event_c> {
    let (type_, sequence, sec, usec, crtc, user_data, status, connector) = match event {
        novogfx::Event::Vblank(e) => (
            0,
            u64::from(e.sequence),
            u64::from(e.tv_sec),
            u64::from(e.tv_usec),
            e.crtc_id,
            e.user_data,
            0,
            0,
        ),
        novogfx::Event::PageFlip(e) => (
            1,
            u64::from(e.sequence),
            u64::from(e.tv_sec),
            u64::from(e.tv_usec),
            e.crtc_id,
            e.user_data,
            e.status,
            0,
        ),
        novogfx::Event::CrtcSequence(e) => {
            // The IDL represents vblank time as seconds/microseconds; sequence
            // notifications are vblanks, not page flips, and carry no CRTC id.
            let ns = u64::try_from(e.time_ns)
                .map_err(|_| novogfx::Error::InvalidArgument("negative event timestamp".into()))?;
            (
                0,
                e.sequence,
                ns / 1_000_000_000,
                (ns % 1_000_000_000) / 1000,
                0,
                e.user_data,
                0,
                0,
            )
        }
        novogfx::Event::Hotplug(e) => (2, e.sequence, 0, 0, 0, 0, 0, e.connector_id),
    };
    let ulong = |value| {
        c_ulong::try_from(value).map_err(|_| {
            novogfx::Error::InvalidArgument("event value exceeds C unsigned long".into())
        })
    };
    Ok(novogfx_event_c {
        type_,
        sequence: ulong(sequence)?,
        tv_sec: ulong(sec)?,
        tv_usec: ulong(usec)?,
        crtc_id: c_ulong::from(crtc),
        user_data: ulong(user_data)?,
        flip_status: status,
        connector_id: c_ulong::from(connector),
    })
}

#[cfg(unix)]
unsafe fn raw_read_c(fd: std::os::fd::RawFd, buf: *mut u8, len: usize) -> isize {
    unsafe extern "C" {
        fn read(fd: std::os::fd::RawFd, buf: *mut c_void, count: usize) -> isize;
    }
    // SAFETY: stack buffer at the call site.
    unsafe { read(fd, buf.cast::<c_void>(), len) }
}

#[cfg(not(unix))]
unsafe fn raw_read_c(_fd: std::os::fd::RawFd, _buf: *mut u8, _len: usize) -> isize {
    -1
}

/// Destroy the reader.
#[no_mangle]
/// # Safety
/// Reader must be exclusively owned and destroyed once.
pub unsafe extern "C" fn novogfx_event_reader_destroy(reader: *mut novogfx_event_reader_t) {
    if reader.is_null() {
        return;
    }
    // SAFETY: freed once here.
    unsafe { drop(Box::from_raw(reader)) };
}

// ---------------------------------------------------------------------------
// Page flip / memory helpers
// ---------------------------------------------------------------------------

/// Legacy page flip, preserving the event cookie.
#[no_mangle]
/// # Safety
/// CRTC, framebuffer and device must be live and exclusively accessed.
pub unsafe extern "C" fn novogfx_crtc_page_flip(
    crtc: *mut novogfx_crtc_t,
    fb: *const novogfx_framebuffer_t,
    flags: c_ulong,
    user_data: c_ulong,
) -> u32 {
    if crtc.is_null() || fb.is_null() {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: handles validated non-NULL.
    let (dev_ptr, crtc_id, fb_id) = unsafe { ((*crtc).device, (*crtc).id, (*fb).id) };
    // SAFETY: both handles are live and must belong to the same DRM device.
    if unsafe { (*fb).device != dev_ptr } {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    if flags & !3 != 0 {
        return NOVOGFX_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: device is live and the kernel only copies the cookie; it never
    // dereferences this integer-as-pointer value.
    let ret = unsafe {
        novogfx_sys::xf86drm::drmModePageFlip(
            (*dev_ptr).inner.as_fd().as_raw_fd(),
            crtc_id,
            fb_id,
            flags as u32,
            user_data as usize as *mut c_void,
        )
    };
    if ret == 0 {
        NOVOGFX_OK
    } else {
        novogfx::Error::from_neg_errno(ret, "drmModePageFlip").status_code()
    }
}

/// Free a string returned by the API.
#[no_mangle]
/// # Safety
/// String must come from this API as owned storage and be freed once.
pub unsafe extern "C" fn novogfx_string_free(s: *mut c_char) {
    if s.is_null() {
        return;
    }
    // SAFETY: `s` came from `CString::into_raw` in this ABI.
    unsafe { drop(CString::from_raw(s)) };
}

/// Free a buffer returned by the API (no owning buffers currently).
#[no_mangle]
pub extern "C" fn novogfx_buffer_free(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    // No heap buffers are currently returned; this is a no-op guard for ABI
    // stability. (If a future function returns malloc'd buffers, free them
    // according to that function's docs, not here.)
    let _ = ptr;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

trait DeviceMutHack {
    /// Reborrow a `*mut novogfx_device_t`'s device as `&mut Device`.
    ///
    /// Sound only under the IDL's single-threaded device rule: the C caller
    /// guarantees no other `&`/`&mut` to the device is live across the call.
    fn inner_mut_hack(&mut self) -> &mut novogfx::Device;
}

impl DeviceMutHack for novogfx_device_t {
    fn inner_mut_hack(&mut self) -> &mut novogfx::Device {
        &mut self.inner
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn foreign_devices_and_narrowing_fail_before_queuing() {
        let mut a = 0u8;
        let mut b = 0u8;
        let device = ptr::from_mut(&mut a).cast::<novogfx_device_t>();
        let foreign = ptr::from_mut(&mut b).cast::<novogfx_device_t>();
        let mut request = novogfx_atomic_request_t {
            device,
            crtcs: vec![],
            planes: vec![],
            connectors: vec![],
        };
        let crtc = novogfx_crtc_t {
            device: foreign,
            id: 1,
            mode: None,
        };
        let mut plane = novogfx_plane_t { device, id: 2 };
        let mut fb = novogfx_framebuffer_t {
            device: foreign,
            id: 3,
        };
        // SAFETY: these live stack handles are only inspected for identity/IDs;
        // no accepted operation dereferences the opaque device marker pointers.
        unsafe {
            assert_eq!(
                novogfx_atomic_request_set_crtc(
                    &mut request,
                    &crtc,
                    ptr::null(),
                    ptr::null(),
                    ptr::null()
                ),
                NOVOGFX_ERR_INVALID_ARGUMENT
            );
        }
        let mut update = novogfx_plane_update_t {
            plane: &mut plane,
            fb: &mut fb,
            crtc: ptr::null_mut(),
            src_x: 0,
            src_y: 0,
            src_w: 0,
            src_h: 0,
            crtc_x: -1,
            crtc_y: -2,
            crtc_w: 64,
            crtc_h: 64,
            zpos: 0,
            flags: 1,
        };
        // SAFETY: all inspected request/update/handle pointers reference live locals.
        unsafe {
            assert_eq!(
                novogfx_atomic_request_add_plane(&mut request, &update),
                NOVOGFX_ERR_INVALID_ARGUMENT
            );
            assert!(request.planes.is_empty());
            update.flags = 8; // Unselected foreign framebuffer is ignored.
            assert_eq!(
                novogfx_atomic_request_add_plane(&mut request, &update),
                NOVOGFX_OK
            );
            assert_eq!(request.planes[0].crtc_x, -1);
            #[cfg(target_pointer_width = "64")]
            {
                update.crtc_w = u64::from(u32::MAX) + 1;
                assert_eq!(
                    novogfx_atomic_request_add_plane(&mut request, &update),
                    NOVOGFX_ERR_INVALID_ARGUMENT
                );
                assert_eq!(request.planes.len(), 1);
            }
        }
    }

    #[test]
    fn c_reader_buffers_partial_records_resets_corruption_and_never_replays() {
        use std::io::Write;
        let (input, mut output) = std::os::unix::net::UnixStream::pair().unwrap();
        input.set_nonblocking(true).unwrap();
        let mut reader = novogfx_event_reader_t {
            fd: input.into(),
            pending: vec![],
            events: vec![],
            sequence: 0,
        };
        let mut bytes = novogfx_sys::drm::DRM_EVENT_CRTC_SEQUENCE
            .to_ne_bytes()
            .to_vec();
        bytes.extend_from_slice(&32u32.to_ne_bytes());
        bytes.extend_from_slice(&42u64.to_ne_bytes());
        bytes.extend_from_slice(&1234567890i64.to_ne_bytes());
        let sequence = if std::mem::size_of::<c_ulong>() == 8 {
            u64::from(u32::MAX) + 5
        } else {
            5
        };
        bytes.extend_from_slice(&sequence.to_ne_bytes());
        let mut events = ptr::null();
        let mut count = 99;
        output.write_all(&bytes[..17]).unwrap();
        // SAFETY: reader and out-pointers are live locals; returned views are
        // inspected only before the next poll invalidates them.
        unsafe {
            assert_eq!(
                novogfx_event_reader_poll(&mut reader, &mut events, &mut count),
                NOVOGFX_ERR_AGAIN
            );
            assert!(events.is_null());
            assert_eq!(count, 0);
            output.write_all(&bytes[17..]).unwrap();
            assert_eq!(
                novogfx_event_reader_poll(&mut reader, &mut events, &mut count),
                NOVOGFX_OK
            );
            assert_eq!(
                (*events).type_,
                0,
                "sequence events are vblank notifications"
            );
            assert_eq!((*events).sequence, sequence as c_ulong);
            assert_eq!(((*events).tv_sec, (*events).tv_usec), (1, 234567));
            assert_eq!(
                novogfx_event_reader_poll(&mut reader, &mut events, &mut count),
                NOVOGFX_ERR_AGAIN
            );
            assert!(events.is_null());
            output.write_all(&[0; 8]).unwrap();
            assert_eq!(
                novogfx_event_reader_poll(&mut reader, &mut events, &mut count),
                NOVOGFX_ERR_INVALID_ARGUMENT
            );
            assert!(reader.pending.is_empty());
            output.write_all(&bytes).unwrap();
            assert_eq!(
                novogfx_event_reader_poll(&mut reader, &mut events, &mut count),
                NOVOGFX_OK
            );
            assert_eq!(count, 1);
        }
    }

    #[test]
    fn mode_name_survives_owner_moves_and_matches_c_layout() {
        // SAFETY: raw mode is plain integers and a char array, all zero-valid.
        let raw = unsafe { std::mem::zeroed::<novogfx_sys::drm_mode::drm_mode_modeinfo>() };
        let mut mode = Mode::from_raw(&raw);
        mode.name = "1920x1080".into();
        let owned = [mode_to_c(&mode)];
        // SAFETY: ModeC retains its CString allocation after moving into array.
        assert_eq!(
            unsafe { CStr::from_ptr(owned[0].inner.name) }.to_bytes(),
            b"1920x1080"
        );
        assert_eq!(mode_c_to_rust(&owned[0].inner).unwrap().name, mode.name);
        let mut invalid = owned[0].inner;
        let bad_name = CString::new(vec![0xff]).unwrap();
        invalid.name = bad_name.as_ptr();
        assert!(matches!(
            mode_c_to_rust(&invalid),
            Err(NOVOGFX_ERR_INVALID_ARGUMENT)
        ));
        #[cfg(target_pointer_width = "64")]
        {
            invalid = owned[0].inner;
            invalid.clock_khz = u64::from(u32::MAX) + 1;
            assert!(matches!(
                mode_c_to_rust(&invalid),
                Err(NOVOGFX_ERR_INVALID_ARGUMENT)
            ));
        }
        assert_eq!(
            std::mem::size_of_val(&owned[0].inner.name),
            std::mem::size_of::<*const c_char>()
        );
    }

    #[test]
    fn idl_flags_translate_to_kernel_bits() {
        assert_eq!(translate_commit_flags(1).unwrap().bits(), 0x400);
        assert_eq!(translate_commit_flags(2).unwrap().bits(), 0x200);
        assert_eq!(translate_commit_flags(4).unwrap().bits(), 1);
        assert_eq!(translate_commit_flags(8).unwrap().bits(), 0x100);
        assert_eq!(translate_commit_flags(9).unwrap().bits(), 0x500);
        assert!(translate_commit_flags(12).is_err());
        assert!(translate_commit_flags(10).is_err());
        assert!(translate_commit_flags(16).is_err());
    }
}
