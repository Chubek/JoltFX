//! [`AtomicRequest`] builder: accumulate CRTC/plane/connector changes,
//! then commit them with a single `drmModeAtomicCommit`.

use std::os::fd::{AsFd, AsRawFd};

use novogfx_core::{Device, Error, Result};
use novogfx_kms::Mode;
use novogfx_sys::{drm_mode, xf86drm};

use crate::CommitFlags;

/// Per-plane update descriptor, mirroring `novogfx_plane_update_t`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlaneUpdate {
    /// Plane id.
    pub plane: u32,
    /// Framebuffer id (None = disable the plane).
    pub fb: Option<u32>,
    /// CRTC id (None = disable the plane).
    pub crtc: Option<u32>,
    /// Source crop in 16.16 fixed point.
    pub src_x: u32,
    /// Source crop Y (16.16).
    pub src_y: u32,
    /// Source width (16.16).
    pub src_w: u32,
    /// Source height (16.16).
    pub src_h: u32,
    /// Destination X (signed).
    pub crtc_x: i32,
    /// Destination Y (signed).
    pub crtc_y: i32,
    /// Destination width.
    pub crtc_w: u32,
    /// Destination height.
    pub crtc_h: u32,
    /// Stacking order.
    pub zpos: Option<u32>,
    /// Fields to update: FB=1, CRTC=2, source=4, destination=8, zpos=16.
    pub update_mask: u32,
}

impl PlaneUpdate {
    /// Full-screen update for `plane` → (`fb`, `crtc`) at native resolution.
    #[must_use]
    pub fn fullscreen(plane: u32, fb: u32, crtc: u32, width: u32, height: u32) -> Self {
        Self {
            plane,
            fb: Some(fb),
            crtc: Some(crtc),
            src_x: 0,
            src_y: 0,
            src_w: width.checked_mul(1 << 16).unwrap_or(0),
            src_h: height.checked_mul(1 << 16).unwrap_or(0),
            crtc_x: 0,
            crtc_y: 0,
            crtc_w: width,
            crtc_h: height,
            zpos: None,
            update_mask: 15,
        }
    }

    /// Validate selected fields before property allocation or hardware calls.
    pub fn validate(&self) -> Result<()> {
        let disabling = self.update_mask & 3 == 3 && self.fb.is_none() && self.crtc.is_none();
        if self.plane == 0
            || self.update_mask & !31 != 0
            || (self.update_mask & 1 != 0 && self.fb == Some(0))
            || (self.update_mask & 2 != 0 && self.crtc == Some(0))
            || (self.update_mask & 3 == 3 && self.fb.is_some() != self.crtc.is_some())
            || (!disabling && self.update_mask & 4 != 0 && (self.src_w == 0 || self.src_h == 0))
            || (!disabling && self.update_mask & 8 != 0 && (self.crtc_w == 0 || self.crtc_h == 0))
            || (self.update_mask & 16 != 0 && self.zpos.is_none())
        {
            return Err(Error::InvalidArgument(
                "invalid selected plane fields".into(),
            ));
        }
        Ok(())
    }

    /// Disable `plane`.
    #[must_use]
    pub fn disable(plane: u32) -> Self {
        Self {
            plane,
            fb: None,
            crtc: None,
            src_x: 0,
            src_y: 0,
            src_w: 0,
            src_h: 0,
            crtc_x: 0,
            crtc_y: 0,
            crtc_w: 0,
            crtc_h: 0,
            zpos: None,
            update_mask: 3,
        }
    }
}

struct CrtcBinding {
    crtc: u32,
    fb: Option<u32>,
    mode: Option<Mode>,
    connector: Option<u32>,
}

struct ConnectorBinding {
    connector: u32,
    crtc: Option<u32>,
}

/// A pending atomic commit.
///
/// Holds `&mut Device` so only one commit is in flight per device (see
/// AGENTS.md concurrency rules). Blob properties are created and destroyed
/// within [`AtomicRequest::commit`]; a failed commit leaves no dangling
/// blob or property references.
pub struct AtomicRequest<'a> {
    device: &'a mut Device,
    crtcs: Vec<CrtcBinding>,
    planes: Vec<PlaneUpdate>,
    connectors: Vec<ConnectorBinding>,
}

