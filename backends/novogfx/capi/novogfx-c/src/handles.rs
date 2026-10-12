//! Device-local weak handle interning. Resource snapshots own strong handles.

use std::collections::HashMap;
use std::rc::{Rc, Weak};

pub(crate) struct HandleCache<T> {
    entries: HashMap<u32, Weak<T>>,
}

impl<T> Default for HandleCache<T> {
    fn default() -> Self {
        Self {
            entries: HashMap::new(),
        }
    }
}

impl<T> HandleCache<T> {
    pub(crate) fn get_or_try_insert(
        &mut self,
        id: u32,
        create: impl FnOnce() -> novogfx::Result<T>,
    ) -> novogfx::Result<Rc<T>> {
        if let Some(handle) = self.entries.get(&id).and_then(Weak::upgrade) {
            return Ok(handle);
        }
        // Prune expired weak entries; the cache never retains a live object.
        self.entries.retain(|_, handle| handle.strong_count() != 0);
        let handle = Rc::new(create()?);
        self.entries.insert(id, Rc::downgrade(&handle));
        Ok(handle)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    struct Tracked(Rc<Cell<u32>>);
    impl Drop for Tracked {
        fn drop(&mut self) {
            self.0.set(self.0.get() + 1);
        }
    }

    #[test]
    fn overlapping_snapshots_share_identity_and_last_owner_releases_handle() {
        let drops = Rc::new(Cell::new(0));
        let mut cache = HandleCache::default();
        let first = cache
            .get_or_try_insert(42, || Ok(Tracked(drops.clone())))
            .unwrap();
        let second = cache
            .get_or_try_insert(42, || panic!("must reuse live handle"))
            .unwrap();
        let weak = Rc::downgrade(&first);
        assert!(Rc::ptr_eq(&first, &second));
        drop(first);
        assert_eq!(drops.get(), 0);
        assert!(weak.upgrade().is_some());
        drop(second);
        assert_eq!(drops.get(), 1);
        assert!(weak.upgrade().is_none());
        let replacement = cache
            .get_or_try_insert(42, || Ok(Tracked(drops.clone())))
            .unwrap();
        drop(replacement);
        assert_eq!(drops.get(), 2);
    }

    #[test]
    fn failed_snapshot_population_releases_prior_handles() {
        let drops = Rc::new(Cell::new(0));
        let mut cache = HandleCache::default();
        let result: novogfx::Result<Vec<Rc<Tracked>>> = (|| {
            let mut snapshot = vec![cache.get_or_try_insert(1, || Ok(Tracked(drops.clone())))?];
            snapshot
                .push(cache.get_or_try_insert(2, || Err(novogfx::Error::NotFound("gone".into())))?);
            Ok(snapshot)
        })();
        assert!(result.is_err());
        assert_eq!(drops.get(), 1);
        assert_eq!(cache.entries[&1].strong_count(), 0);
    }
}
