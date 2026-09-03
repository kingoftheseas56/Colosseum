//! Daemon — the composition root. Owns HTTP, path discovery, and wiring.
//! Domain logic lives in the `catalog` and `account` crates; this binary only
//! maps their contracts onto routes.
//!
//! # Catalog endpoints (Phase A browse spine)
//!
//! All catalog routes read the local `catalog` store; there are no provider
//! integrations yet (jikan/kitsu/anilist/metahub are a later daemon phase).
//!
//! ## GET /catalog/home
//!
//! Aggregate for the home screen. 200 `application/json`:
//!
//! ```json
//! {
//!   "continue_watching": [ { "id": 2, "title": "Demo Series Beta",
//!     "source": "demo", "description": "…", "poster_color": "#64b5f6",
//!     "added_at": "2026-01-02T00:00:00Z",
//!     "last_watched_at": "2026-03-06T20:15:00Z",
//!     "watch_position_secs": 720, "duration_secs": 1440,
//!     "episode_count": 24 } ],
//!   "trending": [ … newest-added first … ]
//! }
//! ```
//!
//! `continue_watching` holds every series with `watch_position_secs > 0`,
//! most-recently-watched first; `trending` is the whole catalog ordered by
//! `added_at` descending. Both are deterministic given the demo seed.
//!
//! ## GET /catalog/series/{id}
//!
//! Detail projection for one series. 200 `application/json`:
//!
//! ```json
//! { "id": 7, "title": "Starlight Academy", "source": "demo",
//!   "description": "Rival clubs chase the winter constellation cup.",
//!   "poster_color": "#f06292", "added_at": "2026-02-01T00:00:00Z",
//!   "episode_count": 12 }
//! ```
//!
//! Unknown ids return 404 with the Go error envelope
//! `{"error":{"code":"not_found","message":"No series with that id."}}`.
//!
//! ## GET /catalog/search (live provider flag `ADDONS_LIVE`)
//!
//! Without the flag this route is byte-for-byte the offline seed search: a
//! JSON array of `catalog::Series` rows. With `ADDONS_LIVE=1`, the route
//! consults the live Cinemeta catalog add-on (`https://v3-cinemeta.strem.io`)
//! instead: it searches both the `movie` and `series` catalogs and returns
//! real rows tagged with IMDb `tt` ids and names. The response is still a
//! JSON array, but each row is the addons crate's `MetaPreview` shape
//! (`type`, `id`, `imdb_id`, `name`, `poster`, `releaseInfo`, …), e.g.
//!
//! ```json
//! [ { "type": "movie", "id": "tt1160419", "imdb_id": "tt1160419",
//!     "name": "Dune: Part One", "poster": "https://…",
//!     "releaseInfo": "2021" } ]
//! ```
//!
//! If Cinemeta is unreachable, times out, or errors, the route logs the error
//! and falls back to the local seed (the offline shape) so search never
//! hard-fails on a down provider.
//!
//! ## GET /sources/imdb/{tt_id} (live provider flag `ADDONS_LIVE`)
//!
//! Live torrent sources for one IMDb id — the Torrentio half of the
//! real-torrent bridge. This route is live-only: without `ADDONS_LIVE=1` it
//! returns 404 with the Go error envelope
//! `{"error":{"code":"not_found","message":"Live sources are disabled; run with ADDONS_LIVE=1."}}`.
//! The offline `/catalog/series/{id}/sources` seeded route is unchanged and
//! never consults this path.
//!
//! With the flag set, `{tt_id}` maps to a media type and is fetched raw:
//! a bare `tt1160419` is a movie (`GET /stream/movie/tt1160419.json`); a
//! series episode `tt10466872:1:2` is a series
//! (`GET /stream/series/tt10466872:1:2.json`) — the id goes into the URL path
//! raw, colons preserved, matching the QML `Torrentio.js` oracle. The response
//! is the same [`addons::Sources`] wire shape as
//! `/catalog/series/{id}/sources` (candidates ranked quality → seeders →
//! release → language, `counts_by_addon`, `installed_addons`), with
//! `installed_addons` taken from the live registry. If Torrentio is
//! unreachable, times out, or errors, the route returns 502 with the envelope
//! `{"error":{"code":"provider_unavailable","message":"Torrentio stream lookup failed."}}`.

