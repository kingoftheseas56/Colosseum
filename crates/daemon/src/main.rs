//! Daemon — the composition root. Owns HTTP, path discovery, and wiring.
//! Domain logic lives in the `catalog` and `account` crates; this binary only
//! maps their contracts onto routes.

mod paths;

use std::sync::Arc;

use axum::extract::{Query, State};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::Deserialize;
use tracing_subscriber::EnvFilter;

use account::{CreateAccountRequest, RefreshRequest, Service as AccountService, SignInRequest};
use catalog::Catalog;

struct AppState {
    catalog: Arc<Catalog>,
    accounts: Arc<AccountService>,
}

#[derive(Deserialize)]
struct SearchQuery {
    q: String,
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
    });
    state.catalog.seed_demo();

    let app = Router::new()
        .route("/healthz", get(|| async { "ok" }))
        .route("/readyz", get(|| async { "ok" }))
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

async fn search_catalog(
    State(state): State<Arc<AppState>>,
    Query(query): Query<SearchQuery>,
) -> Json<Vec<catalog::Series>> {
    Json(state.catalog.search(&query.q))
}

async fn create_account(
    State(state): State<Arc<AppState>>,
    Json(request): Json<CreateAccountRequest>,
) -> Json<account::Session> {
    Json(
        state
            .accounts
            .create_account(request)
            .expect("TODO.md: account-core"),
    )
}

async fn sign_in(
    State(state): State<Arc<AppState>>,
    Json(request): Json<SignInRequest>,
) -> Json<account::Session> {
    Json(
        state
            .accounts
            .sign_in(request)
            .expect("TODO.md: account-core"),
    )
}

async fn refresh_session(
    State(state): State<Arc<AppState>>,
    Json(request): Json<RefreshRequest>,
) -> Json<account::Session> {
    Json(
        state
            .accounts
            .refresh(request)
            .expect("TODO.md: account-core"),
    )
}
