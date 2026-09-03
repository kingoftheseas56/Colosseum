//! Daemon HTTP client for the ui-gpui shell.
//!
//! The daemon owns the catalog JSON contract (see `crates/daemon/src/main.rs`
//! and `crates/catalog/src/lib.rs`); this module mirrors those shapes as
//! plain deserialize-only DTOs so `ui-gpui` never has to depend on the domain
//! crates. Fetching follows the existing gpui_tokio + reqwest pattern: callers
//! wrap these futures in `gpui_tokio::Tokio::spawn` from the UI side.

use serde::de::DeserializeOwned;
use serde::Deserialize;

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

/// GET a JSON body and deserialize it, mapping transport/parse errors to a
/// short user-facing string.
async fn get_json<T: DeserializeOwned>(url: &str) -> Result<T, String> {
    let body = reqwest::get(url)
        .await
        .map_err(|e| format!("daemon unreachable: {e}"))?
        .text()
        .await
        .map_err(|e| format!("read failed: {e}"))?;
    serde_json::from_str(&body).map_err(|e| format!("bad JSON from daemon: {e}"))
}

/// `GET /catalog/search?q=…` — legacy single-pane catalog list.
pub async fn search(base: &str, q: &str) -> Result<Vec<Series>, String> {
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
