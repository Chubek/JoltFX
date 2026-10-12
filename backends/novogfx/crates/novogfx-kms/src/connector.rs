//! Connector wrappers: status, type, modes, EDID.

use std::os::fd::AsRawFd;

use novogfx_core::{Device, Error, Result};
use novogfx_sys::{drm_mode, xf86drm};

use crate::{Edid, Mode};

/// Opaque connector id.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ConnectorId(pub u32);

impl ConnectorId {
    /// Raw kernel id.
    #[must_use]
    pub fn raw(&self) -> u32 {
        self.0
    }
}

impl From<u32> for ConnectorId {
    fn from(v: u32) -> Self {
        Self(v)
    }
}

impl std::fmt::Display for ConnectorId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// Connection status.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ConnectorStatus {
    /// A sink is attached.
    Connected,
    /// No sink attached.
    Disconnected,
    /// Unknown (driver could not probe).
    Unknown,
}

impl ConnectorStatus {
    /// Map the kernel `DRM_MODE_CONNECTED/DISCONNECTED/UNKNOWNCONNECTION`.
    #[must_use]
    pub fn from_raw(v: i32) -> Self {
        match v as u32 {
            drm_mode::DRM_MODE_CONNECTED => Self::Connected,
            drm_mode::DRM_MODE_DISCONNECTED => Self::Disconnected,
            _ => Self::Unknown,
        }
    }
}

/// Connector type (HDMI, DisplayPort, eDP, …).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ConnectorType {
    /// Unknown / unrecognised.
    Unknown,
    /// VGA.
    Vga,
    /// DVI-I.
    DviI,
    /// DVI-D.
    DviD,
    /// DVI-A.
    DviA,
    /// Composite.
    Composite,
    /// S-Video.
    Svideo,
    /// LVDS.
    Lvds,
    /// Component.
    Component,
    /// 9-pin DIN.
    Din,
    /// DisplayPort.
    DisplayPort,
    /// HDMI-A.
    HdmiA,
    /// HDMI-B.
    HdmiB,
    /// TV.
    Tv,
    /// Embedded DisplayPort.
    EmbeddedDisplayPort,
    /// Virtual (vkms, dummy).
    Virtual,
    /// DSI.
    Dsi,
    /// DPI.
    Dpi,
}

impl ConnectorType {
    /// Map the kernel `DRM_MODE_CONNECTOR_*` id.
    #[must_use]
    pub fn from_raw(v: u32) -> Self {
        match v {
            drm_mode::DRM_MODE_CONNECTOR_VGA => Self::Vga,
            drm_mode::DRM_MODE_CONNECTOR_DVII => Self::DviI,
            drm_mode::DRM_MODE_CONNECTOR_DVID => Self::DviD,
            drm_mode::DRM_MODE_CONNECTOR_DVIA => Self::DviA,
            drm_mode::DRM_MODE_CONNECTOR_COMPOSITE => Self::Composite,
            drm_mode::DRM_MODE_CONNECTOR_SVIDEO => Self::Svideo,
            drm_mode::DRM_MODE_CONNECTOR_LVDS => Self::Lvds,
            drm_mode::DRM_MODE_CONNECTOR_COMPONENT => Self::Component,
            drm_mode::DRM_MODE_CONNECTOR_9PIN_DIN => Self::Din,
            drm_mode::DRM_MODE_CONNECTOR_DISPLAYPORT => Self::DisplayPort,
            drm_mode::DRM_MODE_CONNECTOR_HDMIA => Self::HdmiA,
            drm_mode::DRM_MODE_CONNECTOR_HDMIB => Self::HdmiB,
            drm_mode::DRM_MODE_CONNECTOR_TV => Self::Tv,
            drm_mode::DRM_MODE_CONNECTOR_EDP => Self::EmbeddedDisplayPort,
            drm_mode::DRM_MODE_CONNECTOR_VIRTUAL => Self::Virtual,
            drm_mode::DRM_MODE_CONNECTOR_DSI => Self::Dsi,
            drm_mode::DRM_MODE_CONNECTOR_DPI => Self::Dpi,
            _ => Self::Unknown,
        }
    }
}

/// A KMS connector bound to a [`Device`] lifetime.
#[derive(Debug)]
pub struct Connector<'a> {
    device: &'a Device,
    id: ConnectorId,
    status: ConnectorStatus,
    kind: ConnectorType,
    type_id: u32,
    encoder_id: Option<u32>,
    modes: Vec<Mode>,
    mm_width: u32,
    mm_height: u32,
}

impl<'a> Connector<'a> {
    /// Query a connector by id.
    ///
    /// # Errors
    ///
    /// Returns [`Error::NotFound`] when the id does not exist.
    pub fn get(device: &'a Device, id: ConnectorId) -> Result<Self> {
        use std::os::fd::AsFd;
        let fd = device.as_fd().as_raw_fd();
        // SAFETY: fd is owned by `device` (alive for `'a`); the returned
        // pointer is NULL or a libdrm allocation we free below.
        let ptr = unsafe { xf86drm::drmModeGetConnector(fd, id.0) };
        if ptr.is_null() {
            return Err(Error::last_os_error("drmModeGetConnector"));
        }
        // SAFETY: `ptr` is a valid `drmModeConnector`; array fields are
        // valid for their counts. We copy everything out before freeing.
        let conn = unsafe {
            let c = &*ptr;
            let modes = if c.count_modes > 0 && !c.modes.is_null() {
                let slice = std::slice::from_raw_parts(c.modes, c.count_modes as usize);
                slice.iter().map(Mode::from_libdrm).collect()
            } else {
                Vec::new()
            };
            let out = Self {
                device,
                id,
                status: ConnectorStatus::from_raw(c.connection),
                kind: ConnectorType::from_raw(c.connector_type),
                type_id: c.connector_type_id,
                encoder_id: if c.encoder_id == 0 {
                    None
                } else {
                    Some(c.encoder_id)
                },
                modes,
                mm_width: c.mm_width,
                mm_height: c.mm_height,
            };
            xf86drm::drmModeFreeConnector(ptr);
            out
        };
        Ok(conn)
    }

