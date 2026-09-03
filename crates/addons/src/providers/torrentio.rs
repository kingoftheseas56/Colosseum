//! Live Torrentio provider — the real torrent stream add-on behind the addons
//! crate's traits.
//!
//! Torrentio (`https://torrentio.strem.fun`) is a self-hostable Stremio
//! stream add-on that scrapes public torrent trackers and returns torrent
//! stream candidates: `GET /stream/{type}/{id}.json` → `{ "streams": [...] }`.
//! Each row carries a torrent `infoHash` + `fileIdx`, and its `title` text
//! embeds quality / seeders / size / language exactly as the original QML
//! client (`qml/Torrentio.js`) parses it. That parsing lives in
//! [`crate::rank`] — this module only fetches the raw wire rows and hands them
//! to the same parse/sort pipeline.
//!
//! # Async seam
//!
//! Like [`crate::providers::cinemeta::Cinemeta`], [`Torrentio`] implements the
//! sync [`crate::Addon`] for install identity only (`streams()` empty) and the
//! async [`crate::providers::StreamSearch`] trait for the live fetch. The
//! daemon awaits the latter only when `ADDONS_LIVE=1`.

use crate::manifest::Manifest;
use crate::providers::{LiveError, StreamSearch};
use crate::stream::{Stream, StreamResponse};
use crate::Addon;

/// Torrentio's public origin (no API key, plain HTTPS JSON).
pub const BASE_URL: &str = "https://torrentio.strem.fun";

/// The live Torrentio client. Cheap to clone (`reqwest::Client` is an `Arc`);
/// construction performs no network I/O.
#[derive(Clone, Debug)]
pub struct Torrentio {
    client: reqwest::Client,
    base: String,
    manifest: Manifest,
}

impl Torrentio {
    /// A client against the canonical Torrentio origin.
    pub fn new() -> Self {
        Self::with_base(BASE_URL.to_string())
    }

    /// A client against a custom origin. Public so tests can point the live
    /// path at a dead port and exercise the daemon's fallback behavior
    /// deterministically, without touching the real endpoint.
    pub fn with_base(base: String) -> Self {
        // Bound every request so a slow/blackholed provider degrades to the
        // daemon's fallback instead of hanging the sources route.
        let client = reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(10))
            .build()
            .expect("build torrentio reqwest client");
        Self {
            client,
            base,
            manifest: Manifest {
                // id/name/version from the real manifest at
                // https://torrentio.strem.fun/manifest.json (captured live).
                id: "com.stremio.torrentio.addon".into(),
                version: "0.0.15".into(),
                name: "Torrentio".into(),
                description: Some("Provides torrent streams from scraped torrent providers".into()),
                // The real manifest advertises `stream` over movie/series/anime
                // (plus a top-level `other`) with id prefixes `tt`/`kitsu`.
                // Modelled here as a bare-string resource over the union.
                types: vec![
                    "movie".into(),
                    "series".into(),
                    "anime".into(),
                    "other".into(),
                ],
                resources: vec!["stream".into()],
                id_prefixes: vec!["tt".into(), "kitsu".into()],
            },
        }
    }

    /// Build the `GET /stream/{type}/{id}.json` URL. The id goes into the path
    /// **raw** — colons in series-episode ids (`tt123:1:2`) are preserved, the
    /// same as the JS oracle's string concatenation (no `encodeURIComponent`).
    fn stream_url(&self, media_type: &str, id: &str) -> reqwest::Url {
        let url = format!("{}/stream/{}/{}.json", self.base, media_type, id);
        reqwest::Url::parse(&url).expect("torrentio stream URL is a valid absolute URL")
    }

    async fn fetch_streams(&self, media_type: &str, id: &str) -> Result<Vec<Stream>, LiveError> {
        let response = self
            .client
            .get(self.stream_url(media_type, id))
            .send()
            .await?
            .error_for_status()?;
        let body: StreamResponse = response.json().await?;
        Ok(body.streams)
    }
}

impl Default for Torrentio {
    fn default() -> Self {
        Self::new()
    }
}

impl StreamSearch for Torrentio {
    fn streams<'a>(
        &'a self,
        media_type: &str,
        id: &str,
    ) -> impl std::future::Future<Output = Result<Vec<Stream>, LiveError>> + Send + 'a {
        // Own the args so the returned future is `Send + 'a` (the same seam
        // shape as `CatalogSearch`); the network fetch happens on await.
        let media_type = media_type.to_string();
        let id = id.to_string();
        async move { self.fetch_streams(&media_type, &id).await }
    }
}

impl Addon for Torrentio {
    fn id(&self) -> &str {
        "com.stremio.torrentio.addon"
    }

    fn manifest(&self) -> &Manifest {
        &self.manifest
    }

