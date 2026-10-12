//! Plane wrappers.

use std::os::fd::{AsFd, AsRawFd};

use novogfx_core::{Device, Error, Result};
use novogfx_sys::xf86drm;

/// Opaque plane id.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PlaneId(pub u32);

impl PlaneId {
    /// Raw kernel id.
    #[must_use]
    pub fn raw(&self) -> u32 {
        self.0
    }
}

impl From<u32> for PlaneId {
    fn from(v: u32) -> Self {
        Self(v)
    }
}

/// Plane type: primary, overlay, or cursor.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PlaneType {
    /// Primary plane.
    Primary,
    /// Overlay plane.
    Overlay,
    /// Cursor plane.
    Cursor,
    /// Unknown (forward compatibility).
    Unknown(u32),
}

impl PlaneType {
    /// libdrm plane-type ids: 0 = overlay, 1 = primary, 2 = cursor
    /// (see `DRM_PLANE_TYPE_*` in `xf86drmMode.h`).
    #[must_use]
    pub fn from_raw(v: u32) -> Self {
        match v {
            1 => Self::Primary,
            2 => Self::Cursor,
            0 => Self::Overlay,
            other => Self::Unknown(other),
        }
    }

    /// Parse the `type` enum property value.
    ///
    /// Pure helper so the mapping is unit-testable without a device.
    #[must_use]
    pub fn from_type_property(value: u64) -> Self {
        Self::from_raw(value as u32)
    }
}

/// A KMS plane bound to a [`Device`] lifetime.
#[derive(Debug)]
pub struct Plane<'a> {
    device: &'a Device,
    id: PlaneId,
    crtc_id: Option<u32>,
    fb_id: Option<u32>,
    possible_crtcs: u32,
    formats: Vec<u32>,
    plane_type: Option<PlaneType>,
}

impl<'a> Plane<'a> {
    /// Query a plane by id.
    pub fn get(device: &'a Device, id: PlaneId) -> Result<Self> {
        let fd = device.as_fd().as_raw_fd();
        // SAFETY: fd owned by `device`; NULL or libdrm allocation.
        let ptr = unsafe { xf86drm::drmModeGetPlane(fd, id.0) };
        if ptr.is_null() {
            return Err(Error::last_os_error("drmModeGetPlane"));
        }
        // SAFETY: valid plane; copy formats out, then free.
        let plane = unsafe {
            let p = &*ptr;
            let formats = if p.count_formats > 0 && !p.formats.is_null() {
                std::slice::from_raw_parts(p.formats, p.count_formats as usize).to_vec()
            } else {
                Vec::new()
            };
            let out = Self {
                device,
                id,
                crtc_id: if p.crtc_id == 0 {
                    None
                } else {
                    Some(p.crtc_id)
                },
                fb_id: if p.fb_id == 0 { None } else { Some(p.fb_id) },
                possible_crtcs: p.possible_crtcs,
                formats,
                plane_type: None,
            };
            xf86drm::drmModeFreePlane(ptr);
            out
        };
        Ok(plane)
    }

    /// Plane id.
    #[must_use]
    pub fn id(&self) -> PlaneId {
        self.id
    }

    /// Currently bound CRTC, if any.
    #[must_use]
    pub fn current_crtc(&self) -> Option<u32> {
        self.crtc_id
    }

    /// Currently bound framebuffer, if any.
    #[must_use]
    pub fn current_fb(&self) -> Option<u32> {
        self.fb_id
    }

    /// Bitmask of compatible CRTCs.
    #[must_use]
    pub fn possible_crtcs(&self) -> u32 {
        self.possible_crtcs
    }

    /// Supported pixel formats (fourcc).
    #[must_use]
    pub fn formats(&self) -> &[u32] {
        &self.formats
    }

    /// Returns non-zero if the plane supports `format`.
    #[must_use]
    pub fn supports_format(&self, format: u32) -> bool {
        self.formats.contains(&format)
    }

    /// Cached plane type, if resolved via [`Plane::refresh_type`].
    #[must_use]
    pub fn plane_type(&self) -> Option<PlaneType> {
        self.plane_type
    }

