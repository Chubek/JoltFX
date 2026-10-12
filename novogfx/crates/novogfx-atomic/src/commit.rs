//! Commit flags for atomic requests.

use novogfx_sys::drm_mode;

/// Atomic commit flags, mirroring `novogfx_commit_flags_t` in the IDL.
///
/// Hand-rolled `bitflags` equivalent (std only).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct CommitFlags(pub u32);

impl CommitFlags {
    /// No flags.
    pub const NONE: Self = Self(0);
    /// Allow a full modeset (`DRM_MODE_ATOMIC_ALLOW_MODESET`).
    ///
    /// Required when changing modes, connecting CRTCs, or otherwise
    /// reconfiguring the display pipeline. Without it only page flips are
    /// permitted.
    pub const ALLOW_MODESET: Self = Self(drm_mode::DRM_MODE_ATOMIC_ALLOW_MODESET);
    /// Non-blocking commit (`DRM_MODE_ATOMIC_NONBLOCK`).
    pub const NONBLOCK: Self = Self(drm_mode::DRM_MODE_ATOMIC_NONBLOCK);
    /// Request a page-flip event on completion (`DRM_MODE_PAGE_FLIP_EVENT`).
    pub const PAGE_FLIP_EVENT: Self = Self(drm_mode::DRM_MODE_PAGE_FLIP_EVENT);
    /// Test only: validate without applying (`DRM_MODE_ATOMIC_TEST_ONLY`).
    ///
    /// Must not mutate hardware state and must not emit events.
    pub const TEST_ONLY: Self = Self(drm_mode::DRM_MODE_ATOMIC_TEST_ONLY);

    /// Empty set.
    #[must_use]
    pub const fn empty() -> Self {
        Self(0)
    }

    /// All known flags.
    #[must_use]
    pub const fn all() -> Self {
        Self(Self::ALLOW_MODESET.0 | Self::NONBLOCK.0 | Self::PAGE_FLIP_EVENT.0 | Self::TEST_ONLY.0)
    }

    /// Raw bits for `drmModeAtomicCommit`.
    #[must_use]
    pub const fn bits(&self) -> u32 {
        self.0
    }

    /// Build from raw bits, masking off unknown bits.
    #[must_use]
    pub const fn from_bits_truncate(bits: u32) -> Self {
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

impl std::ops::BitOr for CommitFlags {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitAnd for CommitFlags {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flag_bits_match_kernel() {
        assert_eq!(CommitFlags::ALLOW_MODESET.bits(), 0x0400);
        assert_eq!(CommitFlags::NONBLOCK.bits(), 0x0200);
        assert_eq!(CommitFlags::TEST_ONLY.bits(), 0x0100);
        assert_eq!(CommitFlags::PAGE_FLIP_EVENT.bits(), 0x01);
    }

    #[test]
    fn flag_operations() {
        let f = CommitFlags::ALLOW_MODESET | CommitFlags::PAGE_FLIP_EVENT;
        assert!(f.contains(CommitFlags::ALLOW_MODESET));
        assert!(f.contains(CommitFlags::PAGE_FLIP_EVENT));
        assert!(!f.contains(CommitFlags::TEST_ONLY));
        assert!(!CommitFlags::empty().contains(CommitFlags::NONBLOCK));
        assert_eq!(
            CommitFlags::from_bits_truncate(u32::MAX).bits(),
            CommitFlags::all().bits()
        );
    }
}