    /// The sync `Addon` surface is install identity only. The live stream rows
    /// are fetched through [`StreamSearch`]; this never contributes rows to the
    /// sync (offline) aggregation.
    fn streams(&self, _media_type: &str, _id: &str) -> Vec<Stream> {
        Vec::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A trimmed, checked-in capture of the live `GET /stream/movie/tt1160419.json`
    /// response (Dune, 2021). Eight rows spanning the quality/language/fileIdx
    /// spread — public data; no network in tests.
    const FIXTURE: &str = include_str!("../../fixtures/torrentio-live-tt1160419.json");

    #[test]
    fn fixture_parses_as_stremio_streams() {
        let response: StreamResponse = serde_json::from_str(FIXTURE).unwrap();
        assert_eq!(response.streams.len(), 8);

        let top = &response.streams[0];
        assert_eq!(
            top.info_hash.as_deref(),
            Some("799dbc6af33a8f32bf1406dc2ec68bbb6864affb")
        );
        assert_eq!(top.file_idx, Some(0));
        assert!(top.title.as_deref().unwrap().contains("👤 265"));
        assert!(top.title.as_deref().unwrap().contains("💾 20.32 GB"));
    }

    #[test]
    fn fixture_rows_parse_and_rank_through_the_shared_pipeline() {
        let response: StreamResponse = serde_json::from_str(FIXTURE).unwrap();
        let sources = crate::registry::sources_for_addon(
            response.streams,
            "com.stremio.torrentio.addon",
            "Torrentio",
            3,
            Vec::new(),
        );

        // All eight rows parse as torrents (every row has an infoHash).
        assert_eq!(sources.candidates.len(), 8);
        assert!(sources
            .candidates
            .iter()
            .all(|c| c.kind == crate::rank::Kind::Torrent));

        // Quality ladder leads: 4K rows first, then 1080p, 720p, SD. The
        // top row is the 4K HDR with 265 seeders.
        assert_eq!(sources.candidates[0].quality.as_deref(), Some("4K"));
        assert_eq!(
            sources.candidates[0].info_hash.as_deref(),
            Some("799dbc6af33a8f32bf1406dc2ec68bbb6864affb")
        );
        assert_eq!(sources.candidates[0].file_idx, Some(0));

        // The 1080p rows sort below all 4K rows; the SD French row is last.
        let last = sources.candidates.last().unwrap();
        assert_eq!(last.quality.as_deref(), Some("SD"));
        assert_eq!(
            last.info_hash.as_deref(),
            Some("b4ccd9fc48109de596cae807a9dbd3438ccbff8d")
        );

        // A 4K row with a non-zero fileIdx carries it through untouched.
        let idx2 = sources
            .candidates
            .iter()
            .find(|c| c.info_hash.as_deref() == Some("e20fd1395a28d21236e659ab9a7692afed225bfb"))
            .expect("CEBRAY 4K row present");
        assert_eq!(idx2.file_idx, Some(2));

        // The row with no `fileIdx` in the fixture defaults to 0 (JS: undefined → 0).
        let no_idx = sources
            .candidates
            .iter()
            .find(|c| c.info_hash.as_deref() == Some("9bf114d8ff03f4037483a5d55f4869ade267b388"))
            .expect("MULTi VF2 row present");
        assert_eq!(no_idx.file_idx, Some(0));

        // Multi-language rows decode their regional-indicator flags.
        let multi = sources
            .candidates
            .iter()
            .find(|c| c.info_hash.as_deref() == Some("78930639b1a4d37c907966d7b5a6a1c23242fec2"))
            .expect("NAHOM DV row present");
        assert!(multi
            .label
            .starts_with("Dune.2021.REPACK.4K.HDR.DV.2160p.BDRemux"));

        // Seeders and size parse from the title text.
        assert_eq!(
            sources.candidates[0].size_bytes,
            Some((20.32_f64 * 1024.0_f64.powi(3)).round() as u64)
        );
    }

    #[test]
    fn stream_url_keeps_series_episode_colons_raw() {
        let addon = Torrentio::new();
        assert_eq!(
            addon.stream_url("movie", "tt1160419").as_str(),
            "https://torrentio.strem.fun/stream/movie/tt1160419.json"
        );
        // Series episodes: tt:id:ep — colons stay literal in the path, the JS
        // oracle concatenates them without encodeURIComponent.
        assert_eq!(
            addon.stream_url("series", "tt10466872:1:2").as_str(),
            "https://torrentio.strem.fun/stream/series/tt10466872:1:2.json"
        );
    }

    #[test]
    fn manifest_matches_live_addon_identity() {
        let addon = Torrentio::new();
        assert_eq!(addon.id(), "com.stremio.torrentio.addon");
        assert_eq!(addon.manifest().name, "Torrentio");
        assert!(addon.manifest().resources.iter().any(|r| r == "stream"));
        assert!(addon.manifest().id_prefixes.iter().any(|p| p == "tt"));

        assert!(addon.manifest().accepts("stream", "movie", "tt1160419"));
        assert!(addon
            .manifest()
            .accepts("stream", "series", "tt10466872:1:2"));
        assert!(addon.manifest().accepts("stream", "anime", "kitsu:123"));
        assert!(!addon.manifest().accepts("catalog", "movie", "tt1160419"));
        // sync Addon surface contributes no rows
        assert!(Addon::streams(&addon, "movie", "tt1160419").is_empty());
    }
}