mod paths;
mod sidecar;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use axum::extract::{Path, Query, State};
use axum::http::header;
use axum::http::{HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::{Deserialize, Serialize};
use tracing_subscriber::EnvFilter;

use account::{CreateAccountRequest, RefreshRequest, Service as AccountService, SignInRequest};
use addons::{sources_for_addon, CatalogSearch, Cinemeta, Registry, StreamSearch, Torrentio};
use catalog::Catalog;
use sidecar::{parse_candidate_id, spool, SidecarSupervisor, SpoolError};

struct AppState {
    catalog: Arc<Catalog>,
    accounts: Arc<AccountService>,
    addons: Arc<Registry>,
    /// The live Cinemeta catalog client, present only when `ADDONS_LIVE=1`.
    live: Option<Arc<Cinemeta>>,
    /// The live Torrentio stream client, present only when `ADDONS_LIVE=1`.
    live_torrentio: Option<Arc<Torrentio>>,
    /// The live registry (seeded fakes + Cinemeta + Torrentio), present only
    /// when `ADDONS_LIVE=1`. Supplies `installed_addons` for the live sources
    /// route without constructing the registry per request.
    live_addons: Option<Arc<Registry>>,
    /// The torrent sidecar supervisor (lazy spawn on first torrent spool).
    sidecar: tokio::sync::Mutex<SidecarSupervisor>,
    ready: AtomicBool,
}

#[derive(Deserialize)]
struct SearchQuery {
    q: String,
}

/// Wire error envelope, mirroring `WriteAPIError` in
/// server/account-service/internal/httpserver/api.go.
#[derive(Serialize)]
struct APIError {
    error: APIErrorDetail,
}

#[derive(Serialize)]
struct APIErrorDetail {
    code: String,
    message: String,
}

fn write_api_error(status: StatusCode, code: &str, message: &str) -> Response {
    let mut response = Json(APIError {
        error: APIErrorDetail {
            code: code.to_owned(),
            message: message.to_owned(),
        },
    })
    .into_response();

    *response.status_mut() = status;
    let headers = response.headers_mut();
    headers.insert(header::CACHE_CONTROL, HeaderValue::from_static("no-store"));
    headers.insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static("application/json; charset=utf-8"),
    );
    headers.insert(
        header::X_CONTENT_TYPE_OPTIONS,
        HeaderValue::from_static("nosniff"),
    );

    response
}

/// Map `account::Error` to an HTTP status, mirroring the `writeServiceError`
/// switch in server/account-service/internal/httpserver/api.go.
fn account_status(err: &account::Error) -> StatusCode {
    match err {
        account::Error::InvalidUsername => StatusCode::BAD_REQUEST,
        account::Error::UsernameUnavailable => StatusCode::CONFLICT,
        account::Error::InvalidPassword => StatusCode::BAD_REQUEST,
        account::Error::InvalidCredentials => StatusCode::UNAUTHORIZED,
        account::Error::SessionInvalid => StatusCode::UNAUTHORIZED,
        account::Error::InvalidDevice => StatusCode::BAD_REQUEST,
    }
}

