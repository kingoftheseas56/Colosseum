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

mod paths;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use axum::extract::{Path, Query, State};
use axum::http::header;
use axum::http::{HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::{Deserialize, Serialize};
use tracing_subscriber::EnvFilter;

use account::{CreateAccountRequest, RefreshRequest, Service as AccountService, SignInRequest};
use catalog::Catalog;

struct AppState {
    catalog: Arc<Catalog>,
    accounts: Arc<AccountService>,
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
        .route("/v1/accounts", post(create_account))
        .route("/v1/sessions", post(sign_in))
        .route("/v1/sessions/refresh", post(refresh_session))
        .with_state(state);

    let port: u16 = std::env::var("DAEMON_PORT")
        .ok()
        .and_then(|p| p.parse().ok())
        .unwrap_or(0);
    let listener = tokio::net::TcpListener::bind(("127.0.0.1", port))
        .await
        .expect("bind");
    tracing::info!("listening on http://{}", listener.local_addr().unwrap());
    axum::serve(listener, app).await.expect("serve");
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

async fn search_catalog(
    State(state): State<Arc<AppState>>,
    Query(query): Query<SearchQuery>,
) -> Json<Vec<catalog::Series>> {
    Json(state.catalog.search(&query.q))
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
