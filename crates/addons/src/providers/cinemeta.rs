//! Live Cinemeta provider — the first real content add-on behind the addons
//! crate's traits.
//!
//! Cinemeta is Stremio's official movie/series catalog add-on
//! (`https://v3-cinemeta.strem.io`), plain HTTP-JSON with no API key. This
//! module delivers the `catalog` resource: search over real IMDb `tt` ids and
//! names. The `stream` resource belongs to a later Torrentio slice and is
//! deliberately not built here.
//!
//! # Async seam
//!
//! The crate's sync [`crate::Addon`] trait cannot express network I/O, so
//! [`Cinemeta`] implements [`crate::Addon`] for the install-identity surface
//! (`id`/`manifest`, with `streams()` empty) and the async
//! [`crate::providers::CatalogSearch`] trait for the live search. The daemon
//! awaits the latter only when `ADDONS_LIVE=1`; the offline fake path never
//! touches a runtime.

use serde::{Deserialize, Serialize};

use crate::manifest::Manifest;
use crate::providers::{CatalogSearch, LiveError};
use crate::stream::Stream;
use crate::Addon;

/// Cinemeta's transport origin (no API key, plain HTTPS JSON).
pub const BASE_URL: &str = "https://v3-cinemeta.strem.io";

/// One catalog meta row as Cinemeta returns it. Catalog responses carry two
/// shapes — a compact `MetaPreview` for the top-ranked rows and a full `Meta`
/// for the tail — which share the fields modelled here. Missing fields default
/// so both shapes deserialize; serialization omits absent fields so the
/// daemon's search rows stay minimal.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MetaPreview {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(rename = "imdb_id", default, skip_serializing_if = "Option::is_none")]
    pub imdb_id: Option<String>,
    #[serde(rename = "type", default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub poster: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub background: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub release_info: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub year: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub runtime: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub imdb_rating: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub genres: Vec<String>,
}

/// The `{ "metas": [...] }` envelope a catalog endpoint returns. Extra fields
/// (`query`, `rank`, `cacheMaxAge`) are ignored.
#[derive(Clone, Debug, Default, Deserialize)]
pub struct CatalogResponse {
    #[serde(default)]
    pub metas: Vec<MetaPreview>,
}

/// The live Cinemeta client. Cheap to clone (`reqwest::Client` is an `Arc`);
/// construction performs no network I/O.
#[derive(Clone, Debug)]
pub struct Cinemeta {
    client: reqwest::Client,
    base: String,
    manifest: Manifest,
}

impl Cinemeta {
    /// A client against the canonical Cinemeta origin.
    pub fn new() -> Self {
        Self::with_base(BASE_URL.to_string())
    }

    /// A client against a custom origin. Public so tests can point the live
    /// path at a dead port and exercise the daemon's fallback behavior
    /// deterministically, without touching the real endpoint.
    pub fn with_base(base: String) -> Self {
        // Bound every request so a slow/blackholed provider degrades to the
        // daemon's fallback instead of hanging the search route.
        let client = reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(10))
            .build()
            .expect("build cinemeta reqwest client");
        Self {
            client,
            base,
            manifest: Manifest {
                id: "com.linvo.cinemeta".into(),
                version: "3.0.14".into(),
                name: "Cinemeta".into(),
                description: Some("The official addon for movie and series catalogs".into()),
                types: vec!["movie".into(), "series".into()],
                resources: vec!["catalog".into(), "meta".into(), "addon_catalog".into()],
                id_prefixes: vec!["tt".into()],
            },
        }
    }

    /// Build the `GET /catalog/{type}/top/search={query}.json` URL. The search
    /// term is a path segment, percent-encoded by `path_segments_mut`.
    fn search_url(&self, media_type: &str, query: &str) -> reqwest::Url {
        let mut url = reqwest::Url::parse(&format!("{}/catalog/{}/top", self.base, media_type))
            .expect("cinemeta base is a valid absolute URL");
        url.path_segments_mut()
            .expect("cinemeta catalog URL has a path")
            .push(&format!("search={query}.json"));
        url
    }

    async fn catalog(&self, media_type: &str, query: &str) -> Result<Vec<MetaPreview>, LiveError> {
        let response = self
            .client
            .get(self.search_url(media_type, query))
            .send()
            .await?
            .error_for_status()?;
        let body: CatalogResponse = response.json().await?;
        Ok(body.metas)
    }
}

impl Default for Cinemeta {
    fn default() -> Self {
        Self::new()
    }
}

impl CatalogSearch for Cinemeta {
    /// Search both movies and series, merged into one list. Movies lead, then
    /// series; a title returned under both types keeps its first (movie) row.
    fn search<'a>(
        &'a self,
        query: &str,
    ) -> impl std::future::Future<Output = Result<Vec<MetaPreview>, LiveError>> + Send + 'a {
        let query = query.to_string();
        async move {
            let movies = self.catalog("movie", &query).await?;
            let series = self.catalog("series", &query).await?;
            Ok(merge(movies, series))
        }
    }
}