fn account_response(result: Result<account::Session, account::Error>) -> Response {
    match result {
        Ok(session) => (StatusCode::OK, Json(session)).into_response(),
        Err(err) => write_api_error(account_status(&err), err.code(), &err.to_string()),
    }
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::try_from_default_env().unwrap_or_else(|_| "daemon=info".into()))
        .init();

    let data_dir = paths::data_dir();
    std::fs::create_dir_all(&data_dir).expect("create data dir");
    tracing::info!(?data_dir, "data dir");

    let state = Arc::new(AppState {
        catalog: Arc::new(Catalog::open(&data_dir.join("catalog.db")).expect("open catalog")),
        accounts: Arc::new(AccountService::in_memory()),
        addons: Arc::new(Registry::seeded()),
        live: live_cinemeta().map(Arc::new),
        live_torrentio: live_torrentio().map(Arc::new),
        live_addons: live_addons().map(Arc::new),
        sidecar: tokio::sync::Mutex::new(SidecarSupervisor::new(data_dir.join("torrent-cache"))),
        ready: AtomicBool::new(false),
    });
    state.catalog.seed_demo();
    // Ready only once the catalog is open and seeded.
    state.ready.store(true, Ordering::SeqCst);

    let app = Router::new()
        .route("/healthz", get(healthz))
        .route("/readyz", get(readyz))
        .route("/catalog/search", get(search_catalog))
        .route("/catalog/home", get(catalog_home))
        .route("/catalog/series/{id}", get(catalog_series))
        .route("/catalog/series/{id}/sources", get(catalog_sources))
        .route("/catalog/meta/{media_type}/{tt_id}", get(catalog_meta))
        .route("/sources/imdb/{tt_id}", get(sources_imdb))
        .route("/torrents/spool", post(spool_torrent))
        .route("/v1/accounts", post(create_account))
        .route("/v1/sessions", post(sign_in))
        .route("/v1/sessions/refresh", post(refresh_session))
        .with_state(state.clone());

    let port: u16 = std::env::var("DAEMON_PORT")
        .ok()
        .and_then(|p| p.parse().ok())
        .unwrap_or(0);
    let listener = tokio::net::TcpListener::bind(("127.0.0.1", port))
        .await
        .expect("bind");
    tracing::info!("listening on http://{}", listener.local_addr().unwrap());
    axum::serve(listener, app)
        .with_graceful_shutdown(async {
            let _ = tokio::signal::ctrl_c().await;
            tracing::info!("ctrl-c received, shutting down");
        })
        .await
        .expect("serve");

    // The daemon owns the sidecar's lifecycle: kill it on shutdown.
    state.sidecar.lock().await.shutdown().await;
}

async fn healthz() -> &'static str {
    "ok"
}

async fn readyz(State(state): State<Arc<AppState>>) -> Response {
    if state.ready.load(Ordering::SeqCst) {
        (StatusCode::OK, "ok").into_response()
    } else {
        write_api_error(
            StatusCode::SERVICE_UNAVAILABLE,
            "not_ready",
            "The service is not ready.",
        )
    }
}

/// `ADDONS_LIVE=1` enables the live Cinemeta path; anything else (unset,
/// `0`, `true`, …) is off. Kept as a pure function so the flag decision is
/// testable without touching the process environment.
fn live_enabled(value: Option<&str>) -> bool {
    matches!(value, Some("1"))
}

/// The live Cinemeta client for catalog search, present only when
/// `ADDONS_LIVE=1` is set.
fn live_cinemeta() -> Option<Cinemeta> {
    if live_enabled(std::env::var("ADDONS_LIVE").ok().as_deref()) {
        tracing::info!("ADDONS_LIVE=1: live Cinemeta catalog search enabled");
        Some(Cinemeta::new())
    } else {
        None
    }
}

/// The live Torrentio client for the `tt`-keyed sources route, present only
/// when `ADDONS_LIVE=1` is set.
fn live_torrentio() -> Option<Torrentio> {
    if live_enabled(std::env::var("ADDONS_LIVE").ok().as_deref()) {
        Some(Torrentio::new())
    } else {
        None
    }
}

/// The live registry (seeded fakes + Cinemeta + Torrentio), present only when
/// `ADDONS_LIVE=1` is set. Its `installed()` is the `installed_addons` list
/// for the live sources route.
fn live_addons() -> Option<Registry> {
    if live_enabled(std::env::var("ADDONS_LIVE").ok().as_deref()) {
        Some(Registry::live())
    } else {
        None
    }
}

