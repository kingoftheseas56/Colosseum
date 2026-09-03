//! Daemon HTTP client for the ui-gpui shell.
//!
//! The daemon owns the catalog JSON contract (see `crates/daemon/src/main.rs`
//! and `crates/catalog/src/lib.rs`); this module mirrors those shapes as
//! plain deserialize-only DTOs so `ui-gpui` never has to depend on the domain
//! crates. Fetching follows the existing gpui_tokio + reqwest pattern: callers
//! wrap these futures in `gpui_tokio::Tokio::spawn` from the UI side.

use serde::de::DeserializeOwned;
use serde::Deserialize;
use std::collections::BTreeMap;

/// A full `series` row, as served by `/catalog/search` and both `/catalog/home`
/// rails. Watch fields are carried so the continue-watching rail can show
/// progress later; they are not all read by the current shell.
#[derive(Clone, Deserialize)]
pub struct Series {
    pub id: i64,
    pub title: String,
    #[allow(dead_code)]
    pub source: String,
    #[allow(dead_code)]
    pub description: String,
    pub poster_color: String,
    #[allow(dead_code)]
    pub added_at: String,
    #[allow(dead_code)]
    pub last_watched_at: Option<String>,
    #[allow(dead_code)]
    pub watch_position_secs: i64,
    #[allow(dead_code)]
    pub duration_secs: i64,
    #[allow(dead_code)]
    pub episode_count: Option<i64>,
}

/// The series-detail projection served by `/catalog/series/{id}`.
#[derive(Clone, Deserialize)]
pub struct SeriesDetail {
    pub id: i64,
    pub title: String,
    #[allow(dead_code)]
    pub source: String,
    pub description: String,
    pub poster_color: String,
    #[allow(dead_code)]
    pub added_at: String,
    #[allow(dead_code)]
    pub episode_count: Option<i64>,
}

/// The home aggregate served by `/catalog/home`.
#[derive(Clone, Deserialize)]
pub struct Home {
    pub continue_watching: Vec<Series>,
    pub trending: Vec<Series>,
}

/// A live Cinemeta meta row — the wire shape served by `/catalog/search` with
/// `ADDONS_LIVE=1` and by the `/catalog/meta/{type}/{tt_id}` detail route.
/// Mirrors the addons crate's `MetaPreview` (which models both the compact
/// catalog row and the full meta object; the meta route always returns the
/// complete field set). `type` is `media_type` on the Rust side because `type`
/// is reserved.
#[derive(Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CinemetaMeta {
    #[serde(default)]
    pub id: Option<String>,
    #[serde(rename = "type", default)]
    pub media_type: Option<String>,
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    #[allow(dead_code)]
    pub poster: Option<String>,
    #[serde(default)]
    #[allow(dead_code)]
    pub background: Option<String>,
    #[serde(default)]
    pub release_info: Option<String>,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub year: Option<String>,
    #[serde(default)]
    pub runtime: Option<String>,
    #[serde(default)]
    pub imdb_rating: Option<String>,
    #[serde(default)]
    pub genres: Vec<String>,
}

impl CinemetaMeta {
    /// The display title (the live wire's `name`), or a stable fallback.
    pub fn title(&self) -> String {
        self.name
            .clone()
            .filter(|n| !n.is_empty())
            .unwrap_or_else(|| self.id.clone().unwrap_or_else(|| "unknown".into()))
    }

    /// A one-line subtitle for a search row / hero: `Movie · 2021`, `Series ·
    /// 2024-`, with the IMDb rating appended when present.
    pub fn subtitle(&self) -> String {
        let kind = self.media_type.as_deref().unwrap_or("movie");
        let kind = match kind {
            "series" => "Series",
            "movie" => "Movie",
            other => other,
        };
        let mut out = kind.to_string();
        if let Some(year) = self.release_info.as_deref().or(self.year.as_deref()) {
            if !year.is_empty() {
                out.push_str(&format!(" · {year}"));
            }
        }
        if let Some(rating) = self.imdb_rating.as_deref() {
            if !rating.is_empty() {
                out.push_str(&format!(" · ⭐ {rating}"));
            }
        }
        out
    }
}

/// One row in the `/catalog/search` response. The daemon serves *either* a
/// live Cinemeta array (`ADDONS_LIVE=1`) or the seeded offline `Series` array
/// (no flag), and the two shapes share no field types (`id` is a `tt` string
/// vs an i64). An untagged enum picks the shape by trying the live shape first;
/// a seeded row's numeric `id` fails `CinemetaMeta`'s `Option<String>` and
/// falls through to `Series`, while a live row's string `id` fails `Series`.
#[derive(Clone, Deserialize)]
#[serde(untagged)]
pub enum SearchRow {
    Live(CinemetaMeta),
    Seeded(Series),
}

impl SearchRow {
    /// The OCR-able title for a result row.
    pub fn title(&self) -> String {
        match self {
            SearchRow::Live(meta) => meta.title(),
            SearchRow::Seeded(series) => series.title.clone(),
        }
    }

    /// A kind/type label so rows can be told apart (`Movie · 2021`,
    /// `Demo series`).
    pub fn subtitle(&self) -> String {
        match self {
            SearchRow::Live(meta) => meta.subtitle(),
            SearchRow::Seeded(_) => "Demo series".into(),
        }
    }

    /// Whether this is a live Cinemeta row (open detail-by-imdb) vs a seeded
    /// row (open the existing series detail).
    #[allow(dead_code)]
    pub fn is_live(&self) -> bool {
        matches!(self, SearchRow::Live(_))
    }
}