impl<'a> AtomicRequest<'a> {
    /// Create an empty request for `device`.
    pub fn new(device: &'a mut Device) -> Self {
        Self {
            device,
            crtcs: Vec::new(),
            planes: Vec::new(),
            connectors: Vec::new(),
        }
    }

    /// Set the connector + mode + framebuffer for a CRTC.
    ///
    /// Pass `fb = None` to disable the CRTC, `mode = None` to keep the
    /// current mode, and `connector = None` to leave routing untouched.
    pub fn set_crtc(
        &mut self,
        crtc: u32,
        fb: Option<u32>,
        mode: Option<&Mode>,
        connector: Option<u32>,
    ) -> Result<()> {
        if crtc == 0 || fb == Some(0) || connector == Some(0) {
            return Err(Error::InvalidArgument("KMS id 0 is invalid".to_string()));
        }
        self.crtcs.push(CrtcBinding {
            crtc,
            fb,
            mode: mode.cloned(),
            connector,
        });
        Ok(())
    }

    /// Queue a plane update (consuming-builder style via `with_*` alias).
    pub fn add_plane(&mut self, update: PlaneUpdate) -> Result<()> {
        update.validate()?;
        self.planes.push(update);
        Ok(())
    }

    /// Consuming-builder alias for [`AtomicRequest::add_plane`].
    pub fn with_plane(mut self, update: PlaneUpdate) -> Result<Self> {
        self.add_plane(update)?;
        Ok(self)
    }

    /// Set a connector's CRTC binding without touching the mode.
    pub fn set_connector_crtc(&mut self, connector: u32, crtc: Option<u32>) -> Result<()> {
        if connector == 0 || crtc == Some(0) {
            return Err(Error::InvalidArgument(
                "connector id 0 is invalid".to_string(),
            ));
        }
        self.connectors.push(ConnectorBinding { connector, crtc });
        Ok(())
    }

    /// Number of queued CRTC bindings (test helper, no device required).
    #[must_use]
    pub fn len_crtcs(&self) -> usize {
        self.crtcs.len()
    }

    /// Number of queued plane updates.
    #[must_use]
    pub fn len_planes(&self) -> usize {
        self.planes.len()
    }