async fn search_catalog(
    State(state): State<Arc<AppState>>,
    Query(query): Query<SearchQuery>,
) -> Response {
    match &state.live {
        Some(live) => match live.search(&query.q).await {
            Ok(metas) => (StatusCode::OK, Json(metas)).into_response(),
            Err(err) => {
                tracing::warn!(error = %err, "live Cinemeta search failed; falling back to local seed");
                Json(state.catalog.search(&query.q)).into_response()
            }
        },
        None => Json(state.catalog.search(&query.q)).into_response(),
    }
}

async fn catalog_home(State(state): State<Arc<AppState>>) -> Json<catalog::Home> {
    Json(state.catalog.home())
}

#[derive(Deserialize)]
struct SeriesPath {
    id: i64,
}

async fn catalog_series(
    State(state): State<Arc<AppState>>,
    Path(path): Path<SeriesPath>,
) -> Response {
    match state.catalog.series(path.id) {
        Some(detail) => (StatusCode::OK, Json(detail)).into_response(),
        None => write_api_error(
            StatusCode::NOT_FOUND,
            "not_found",
            "No series with that id.",
        ),
    }
}

/// `GET /catalog/series/{id}/sources` — the seeded add-on sources for one
/// series, candidates sorted per the addons crate's ranking (quality → seeders
/// → release → language → install priority). Unknown ids 404 with the same
/// envelope as the detail route.
async fn catalog_sources(
    State(state): State<Arc<AppState>>,
    Path(path): Path<SeriesPath>,
) -> Response {
    if state.catalog.series(path.id).is_none() {
        return write_api_error(
            StatusCode::NOT_FOUND,
            "not_found",
            "No series with that id.",
        );
    }
    let sources = state.addons.sources("series", &path.id.to_string());
    (StatusCode::OK, Json(sources)).into_response()
}

#[derive(Deserialize)]
struct MetaPath {
    media_type: String,
    tt_id: String,
}

/// `GET /catalog/meta/{media_type}/{tt_id}` — the live Cinemeta meta (detail)
/// for one id, the detail-by-imdb half of the torrent lane. This mirrors the
/// `/sources/imdb` live gate exactly: without `ADDONS_LIVE=1` it returns 404
/// with the Go envelope, and a down/timeout provider returns 502. The seeded
/// `/catalog/series/{id}` detail route is untouched.
async fn catalog_meta(State(state): State<Arc<AppState>>, Path(path): Path<MetaPath>) -> Response {
    let Some(live) = &state.live else {
        return write_api_error(
            StatusCode::NOT_FOUND,
            "not_found",
            "Live metadata is disabled; run with ADDONS_LIVE=1.",
        );
    };

    match live.meta(&path.media_type, &path.tt_id).await {
        Ok(meta) => (StatusCode::OK, Json(meta)).into_response(),
        Err(err) => {
            tracing::warn!(
                error = %err,
                tt_id = %path.tt_id,
                media_type = %path.media_type,
                "live Cinemeta meta failed"
            );
            write_api_error(
                StatusCode::BAD_GATEWAY,
                "provider_unavailable",
                "Cinemeta metadata lookup failed.",
            )
        }
    }
}

#[derive(Deserialize)]
struct ImdbPath {
    tt_id: String,
}

/// Map a `tt` id to the Torrentio media type: a bare `tt1160419` is a movie,
/// a series episode `tt10466872:1:2` is a series (the id's colon convention,
/// matching the QML `Torrentio.js` oracle).
fn media_type_for_tt_id(tt_id: &str) -> &str {
    if tt_id.contains(':') {
        "series"
    } else {
        "movie"
    }
}

