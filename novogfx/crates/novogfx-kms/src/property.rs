//! Property handles, well-known names, and name resolution.
//!
//! Properties are resolved by name lazily and cached per object (see the
//! typed wrappers). This module holds the name table and the mockable
//! resolution helper.

use novogfx_sys::drm_mode;

/// Opaque property id.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PropertyId(pub u32);

impl PropertyId {
    /// Raw kernel id.
    #[must_use]
    pub fn raw(&self) -> u32 {
        self.0
    }
}

impl From<u32> for PropertyId {
    fn from(v: u32) -> Self {
        Self(v)
    }
}

impl fmt::Display for PropertyId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

use std::fmt;

/// A resolved DRM property: id, name, and type flags.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Property {
    /// Kernel id.
    pub id: PropertyId,
    /// Symbolic name, e.g. `"MODE_ID"`.
    pub name: String,
    /// Raw `DRM_MODE_PROP_*` flags.
    pub flags: u32,
}

impl Property {
    /// Create a resolved property snapshot.
    #[must_use]
    pub fn new(id: PropertyId, name: String, flags: u32) -> Self {
        Self { id, name, flags }
    }

    /// Returns `true` if this is a blob property.
    #[must_use]
    pub fn is_blob(&self) -> bool {
        self.flags & drm_mode::DRM_MODE_PROP_BLOB != 0
    }

    /// Returns `true` if this property is immutable.
    #[must_use]
    pub fn is_immutable(&self) -> bool {
        self.flags & drm_mode::DRM_MODE_PROP_IMMUTABLE != 0
    }

    /// Returns `true` if this is an enum property.
    #[must_use]
    pub fn is_enum(&self) -> bool {
        self.flags & drm_mode::DRM_MODE_PROP_ENUM != 0
    }

    /// Returns `true` if this is a range property.
    #[must_use]
    pub fn is_range(&self) -> bool {
        self.flags & drm_mode::DRM_MODE_PROP_RANGE != 0
    }
}

/// Well-known atomic/KMS property names.
pub mod well_known {
    /// Active CRTC / head.
    pub const ACTIVE: &str = "ACTIVE";
    /// Framebuffer binding.
    pub const FB_ID: &str = "FB_ID";
    /// Mode blob.
    pub const MODE_ID: &str = "MODE_ID";
    /// Connector-to-CRTC binding.
    pub const CRTC_ID: &str = "CRTC_ID";
    /// Plane source crop (16.16 fixed point).
    pub const SRC_X: &str = "SRC_X";
    /// Plane source crop Y.
    pub const SRC_Y: &str = "SRC_Y";
    /// Plane source width.
    pub const SRC_W: &str = "SRC_W";
    /// Plane source height.
    pub const SRC_H: &str = "SRC_H";
    /// Plane destination X.
    pub const CRTC_X: &str = "CRTC_X";
    /// Plane destination Y.
    pub const CRTC_Y: &str = "CRTC_Y";
    /// Plane destination width.
    pub const CRTC_W: &str = "CRTC_W";
    /// Plane destination height.
    pub const CRTC_H: &str = "CRTC_H";
    /// Plane type (`primary`/`overlay`/`cursor`).
    pub const TYPE: &str = "type";
    /// Plane alpha / opacity.
    pub const ALPHA: &str = "alpha";
    /// Plane z-order.
    pub const ZPOS: &str = "zpos";
    /// Supported formats blob.
    pub const IN_FORMATS: &str = "IN_FORMATS";
    /// EDID blob.
    pub const EDID: &str = "EDID";
    /// DPMS state.
    pub const DPMS: &str = "DPMS";

    /// All names in the table (used by docs/tests).
    #[must_use]
    pub fn all() -> &'static [&'static str] {
        &[
            ACTIVE, FB_ID, MODE_ID, CRTC_ID, SRC_X, SRC_Y, SRC_W, SRC_H, CRTC_X, CRTC_Y, CRTC_W,
            CRTC_H, TYPE, ALPHA, ZPOS, IN_FORMATS, EDID, DPMS,
        ]
    }
}

/// Resolve a property id by name from a `(id, name)` snapshot list.
///
/// Pure and unit-testable: takes the object-property list as `(id, name)`
/// pairs (as returned by `drmModeObjectGetProperties` + `drmModeGetProperty`)
/// and returns the matching id.
///
/// Returns `None` when the name is absent.
#[must_use]
pub fn resolve_by_name(props: &[(u32, String)], name: &str) -> Option<PropertyId> {
    props
        .iter()
        .find(|(_, n)| n == name)
        .map(|(id, _)| PropertyId(*id))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mock_props() -> Vec<(u32, String)> {
        vec![
            (10, "FB_ID".to_string()),
            (11, "MODE_ID".to_string()),
            (12, "ACTIVE".to_string()),
        ]
    }

    #[test]
    fn resolves_known_names() {
        let props = mock_props();
        assert_eq!(resolve_by_name(&props, "FB_ID"), Some(PropertyId(10)));
        assert_eq!(resolve_by_name(&props, "ACTIVE"), Some(PropertyId(12)));
        assert_eq!(resolve_by_name(&props, "MISSING"), None);
        // Case-sensitive: kernel names are uppercase.
        assert_eq!(resolve_by_name(&props, "fb_id"), None);
    }

    #[test]
    fn property_type_predicates() {
        let blob = Property::new(
            PropertyId(1),
            "MODE_ID".into(),
            drm_mode::DRM_MODE_PROP_BLOB,
        );
        assert!(blob.is_blob());
        assert!(!blob.is_enum());
        let e = Property::new(PropertyId(2), "DPMS".into(), drm_mode::DRM_MODE_PROP_ENUM);
        assert!(e.is_enum());
        assert!(!e.is_blob());
        let r = Property::new(PropertyId(3), "alpha".into(), drm_mode::DRM_MODE_PROP_RANGE);
        assert!(r.is_range());
        let im = Property::new(
            PropertyId(4),
            "type".into(),
            drm_mode::DRM_MODE_PROP_ENUM | drm_mode::DRM_MODE_PROP_IMMUTABLE,
        );
        assert!(im.is_immutable());
    }

    #[test]
    fn well_known_table_is_sorted_and_unique() {
        let all = well_known::all();
        assert!(all.contains(&well_known::MODE_ID));
        assert!(all.contains(&well_known::FB_ID));
        assert!(all.contains(&well_known::CRTC_ID));
        let mut sorted = all.to_vec();
        sorted.sort_unstable();
        sorted.dedup();
        assert_eq!(sorted.len(), all.len(), "duplicate well-known names");
    }

    #[test]
    fn bitflag_conversions_match_kernel() {
        assert_eq!(drm_mode::DRM_MODE_PROP_BLOB, 1 << 4);
        assert_eq!(drm_mode::DRM_MODE_PROP_ENUM, 1 << 3);
        assert_eq!(drm_mode::DRM_MODE_PROP_ATOMIC, 0x8000_0000);
    }
}
