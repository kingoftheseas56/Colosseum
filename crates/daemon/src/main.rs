//! Daemon — the composition root. Owns HTTP, path discovery, and wiring.
//! Domain logic lives in the `catalog` and `account` crates; this binary only
//! maps their contracts onto routes.

mod paths;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use axum::extract::{Query, State};
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