    /// Returns `true` when no changes are queued.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.crtcs.is_empty() && self.planes.is_empty() && self.connectors.is_empty()
    }

    /// Reset to an empty state, discarding pending changes.
    pub fn reset(&mut self) {
        self.crtcs.clear();
        self.planes.clear();
        self.connectors.clear();
    }

    /// Validate without applying (`TEST_ONLY`).
    ///
    /// Must not mutate hardware state and must not emit events.
    pub fn test(&mut self) -> Result<()> {
        self.commit_inner(CommitFlags::TEST_ONLY | CommitFlags::ALLOW_MODESET)
    }

    /// Commit with the given flags.
    ///
    /// Blob properties are created before the commit and destroyed on every
    /// exit path (success or failure).
    pub fn commit(&mut self, flags: CommitFlags) -> Result<()> {
        self.commit_inner(flags)
    }

    fn commit_inner(&mut self, flags: CommitFlags) -> Result<()> {
        if flags.bits() & !CommitFlags::all().bits() != 0
            || (flags.contains(CommitFlags::TEST_ONLY)
                && flags.bits() & (CommitFlags::NONBLOCK | CommitFlags::PAGE_FLIP_EVENT).bits()
                    != 0)
        {
            return Err(Error::InvalidArgument("invalid atomic commit flags".into()));
        }
        if !self
            .device
            .capabilities()
            .contains(novogfx_core::DeviceCaps::ATOMIC)
        {
            return Err(Error::NotSupported(
                "enable atomic mode before committing".into(),
            ));
        }
        let fd = self.device.as_fd().as_raw_fd();
        // SAFETY: `drmModeAtomicAlloc` takes no arguments and returns NULL
        // or a heap allocation owned by the caller.
        let req = unsafe { xf86drm::drmModeAtomicAlloc() };
        if req.is_null() {
            return Err(Error::OutOfMemory(
                "drmModeAtomicAlloc returned NULL".to_string(),
            ));
        }
        let mut blobs: Vec<u32> = Vec::new();
        let result = self.populate_and_commit(fd, req, flags, &mut blobs);
        // Teardown: destroy blobs first, then free the request, on every path.
        for blob in blobs {
            // SAFETY: blob ids were just created by us on `fd`; destroying
            // a stale id is a harmless kernel ENOENT, reported but ignored.
            let _ = unsafe { xf86drm::drmModeDestroyPropertyBlob(fd, blob) };
        }
        // SAFETY: `req` came from `drmModeAtomicAlloc` and is freed once here.
        unsafe { xf86drm::drmModeAtomicFree(req) };
        result
    }

    fn populate_and_commit(
        &self,
        fd: std::os::fd::RawFd,
        req: *mut xf86drm::drmModeAtomicReq,
        flags: CommitFlags,
        blobs: &mut Vec<u32>,
    ) -> Result<()> {
        let mut primary_updates = Vec::new();
        for c in &self.crtcs {
            // Atomic FB_ID belongs to the primary plane, never the CRTC.
            let active = find_prop(fd, c.crtc, drm_mode::DRM_MODE_OBJECT_CRTC, "ACTIVE")?;
            let mode_id = find_prop(fd, c.crtc, drm_mode::DRM_MODE_OBJECT_CRTC, "MODE_ID")?;
            // ACTIVE = (fb.is_some()).
            if let Some(pid) = active {
                add_prop(req, c.crtc, pid, u64::from(c.fb.is_some()))?;
            }
            let resources = self.device.resource_snapshot()?;
            let index = resources
                .crtcs
                .iter()
                .position(|id| *id == c.crtc)
                .ok_or_else(|| Error::NotFound(format!("CRTC {}", c.crtc)))?;
            let mut primary = None;
            for id in &resources.planes {
                let mut plane = novogfx_kms::Plane::get(self.device, novogfx_kms::PlaneId(*id))?;
                if index < 32 && plane.possible_crtcs() & (1u32 << index) != 0 {
                    plane.refresh_type()?;
                    if plane.plane_type() == Some(novogfx_kms::PlaneType::Primary)
                        && (plane.current_crtc().is_none() || plane.current_crtc() == Some(c.crtc))
                    {
                        primary = Some(*id);
                        break;
                    }
                }
            }
            let primary =
                primary.ok_or_else(|| Error::NotSupported("no compatible primary plane".into()))?;
            primary_updates.push(if let Some(fb) = c.fb {
                let current = novogfx_kms::Crtc::get(self.device, novogfx_kms::CrtcId(c.crtc))?;
                let mode = c
                    .mode
                    .as_ref()
                    .or_else(|| current.current_mode())
                    .ok_or_else(|| Error::InvalidArgument("enabled CRTC requires a mode".into()))?;
                PlaneUpdate::fullscreen(
                    primary,
                    fb,
                    c.crtc,
                    u32::from(mode.hdisplay),
                    u32::from(mode.vdisplay),
                )
            } else {
                PlaneUpdate::disable(primary)
            });
            // MODE_ID blob (created + tracked for teardown below).
            if let Some(mode) = &c.mode {
                if let Some(pid) = mode_id {
                    let blob = create_mode_blob(fd, mode)?;
                    blobs.push(blob);
                    add_prop(req, c.crtc, pid, u64::from(blob))?;
                }
            } else if c.fb.is_none() {
                if let Some(pid) = mode_id {
                    add_prop(req, c.crtc, pid, 0)?;
                }
            }
            // Connector routing for this CRTC, if requested.
            if let Some(conn) = c.connector {
                if let Some(pid) =
                    find_prop(fd, conn, drm_mode::DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID")?
                {
                    add_prop(req, conn, pid, u64::from(c.fb.map_or(0, |_| c.crtc)))?;
                }
            }
        }
        for conn in &self.connectors {
            if let Some(pid) = find_prop(
                fd,
                conn.connector,
                drm_mode::DRM_MODE_OBJECT_CONNECTOR,
                "CRTC_ID",
            )? {
                add_prop(req, conn.connector, pid, u64::from(conn.crtc.unwrap_or(0)))?;
            }
        }
        for p in primary_updates.iter().chain(&self.planes) {
            p.validate()?;
            if p.update_mask == 0 {
                continue;
            }
            if p.update_mask & 1 != 0 {
                if let Some(pid) = find_prop(fd, p.plane, drm_mode::DRM_MODE_OBJECT_PLANE, "FB_ID")?
                {
                    add_prop(req, p.plane, pid, u64::from(p.fb.unwrap_or(0)))?;
                }
            }
            if p.update_mask & 2 != 0 {
                if let Some(pid) =
                    find_prop(fd, p.plane, drm_mode::DRM_MODE_OBJECT_PLANE, "CRTC_ID")?
                {
                    add_prop(req, p.plane, pid, u64::from(p.crtc.unwrap_or(0)))?;
                }
            }
            // Source / destination rectangles when the plane stays enabled.
            {
                for (name, value) in [
                    ("SRC_X", u64::from(p.src_x)),
                    ("SRC_Y", u64::from(p.src_y)),
                    ("SRC_W", u64::from(p.src_w)),
                    ("SRC_H", u64::from(p.src_h)),
                    ("CRTC_X", (p.crtc_x as i64) as u64),
                    ("CRTC_Y", (p.crtc_y as i64) as u64),
                    ("CRTC_W", u64::from(p.crtc_w)),
                    ("CRTC_H", u64::from(p.crtc_h)),
                ] {
                    let bit = if name.starts_with("SRC_") { 4 } else { 8 };
                    if p.update_mask & bit == 0 {
                        continue;
                    }
                    if let Some(pid) =
                        find_prop(fd, p.plane, drm_mode::DRM_MODE_OBJECT_PLANE, name)?
                    {
                        add_prop(req, p.plane, pid, value)?;
                    }
                }
                if let Some(z) = p.zpos.filter(|_| p.update_mask & 16 != 0) {
                    if let Some(pid) =
                        find_prop(fd, p.plane, drm_mode::DRM_MODE_OBJECT_PLANE, "zpos")?
                    {
                        add_prop(req, p.plane, pid, u64::from(z))?;
                    }
                }
            }
        }
        // SAFETY: `req` was allocated above and populated with validated
        // object/property ids; `drmModeAtomicCommit` reads it synchronously.
        let ret =
            unsafe { xf86drm::drmModeAtomicCommit(fd, req, flags.bits(), std::ptr::null_mut()) };
        if ret != 0 {
            return Err(Error::from_neg_errno(ret, "drmModeAtomicCommit"));
        }
        Ok(())
    }
}