/// `GET /sources/imdb/{tt_id}` — live torrent candidates for one IMDb id,
/// answering the Cinemeta catalog bridge with real Torrentio streams. Live-only
/// (`ADDONS_LIVE=1`); offline it 404s with the Go envelope. The response is the
/// same [`addons::Sources`] wire shape as `/catalog/series/{id}/sources`.
async fn sources_imdb(State(state): State<Arc<AppState>>, Path(path): Path<ImdbPath>) -> Response {
    let (Some(torrentio), Some(live_addons)) = (&state.live_torrentio, &state.live_addons) else {
        return write_api_error(
            StatusCode::NOT_FOUND,
            "not_found",
            "Live sources are disabled; run with ADDONS_LIVE=1.",
        );
    };

    let media_type = media_type_for_tt_id(&path.tt_id);
    let installed = live_addons.installed();
    // The live Torrentio's install priority in the live registry (the seeded
    // fake also claims the Torrentio id at priority 0, so take the last one).
    let priority = installed
        .iter()
        .filter(|a| a.id == "com.stremio.torrentio.addon")
        .map(|a| a.priority)
        .max()
        .unwrap_or(0);

    match torrentio.streams(media_type, &path.tt_id).await {
        Ok(streams) => {
            let sources = sources_for_addon(
                streams,
                "com.stremio.torrentio.addon",
                "Torrentio",
                priority,
                installed,
            );
            (StatusCode::OK, Json(sources)).into_response()
        }
        Err(err) => {
            tracing::warn!(error = %err, tt_id = %path.tt_id, media_type, "live Torrentio sources failed");
            write_api_error(
                StatusCode::BAD_GATEWAY,
                "provider_unavailable",
                "Torrentio stream lookup failed.",
            )
        }
    }
}

/// `POST /torrents/spool` body: the torrent-kind candidate's wire-model row id
/// (`t:<infoHash>:<fileIdx>`), which is the addons crate's `_rowKey` and the
/// natural client key for a ranked candidate.
#[derive(Deserialize)]
struct SpoolBody {
    candidate_id: String,
}

/// Spool poll cadence (the app's own stats cadence, ~1 Hz) and the daemon-side
/// watchdog bound (`stalled`).
const SPOOL_POLL: Duration = Duration::from_secs(1);
const SPOOL_TIMEOUT: Duration = Duration::from_secs(15 * 60);

/// `POST /torrents/spool` — lazily start the sidecar, add one torrent-kind
/// candidate, poll until its file is piece-verified complete, and return the
/// completed `file://` path for the UI layer (the daemon does not own the
/// player).
async fn spool_torrent(
    State(state): State<Arc<AppState>>,
    Json(body): Json<SpoolBody>,
) -> Response {
    let request = match parse_candidate_id(&body.candidate_id) {
        Ok(request) => request,
        Err(err) => return spool_error_response(&err),
    };

    let client = {
        let mut supervisor = state.sidecar.lock().await;
        match supervisor.ensure_running().await {
            Ok(client) => client,
            Err(err) => return spool_error_response(&err),
        }
    };

    match spool(
        &client,
        &request.info_hash,
        request.file_idx,
        SPOOL_POLL,
        SPOOL_TIMEOUT,
    )
    .await
    {
        Ok(response) => (StatusCode::OK, Json(response)).into_response(),
        Err(SpoolError::SidecarUnavailable(_)) => {
            // Restart-once supervision: kill, respawn, retry the add once.
            let mut supervisor = state.sidecar.lock().await;
            if supervisor.restarted() {
                return spool_error_response(&SpoolError::SidecarUnavailable(
                    "sidecar unavailable after restart".into(),
                ));
            }
            supervisor.mark_restarted();
            let retry = supervisor.restart().await;
            drop(supervisor);
            match retry {
                Ok(client) => {
                    match spool(
                        &client,
                        &request.info_hash,
                        request.file_idx,
                        SPOOL_POLL,
                        SPOOL_TIMEOUT,
                    )
                    .await
                    {
                        Ok(response) => (StatusCode::OK, Json(response)).into_response(),
                        Err(err) => spool_error_response(&err),
                    }
                }
                Err(err) => spool_error_response(&err),
            }
        }
        Err(err) => spool_error_response(&err),
    }
}