    /// Connector id.
    #[must_use]
    pub fn id(&self) -> ConnectorId {
        self.id
    }

    /// Connection status (cached at construction; re-query after hotplug).
    #[must_use]
    pub fn status(&self) -> ConnectorStatus {
        self.status
    }

    /// Connector type.
    #[must_use]
    pub fn connector_type(&self) -> ConnectorType {
        self.kind
    }

    /// Per-type connector number (not a stable identifier).
    #[must_use]
    pub fn type_id(&self) -> u32 {
        self.type_id
    }

    /// Modes parsed once at construction.
    #[must_use]
    pub fn modes(&self) -> &[Mode] {
        &self.modes
    }

    /// Preferred mode, if any is marked preferred.
    #[must_use]
    pub fn preferred_mode(&self) -> Option<&Mode> {
        self.modes.iter().find(|m| m.is_preferred())
    }

    /// Currently bound encoder id, if any.
    #[must_use]
    pub fn current_encoder(&self) -> Option<u32> {
        self.encoder_id
    }

    /// Physical size in millimetres.
    #[must_use]
    pub fn size_mm(&self) -> (u32, u32) {
        (self.mm_width, self.mm_height)
    }

    /// Raw EDID bytes, if the `EDID` blob property exists.
    ///
    /// Returns `None` when unavailable (headless, disconnected, or no EDID).
    #[must_use]
    pub fn edid_raw(&self) -> Option<Vec<u8>> {
        self.edid_blob_via_properties().unwrap_or(None)
    }

    /// Parsed EDID, if present and valid.
    #[must_use]
    pub fn edid(&self) -> Option<Edid> {
        self.edid_raw().and_then(|b| Edid::parse(&b).ok())
    }

    /// Borrowed device.
    #[must_use]
    pub fn device(&self) -> &'a Device {
        self.device
    }

    fn edid_blob_via_properties(&self) -> Result<Option<Vec<u8>>> {
        use std::os::fd::AsFd;
        let fd = self.device.as_fd().as_raw_fd();
        // SAFETY: object id/type are plain integers; returned pointer is
        // NULL or a libdrm allocation.
        let props = unsafe {
            xf86drm::drmModeObjectGetProperties(fd, self.id.0, drm_mode::DRM_MODE_OBJECT_CONNECTOR)
        };
        if props.is_null() {
            return Ok(None);
        }
        // SAFETY: `props` is valid; we copy ids out, then free before any
        // further libdrm calls that could invalidate it.
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
            // SAFETY: property id from the kernel; NULL or allocation.
            let prop = unsafe { xf86drm::drmModeGetProperty(fd, *pid) };
            if prop.is_null() {
                continue;
            }
            // SAFETY: valid property; name is NUL-padded.
            let (is_edid, is_blob) = unsafe {
                let pr = &*prop;
                let name = c_prop_name(&pr.name);
                let out = (name == "EDID", pr.flags & drm_mode::DRM_MODE_PROP_BLOB != 0);
                xf86drm::drmModeFreeProperty(prop);
                out
            };
            if is_edid && is_blob {
                let blob_id = values.get(i).copied().unwrap_or(0) as u32;
                if blob_id == 0 {
                    return Ok(None);
                }
                // SAFETY: blob id from the kernel.
                let blob = unsafe { xf86drm::drmModeGetPropertyBlob(fd, blob_id) };
                if blob.is_null() {
                    return Ok(None);
                }
                // SAFETY: valid blob; data valid for `length`.
                let data = unsafe {
                    let b = &*blob;
                    let out = if b.length > 0 && !b.data.is_null() {
                        let s = std::slice::from_raw_parts(b.data as *const u8, b.length as usize);
                        Some(s.to_vec())
                    } else {
                        None
                    };
                    xf86drm::drmModeFreePropertyBlob(blob);
                    out
                };
                return Ok(data);
            }
        }
        Ok(None)
    }
}

fn c_prop_name(buf: &[std::os::raw::c_char]) -> String {
    buf.iter()
        .take_while(|&&c| c != 0)
        .map(|&c| c as u8 as char)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_mapping_covers_all_kernel_values() {
        assert_eq!(
            ConnectorStatus::from_raw(drm_mode::DRM_MODE_CONNECTED as i32),
            ConnectorStatus::Connected
        );
        assert_eq!(
            ConnectorStatus::from_raw(drm_mode::DRM_MODE_DISCONNECTED as i32),
            ConnectorStatus::Disconnected
        );
        assert_eq!(
            ConnectorStatus::from_raw(drm_mode::DRM_MODE_UNKNOWNCONNECTION as i32),
            ConnectorStatus::Unknown
        );
        assert_eq!(ConnectorStatus::from_raw(99), ConnectorStatus::Unknown);
    }

    #[test]
    fn connector_type_mapping() {
        assert_eq!(
            ConnectorType::from_raw(drm_mode::DRM_MODE_CONNECTOR_HDMIA),
            ConnectorType::HdmiA
        );
        assert_eq!(
            ConnectorType::from_raw(drm_mode::DRM_MODE_CONNECTOR_DISPLAYPORT),
            ConnectorType::DisplayPort
        );
        assert_eq!(
            ConnectorType::from_raw(drm_mode::DRM_MODE_CONNECTOR_VIRTUAL),
            ConnectorType::Virtual
        );
        assert_eq!(ConnectorType::from_raw(999), ConnectorType::Unknown);
    }
}
