//! Live (HTTP-backed) add-on providers.
//!
//! The seeded fakes in [`crate::registry`] are sync and offline. Live
//! providers perform network I/O, so they are async-only and are wired in
//! behind the `ADDONS_LIVE=1` flag by the daemon — the sync fake path is
//! untouched (see [`crate::registry::Registry::seeded`]).

pub mod cinemeta;
pub mod torrentio;

pub use cinemeta::{Cinemeta, MetaPreview, MetaResponse};
pub use torrentio::Torrentio;

/// Errors from a live provider. The daemon treats any of these as "provider
/// down" and degrades to its fallback behavior (`ADDONS_LIVE=1` never
/// hard-fails a route on a down provider).
#[derive(Debug, thiserror::Error)]
pub enum LiveError {
    #[error("addon request failed: {0}")]
    Request(#[from] reqwest::Error),
    #[error("addon returned malformed JSON: {0}")]
    Malformed(#[from] serde_json::Error),
}

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

/// The async seam for the Stremio `stream` resource — the live counterpart to
/// the sync [`crate::Addon::streams`]. Torrentio implements this to fetch the
/// real `{ "streams": [...] }` body over HTTP; the rows are then handed to the
/// same [`crate::rank`] parse/sort pipeline as the seeded fakes.
pub trait StreamSearch: Send + Sync {
    fn streams<'a>(
        &'a self,
        media_type: &str,
        id: &str,
    ) -> impl std::future::Future<Output = Result<Vec<crate::stream::Stream>, LiveError>> + Send + 'a;
}