fn add_prop(req: *mut xf86drm::drmModeAtomicReq, object: u32, prop: u32, value: u64) -> Result<()> {
    // SAFETY: `req` is a live allocation from `drmModeAtomicAlloc`; ids and
    // value are plain integers.
    let ret = unsafe { xf86drm::drmModeAtomicAddProperty(req, object, prop, value) };
    if ret < 0 {
        return Err(Error::InvalidArgument(format!(
            "drmModeAtomicAddProperty failed for object {object} prop {prop}"
        )));
    }
    Ok(())
}

fn find_prop(
    fd: std::os::fd::RawFd,
    object: u32,
    object_type: u32,
    name: &str,
) -> Result<Option<u32>> {
    // SAFETY: plain integers; NULL or libdrm allocation.
    let props = unsafe { xf86drm::drmModeObjectGetProperties(fd, object, object_type) };
    if props.is_null() {
        // Missing object → propagate the kernel error.
        return Err(Error::last_os_error("drmModeObjectGetProperties"));
    }
    // SAFETY: valid properties; copy ids out, then free before resolving names.
    let (ids, _) = unsafe {
        let p = &*props;
        let n = p.count_props as usize;
        let ids = if n > 0 && !p.props.is_null() {
            std::slice::from_raw_parts(p.props, n).to_vec()
        } else {
            Vec::new()
        };
        xf86drm::drmModeFreeObjectProperties(props);
        (ids, ())
    };
    for pid in ids {
        // SAFETY: kernel-provided id.
        let prop = unsafe { xf86drm::drmModeGetProperty(fd, pid) };
        if prop.is_null() {
            continue;
        }
        // SAFETY: valid property; name NUL-padded.
        let matches = unsafe {
            let pr = &*prop;
            let pname: String = pr
                .name
                .iter()
                .take_while(|&&c| c != 0)
                .map(|&c| c as u8 as char)
                .collect();
            xf86drm::drmModeFreeProperty(prop);
            pname == name
        };
        if matches {
            return Ok(Some(pid));
        }
    }
    Err(Error::NotSupported(format!(
        "object {object} lacks required property {name}"
    )))
}