impl Addon for Cinemeta {
    fn id(&self) -> &str {
        "com.linvo.cinemeta"
    }

    fn manifest(&self) -> &Manifest {
        &self.manifest
    }

    /// Cinemeta has no `stream` resource (that's Torrentio's resource), so it
    /// contributes no source rows.
    fn streams(&self, _media_type: &str, _id: &str) -> Vec<Stream> {
        Vec::new()
    }
}

/// Merge movie + series catalog results into one list, movies first. Duplicate
/// ids (a title returned under both types) keep the first row; rows without an
/// id pass through.
fn merge(movies: Vec<MetaPreview>, series: Vec<MetaPreview>) -> Vec<MetaPreview> {
    let mut seen = std::collections::HashSet::new();
    let mut out = Vec::with_capacity(movies.len() + series.len());
    for meta in movies.into_iter().chain(series) {
        match meta.id.as_deref().filter(|id| !id.is_empty()) {
            Some(id) if !seen.insert(id.to_string()) => {} // duplicate: keep first
            _ => out.push(meta),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A trimmed, checked-in capture of the live `search=dune` response —
    /// compact and full metas, movie and series. Public catalog data,
    /// attribution-free; no network in tests.
    const FIXTURE: &str = include_str!("../../fixtures/cinemeta-dune.json");

    #[test]
    fn fixture_parses_compact_and_full_meta_shapes() {
        let response: CatalogResponse = serde_json::from_str(FIXTURE).unwrap();
        assert_eq!(response.metas.len(), 5);

        // Compact catalog row (top-ranked): id/name/type/releaseInfo, no
        // description or genres.
        let dune = &response.metas[0];
        assert_eq!(dune.id.as_deref(), Some("tt1160419"));
        assert_eq!(dune.imdb_id.as_deref(), Some("tt1160419"));
        assert_eq!(dune.media_type.as_deref(), Some("movie"));
        assert_eq!(dune.name.as_deref(), Some("Dune: Part One"));
        assert_eq!(dune.release_info.as_deref(), Some("2021"));
        assert!(dune.description.is_none());

        // Full meta row (tail): description, year, runtime, genres.
        let full = &response.metas[2];
        assert_eq!(full.name.as_deref(), Some("The Dune"));
        assert_eq!(full.year.as_deref(), Some("2013"));
        assert_eq!(full.runtime.as_deref(), Some("86 min"));
        assert_eq!(full.imdb_rating.as_deref(), Some("6.8"));
        assert_eq!(full.genres, vec!["Drama".to_string()]);

        // Series rows keep their type and an open-ended releaseInfo.
        let series = &response.metas[3];
        assert_eq!(series.media_type.as_deref(), Some("series"));
        assert_eq!(series.release_info.as_deref(), Some("2024-"));
    }

    #[test]
    fn merge_dedups_by_id_keeping_first() {
        let response: CatalogResponse = serde_json::from_str(FIXTURE).unwrap();
        let movies = vec![response.metas[0].clone(), response.metas[1].clone()];
        // The series list re-returns tt1160419 (movie wins) plus a series row.
        let series = vec![response.metas[3].clone(), response.metas[0].clone()];

        let merged = merge(movies, series);
        let ids: Vec<&str> = merged.iter().filter_map(|m| m.id.as_deref()).collect();
        assert_eq!(ids, vec!["tt1160419", "tt0087182", "tt10466872"]);
    }

    #[test]
    fn manifest_matches_live_addon_identity() {
        let addon = Cinemeta::new();
        assert_eq!(addon.id(), "com.linvo.cinemeta");
        assert_eq!(addon.manifest().name, "Cinemeta");
        assert!(addon.manifest().resources.iter().any(|r| r == "catalog"));
        assert!(addon.manifest().id_prefixes.iter().any(|p| p == "tt"));

        // Catalog is accepted for movie/series with a tt id; stream is not
        // (that's the later Torrentio slice), so no source rows.
        assert!(addon.manifest().accepts("catalog", "movie", "tt1160419"));
        assert!(addon.manifest().accepts("catalog", "series", "tt10466872"));
        assert!(!addon.manifest().accepts("stream", "movie", "tt1160419"));
        assert!(addon.streams("movie", "tt1160419").is_empty());
    }

    #[test]
    fn search_url_puts_query_in_the_path_segment() {
        let addon = Cinemeta::new();
        assert_eq!(
            addon.search_url("movie", "dune").as_str(),
            "https://v3-cinemeta.strem.io/catalog/movie/top/search=dune.json"
        );
        // Spaces and other path-unsafe characters are percent-encoded, while
        // the protocol's `=` stays literal.
        assert_eq!(
            addon.search_url("series", "dune part two").as_str(),
            "https://v3-cinemeta.strem.io/catalog/series/top/search=dune%20part%20two.json"
        );
    }
}