/// One source candidate in a `/sources` response. Torrent rows carry
/// `info_hash` + `file_idx`; direct rows carry `url`. `id` is the
/// `t:<infoHash>:<fileIdx>` / `u:<url>` row key used as the spool candidate id.
#[derive(Clone, Deserialize)]
pub struct Candidate {
    pub id: String,
    pub addon: String,
    pub kind: String,
    pub label: String,
    #[serde(default)]
    pub quality: Option<String>,
    #[serde(default)]
    #[allow(dead_code)]
    pub url: Option<String>,
    #[serde(default)]
    #[allow(dead_code)]
    pub info_hash: Option<String>,
    #[serde(default)]
    #[allow(dead_code)]
    pub file_idx: Option<u64>,
    #[serde(default)]
    pub size_bytes: Option<u64>,
}

impl Candidate {
    /// Torrent-kind candidates are the only ones the spool route accepts.
    pub fn is_torrent(&self) -> bool {
        self.kind == "torrent"
    }
}

/// One installed add-on descriptor from a `/sources` response.
#[derive(Clone, Deserialize)]
pub struct InstalledAddon {
    #[allow(dead_code)]
    pub id: String,
    pub name: String,
    #[allow(dead_code)]
    pub priority: usize,
}

/// The `/sources/imdb/{tt}` (and `/catalog/series/{id}/sources`) response.
#[derive(Clone, Deserialize)]
pub struct Sources {
    pub candidates: Vec<Candidate>,
    pub counts_by_addon: BTreeMap<String, usize>,
    pub installed_addons: Vec<InstalledAddon>,
}

/// The `POST /torrents/spool` success body: a completed, piece-verified
/// `file://` path for the player.
#[derive(Clone, Deserialize)]
pub struct SpoolResponse {
    #[allow(dead_code)]
    pub torrent_id: usize,
    #[allow(dead_code)]
    pub info_hash: String,
    #[allow(dead_code)]
    pub file_idx: u64,
    #[allow(dead_code)]
    pub status: String,
    pub path: String,
}

/// Extract the Go error envelope's message if a body is one. Used so a 404/502
/// from the daemon surfaces as a short user-facing string instead of a serde
/// "missing field" error.
fn envelope_message(body: &str) -> Option<String> {
    let value: serde_json::Value = serde_json::from_str(body).ok()?;
    value
        .get("error")?
        .get("message")?
        .as_str()
        .map(str::to_string)
}

/// GET a JSON body and deserialize it, mapping transport/parse errors and the
/// daemon's error envelope to a short user-facing string.
async fn get_json<T: DeserializeOwned>(url: &str) -> Result<T, String> {
    let response = reqwest::get(url)
        .await
        .map_err(|e| format!("daemon unreachable: {e}"))?;
    let status = response.status();
    let body = response
        .text()
        .await
        .map_err(|e| format!("read failed: {e}"))?;
    if !status.is_success() {
        return Err(envelope_message(&body).unwrap_or_else(|| format!("daemon returned {status}")));
    }
    serde_json::from_str(&body).map_err(|e| format!("bad JSON from daemon: {e}"))
}

/// `GET /catalog/search?q=…` — legacy single-pane catalog list (offline
/// `Series` shape; the legacy view predates the live search).
pub async fn search(base: &str, q: &str) -> Result<Vec<Series>, String> {
    get_json(&format!("{base}/catalog/search?q={q}")).await
}

/// `GET /catalog/search?q=…` — the search view's fetch, which accepts either
/// the live Cinemeta array or the offline seeded array.
pub async fn search_rows(base: &str, q: &str) -> Result<Vec<SearchRow>, String> {
    get_json(&format!("{base}/catalog/search?q={q}")).await
}

/// `GET /catalog/home` — the home rails aggregate.
pub async fn home(base: &str) -> Result<Home, String> {
    get_json(&format!("{base}/catalog/home")).await
}

/// `GET /catalog/series/{id}` — one series detail.
pub async fn series(base: &str, id: i64) -> Result<SeriesDetail, String> {
    get_json(&format!("{base}/catalog/series/{id}")).await
}

/// `GET /catalog/meta/{type}/{tt_id}` — the live Cinemeta detail for one id.
/// The daemon returns the bare meta object (not a `{ "meta": ... }` envelope),
/// matching its `/catalog/search` array-of-metas wire shape.
pub async fn meta(base: &str, media_type: &str, tt_id: &str) -> Result<CinemetaMeta, String> {
    get_json(&format!("{base}/catalog/meta/{media_type}/{tt_id}")).await
}

/// `GET /sources/imdb/{tt_id}` — ranked live torrent candidates for one id.
pub async fn sources_imdb(base: &str, tt_id: &str) -> Result<Sources, String> {
    get_json(&format!("{base}/sources/imdb/{tt_id}")).await
}

/// `POST /torrents/spool` — spool one torrent candidate and return the
/// completed `file://` path. No client timeout here: the daemon polls
/// internally for up to its own bound, so the request may legitimately run for
/// minutes; a real-swarm stall surfaces as the daemon's `stalled` envelope.
pub async fn spool(base: &str, candidate_id: &str) -> Result<SpoolResponse, String> {
    let response = reqwest::Client::new()
        .post(format!("{base}/torrents/spool"))
        .json(&serde_json::json!({ "candidate_id": candidate_id }))
        .send()
        .await
        .map_err(|e| format!("daemon unreachable: {e}"))?;
    let status = response.status();
    let body = response
        .text()
        .await
        .map_err(|e| format!("read failed: {e}"))?;
    if !status.is_success() {
        return Err(envelope_message(&body).unwrap_or_else(|| format!("daemon returned {status}")));
    }
    serde_json::from_str(&body).map_err(|e| format!("bad JSON from daemon: {e}"))
}