fn create_mode_blob(fd: std::os::fd::RawFd, mode: &Mode) -> Result<u32> {
    // Translate our snapshot into the kernel modeinfo layout.
    let mut raw = novogfx_sys::drm_mode::drm_mode_modeinfo {
        clock: mode.clock_khz,
        hdisplay: mode.hdisplay,
        hsync_start: mode.hsync_start,
        hsync_end: mode.hsync_end,
        htotal: mode.htotal,
        hskew: mode.hskew,
        vdisplay: mode.vdisplay,
        vsync_start: mode.vsync_start,
        vsync_end: mode.vsync_end,
        vtotal: mode.vtotal,
        vscan: mode.vscan,
        vrefresh: mode.vrefresh,
        flags: mode.flags,
        type_: mode.kind,
        name: [0; novogfx_sys::drm_mode::DRM_DISPLAY_MODE_LEN],
    };
    let bytes = mode.name.as_bytes();
    let n = bytes.len().min(raw.name.len() - 1);
    for (i, b) in bytes.iter().take(n).enumerate() {
        raw.name[i] = *b as std::os::raw::c_char;
    }
    let mut blob_id: u32 = 0;
    // SAFETY: `raw` is a stack struct; libdrm copies `size` bytes synchronously.
    let ret = unsafe {
        xf86drm::drmModeCreatePropertyBlob(
            fd,
            std::ptr::from_ref(&raw).cast::<std::ffi::c_void>(),
            std::mem::size_of_val(&raw),
            &mut blob_id,
        )
    };
    if ret != 0 {
        return Err(Error::from_neg_errno(
            ret,
            "drmModeCreatePropertyBlob(MODE_ID)",
        ));
    }
    Ok(blob_id)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plane_update_helpers() {
        let u = PlaneUpdate::fullscreen(50, 100, 10, 1920, 1080);
        assert_eq!(u.src_w, 1920 << 16);
        assert_eq!(u.src_h, 1080 << 16);
        assert_eq!(u.crtc_w, 1920);
        let d = PlaneUpdate::disable(50);
        assert_eq!(d.fb, None);
        assert_eq!(d.crtc, None);
    }

    #[test]
    fn rejects_overflow_and_invalid_masks_without_a_device() {
        assert!(PlaneUpdate::fullscreen(1, 2, 3, 65536, 64)
            .validate()
            .is_err());
        let mut update = PlaneUpdate::fullscreen(1, 2, 3, 64, 64);
        assert!(update.validate().is_ok());
        update.update_mask = 32;
        assert!(update.validate().is_err());
        update.update_mask = 1;
        update.crtc = None;
        update.src_w = 0;
        assert!(
            update.validate().is_ok(),
            "unselected source/CRTC are ignored"
        );
        update.fb = Some(0);
        assert!(update.validate().is_err());
        assert!(PlaneUpdate::disable(1).validate().is_ok());
        let mut all = PlaneUpdate::disable(1);
        all.update_mask = 31;
        all.zpos = Some(0);
        assert!(
            all.validate().is_ok(),
            "ALL also supports disabling the plane"
        );
    }
}