fn spool_error_response(err: &SpoolError) -> Response {
    write_api_error(err.status(), err.code(), &err.to_string())
}

async fn create_account(
    State(state): State<Arc<AppState>>,
    Json(request): Json<CreateAccountRequest>,
) -> Response {
    account_response(state.accounts.create_account(request))
}

async fn sign_in(
    State(state): State<Arc<AppState>>,
    Json(request): Json<SignInRequest>,
) -> Response {
    account_response(state.accounts.sign_in(request))
}

async fn refresh_session(
    State(state): State<Arc<AppState>>,
    Json(request): Json<RefreshRequest>,
) -> Response {
    account_response(state.accounts.refresh(request))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_state() -> Arc<AppState> {
        let catalog = Catalog::open_in_memory().expect("in-memory catalog");
        catalog.seed_demo();
        let tmp = tempfile::tempdir().expect("temp dir");
        Arc::new(AppState {
            catalog: Arc::new(catalog),
            accounts: Arc::new(AccountService::in_memory()),
            addons: Arc::new(Registry::seeded()),
            live: None,
            live_torrentio: None,
            live_addons: None,
            sidecar: tokio::sync::Mutex::new(SidecarSupervisor::new(
                tmp.path().join("torrent-cache"),
            )),
            ready: AtomicBool::new(true),
        })
    }

    async fn json_body(response: Response) -> serde_json::Value {
        let bytes = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .expect("response body");
        serde_json::from_slice(&bytes).expect("json body")
    }

    #[tokio::test]
    async fn sources_returns_seeded_candidates_for_series_2() {
        let state = test_state();
        let response = catalog_sources(State(state), Path(SeriesPath { id: 2 })).await;
        assert_eq!(response.status(), StatusCode::OK);

        let json = json_body(response).await;
        let candidates = json["candidates"].as_array().expect("candidates array");
        assert_eq!(candidates.len(), 9);

        // Quality first: the 4K rows lead across both add-ons, and a NoTorrent
        // 4K direct row sorts above Torrentio 1080p rows (install priority is
        // the last key, not the first).
        assert_eq!(candidates[0]["addon"], "Torrentio");
        assert_eq!(candidates[0]["kind"], "torrent");
        assert_eq!(candidates[0]["quality"], "4K");
        assert!(candidates[0]["info_hash"].is_string());
        assert_eq!(candidates[0]["file_idx"], 0);

        assert_eq!(candidates[1]["addon"], "NoTorrent");
        assert_eq!(candidates[1]["kind"], "direct");
        assert_eq!(candidates[1]["quality"], "4K");
        assert!(candidates[1]["url"]
            .as_str()
            .expect("url")
            .starts_with("https://"));

        // Deterministic aggregate counts and install order.
        let counts = json["counts_by_addon"].as_object().expect("counts");
        assert_eq!(counts["Torrentio"], 6);
        assert_eq!(counts["NoTorrent"], 3);

        let installed = json["installed_addons"].as_array().expect("installed");
        assert_eq!(installed.len(), 2);
        assert_eq!(installed[0]["name"], "Torrentio");
        assert_eq!(installed[0]["priority"], 0);
        assert_eq!(installed[1]["name"], "NoTorrent");
        assert_eq!(installed[1]["priority"], 1);
    }

    #[tokio::test]
    async fn sources_404_for_unknown_series_id() {
        let state = test_state();
        let response = catalog_sources(State(state), Path(SeriesPath { id: 999 })).await;
        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let json = json_body(response).await;
        assert_eq!(json["error"]["code"], "not_found");
        assert_eq!(json["error"]["message"], "No series with that id.");
    }

    #[test]
    fn live_flag_is_exactly_the_string_one() {
        assert!(live_enabled(Some("1")));
        assert!(!live_enabled(None));
        assert!(!live_enabled(Some("0")));
        assert!(!live_enabled(Some("true")));
        assert!(!live_enabled(Some("")));
    }

    #[tokio::test]
    async fn live_search_falls_back_to_seed_when_provider_is_down() {
        let mut state = test_state();
        // Point the live client at a dead local port: the request fails
        // immediately, so the route must degrade to the local seed.
        Arc::get_mut(&mut state)
            .expect("fresh state has a single owner")
            .live = Some(Arc::new(Cinemeta::with_base(
            "http://127.0.0.1:1".to_string(),
        )));

        let response = search_catalog(
            State(state),
            Query(SearchQuery {
                q: "alpha".to_string(),
            }),
        )
        .await;
        assert_eq!(response.status(), StatusCode::OK);

        let json = json_body(response).await;
        let rows = json.as_array().expect("seed fallback is an array");
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0]["title"], "Demo Series Alpha");
        assert_eq!(rows[0]["id"], 1);
    }

    #[test]
    fn media_type_for_tt_id_maps_bare_to_movie_and_colon_to_series() {
        assert_eq!(media_type_for_tt_id("tt1160419"), "movie");
        assert_eq!(media_type_for_tt_id("tt10466872:1:2"), "series");
    }

    #[tokio::test]
    async fn sources_imdb_404s_offline() {
        let state = test_state(); // live_torrentio / live_addons are None
        let response = sources_imdb(
            State(state),
            Path(ImdbPath {
                tt_id: "tt1160419".to_string(),
            }),
        )
        .await;
        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let json = json_body(response).await;
        assert_eq!(json["error"]["code"], "not_found");
        assert_eq!(
            json["error"]["message"],
            "Live sources are disabled; run with ADDONS_LIVE=1."
        );
    }

    #[tokio::test]
    async fn catalog_meta_404s_offline() {
        let state = test_state(); // live Cinemeta is None
        let response = catalog_meta(
            State(state),
            Path(MetaPath {
                media_type: "movie".to_string(),
                tt_id: "tt1160419".to_string(),
            }),
        )
        .await;
        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let json = json_body(response).await;
        assert_eq!(json["error"]["code"], "not_found");
        assert_eq!(
            json["error"]["message"],
            "Live metadata is disabled; run with ADDONS_LIVE=1."
        );
    }

    #[tokio::test]
    async fn catalog_meta_returns_502_when_cinemeta_is_down() {
        let mut state = test_state();
        Arc::get_mut(&mut state)
            .expect("fresh state has a single owner")
            .live = Some(Arc::new(Cinemeta::with_base(
            "http://127.0.0.1:1".to_string(),
        )));

        let response = catalog_meta(
            State(state),
            Path(MetaPath {
                media_type: "movie".to_string(),
                tt_id: "tt1160419".to_string(),
            }),
        )
        .await;
        assert_eq!(response.status(), StatusCode::BAD_GATEWAY);

        let json = json_body(response).await;
        assert_eq!(json["error"]["code"], "provider_unavailable");
        assert_eq!(json["error"]["message"], "Cinemeta metadata lookup failed.");
    }

    #[tokio::test]
    async fn sources_imdb_returns_502_when_torrentio_is_down() {
        let mut state = test_state();
        // Point the live client at a dead local port: the request fails
        // immediately, so the route must surface the Go envelope instead of
        // hanging or returning candidates.
        Arc::get_mut(&mut state)
            .expect("fresh state has a single owner")
            .live_torrentio = Some(Arc::new(Torrentio::with_base(
            "http://127.0.0.1:1".to_string(),
        )));
        Arc::get_mut(&mut state)
            .expect("fresh state has a single owner")
            .live_addons = Some(Arc::new(Registry::live()));

        let response = sources_imdb(
            State(state),
            Path(ImdbPath {
                tt_id: "tt1160419".to_string(),
            }),
        )
        .await;
        assert_eq!(response.status(), StatusCode::BAD_GATEWAY);

        let json = json_body(response).await;
        assert_eq!(json["error"]["code"], "provider_unavailable");
        assert_eq!(json["error"]["message"], "Torrentio stream lookup failed.");
    }
}
