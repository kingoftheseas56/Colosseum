//! Live (HTTP-backed) add-on providers.
//!
//! The seeded fakes in [`crate::registry`] are sync and offline. Live
//! providers perform network I/O, so they are async-only and are wired in
//! behind the `ADDONS_LIVE=1` flag by the daemon — the sync fake path is
//! untouched (see [`crate::registry::Registry::seeded`]).

pub mod cinemeta;

pub use cinemeta::{Cinemeta, LiveError, MetaPreview};

/// The async seam for the Stremio `catalog` resource.
///
/// [`crate::Addon`] is sync because the seeded fakes are pure in-memory; a
/// network-backed add-on *additionally* implements this trait for its live
/// resources. The daemon awaits it only when `ADDONS_LIVE=1`, so the offline
/// path never touches a runtime.
pub trait CatalogSearch: Send + Sync {
    fn search<'a>(
        &'a self,
        query: &str,
    ) -> impl std::future::Future<Output = Result<Vec<MetaPreview>, LiveError>> + Send + 'a;
}