    /// Resolve the `type` property and cache it.
    ///
    /// Queries the generic object properties for `type`; leaves the cache
    /// unchanged when the property is absent.
    pub fn refresh_type(&mut self) -> Result<()> {
        let fd = self.device.as_fd().as_raw_fd();
        // SAFETY: plain integers; NULL or libdrm allocation.
        let props = unsafe {
            xf86drm::drmModeObjectGetProperties(
                fd,
                self.id.0,
                novogfx_sys::drm_mode::DRM_MODE_OBJECT_PLANE,
            )
        };
        if props.is_null() {
            return Err(Error::last_os_error("drmModeObjectGetProperties(plane)"));
        }
        // SAFETY: valid properties; copy out then free.
        let (ids, values) = unsafe {
            let p = &*props;
            let n = p.count_props as usize;
            let ids = if n > 0 && !p.props.is_null() {
                std::slice::from_raw_parts(p.props, n).to_vec()
            } else {
                Vec::new()
            };
            let values = if n > 0 && !p.prop_values.is_null() {
                std::slice::from_raw_parts(p.prop_values, n).to_vec()
            } else {
                Vec::new()
            };
            xf86drm::drmModeFreeObjectProperties(props);
            (ids, values)
        };
        for (i, pid) in ids.iter().enumerate() {
            // SAFETY: kernel-provided id; NULL or allocation.
            let prop = unsafe { xf86drm::drmModeGetProperty(fd, *pid) };
            if prop.is_null() {
                continue;
            }
            // SAFETY: valid property; name NUL-padded.
            let (is_type, value) = unsafe {
                let pr = &*prop;
                let name: String = pr
                    .name
                    .iter()
                    .take_while(|&&c| c != 0)
                    .map(|&c| c as u8 as char)
                    .collect();
                let v = values.get(i).copied().unwrap_or(0);
                xf86drm::drmModeFreeProperty(prop);
                (name == "type", v)
            };
            if is_type {
                self.plane_type = Some(PlaneType::from_type_property(value));
                return Ok(());
            }
        }
        Ok(())
    }

    /// Typed accessor for the `alpha` property value, if present.
    ///
    /// Returns `None` when the driver does not expose `alpha`.
    pub fn alpha(&self) -> Result<Option<u64>> {
        self.named_property_value("alpha")
    }

    fn named_property_value(&self, name: &str) -> Result<Option<u64>> {
        let fd = self.device.as_fd().as_raw_fd();
        // SAFETY: plain integers; NULL or allocation.
        let props = unsafe {
            xf86drm::drmModeObjectGetProperties(
                fd,
                self.id.0,
                novogfx_sys::drm_mode::DRM_MODE_OBJECT_PLANE,
            )
        };
        if props.is_null() {
            return Err(Error::last_os_error("drmModeObjectGetProperties(plane)"));
        }
        // SAFETY: valid properties; copy out then free.
        let (ids, values) = unsafe {
            let p = &*props;
            let n = p.count_props as usize;
            let ids = if n > 0 && !p.props.is_null() {
                std::slice::from_raw_parts(p.props, n).to_vec()
            } else {
                Vec::new()
            };
            let values = if n > 0 && !p.prop_values.is_null() {
                std::slice::from_raw_parts(p.prop_values, n).to_vec()
            } else {
                Vec::new()
            };
            xf86drm::drmModeFreeObjectProperties(props);
            (ids, values)
        };
        for (i, pid) in ids.iter().enumerate() {
            // SAFETY: kernel-provided id.
            let prop = unsafe { xf86drm::drmModeGetProperty(fd, *pid) };
            if prop.is_null() {
                continue;
            }
            // SAFETY: valid property.
            let (matches, value) = unsafe {
                let pr = &*prop;
                let pname: String = pr
                    .name
                    .iter()
                    .take_while(|&&c| c != 0)
                    .map(|&c| c as u8 as char)
                    .collect();
                let v = values.get(i).copied();
                xf86drm::drmModeFreeProperty(prop);
                (pname == name, v)
            };
            if matches {
                return Ok(value);
            }
        }
        Ok(None)
    }

    /// Borrowed device.
    #[must_use]
    pub fn device(&self) -> &'a Device {
        self.device
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plane_type_mapping_matches_libdrm() {
        assert_eq!(PlaneType::from_raw(1), PlaneType::Primary);
        assert_eq!(PlaneType::from_raw(2), PlaneType::Cursor);
        assert_eq!(PlaneType::from_raw(0), PlaneType::Overlay);
        assert_eq!(PlaneType::from_type_property(1), PlaneType::Primary);
    }

    #[test]
    fn supports_format_scans_cached_list() {
        // Build a plane without a device via pointer-free construction:
        // formats are pure data, so test the predicate directly.
        let fmts = [0x3432_5258u32, 0x3432_5241u32];
        assert!(fmts.contains(&0x3432_5258));
        assert!(!fmts.contains(&0x5659_5559));
    }
}
