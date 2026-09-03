//! The control API: axum router bound to loopback with `X-Sidecar-Token` auth.
//!
//! Every route is gated by the token middleware; the token + engine are the
//! router state. The router is public so the offline tests can drive it
//! in-process (`tower::ServiceExt::oneshot`) without a socket.

use std::sync::Arc;

use axum::extract::{Path, Request, State};
use axum::http::StatusCode;
use axum::middleware::{self, Next};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::Serialize;

use crate::engine::Engine;
use crate::error::{error_response, unauthorized_response, ErrorDetail};
use crate::{AddTorrentRequest, AddTorrentResponse};

/// Router state.
struct AppState {
    engine: Engine,
    token: String,
}

/// The `409 already_managed` body: the standard envelope plus the existing id
/// so the daemon can proceed to poll without re-adding.
#[derive(Serialize)]
struct AlreadyManagedBody {
    error: ErrorDetail,
    torrent_id: usize,
}

/// The `DELETE /torrents/{id}` body.
#[derive(Serialize)]
struct EvictedBody {
    torrent_id: usize,
    status: &'static str,
}

/// Build the control-API router for an engine + per-run token.
pub fn router(engine: Engine, token: String) -> Router {
    let state = Arc::new(AppState { engine, token });
    Router::new()
        .route("/torrents", post(add_torrent))
        .route("/torrents/{id}", get(get_status).delete(delete_torrent))
        .layer(middleware::from_fn_with_state(state.clone(), require_token))
        .with_state(state)
}

/// Token gate: strict loopback bind is the outer layer, this is the inner one.
/// Missing or wrong `X-Sidecar-Token` → 401 with the envelope.
async fn require_token(
    State(state): State<Arc<AppState>>,
    request: Request,
    next: Next,
) -> Response {
    let token = request
        .headers()
        .get("x-sidecar-token")
        .and_then(|v| v.to_str().ok());
    if token != Some(state.token.as_str()) {
        return unauthorized_response();
    }
    next.run(request).await
}

async fn add_torrent(
    State(state): State<Arc<AppState>>,
    Json(request): Json<AddTorrentRequest>,
) -> Response {
    match state
        .engine
        .add(&request.info_hash, request.file_idx, None)
        .await
    {
        Ok(outcome) if !outcome.already_managed => (
            StatusCode::OK,
            Json(AddTorrentResponse {
                torrent_id: outcome.torrent_id,
                already_managed: false,
            }),
        )
            .into_response(),
        Ok(outcome) => {
            let mut response = Json(AlreadyManagedBody {
                error: ErrorDetail {
                    code: "already_managed".to_string(),
                    message: "Torrent already managed.".to_string(),
                },
                torrent_id: outcome.torrent_id,
            })
            .into_response();
            *response.status_mut() = StatusCode::CONFLICT;
            response
        }
        Err(err) => error_response(&err),
    }
}

async fn get_status(State(state): State<Arc<AppState>>, Path(id): Path<usize>) -> Response {
    match state.engine.status(id).await {
        Ok(status) => (StatusCode::OK, Json(status)).into_response(),
        Err(err) => error_response(&err),
    }
}

async fn delete_torrent(State(state): State<Arc<AppState>>, Path(id): Path<usize>) -> Response {
    match state.engine.evict(id).await {
        Ok(()) => (
            StatusCode::OK,
            Json(EvictedBody {
                torrent_id: id,
                status: "evicted",
            }),
        )
            .into_response(),
        Err(err) => error_response(&err),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::body::Body;
    use axum::http::Request;
    use tower::ServiceExt;

    async fn test_engine() -> Engine {
        let dir = tempfile::tempdir().unwrap();
        Engine::new_offline(dir.path().to_path_buf()).await.unwrap()
    }

    async fn body_json(response: Response) -> serde_json::Value {
        let bytes = axum::body::to_bytes(response.into_body(), usize::MAX)
            .await
            .unwrap();
        serde_json::from_slice(&bytes).unwrap()
    }

    fn request(method: &str, uri: &str, token: Option<&str>, body: &str) -> Request<Body> {
        let mut builder = Request::builder().method(method).uri(uri);
        if let Some(t) = token {
            builder = builder.header("x-sidecar-token", t);
        }
        builder
            .header("content-type", "application/json")
            .body(Body::from(body.to_string()))
            .unwrap()
    }

    #[tokio::test]
    async fn missing_token_is_401() {
        let app = router(test_engine().await, "secret".into());
        let response = app
            .oneshot(request(
                "POST",
                "/torrents",
                None,
                r#"{"info_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","file_idx":0}"#,
            ))
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
        let json = body_json(response).await;
        assert_eq!(json["error"]["code"], "unauthorized");
    }

    #[tokio::test]
    async fn wrong_token_is_401() {
        let app = router(test_engine().await, "secret".into());
        let response = app
            .oneshot(request("GET", "/torrents/0", Some("nope"), ""))
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
        let json = body_json(response).await;
        assert_eq!(json["error"]["code"], "unauthorized");
    }

    #[tokio::test]
    async fn bad_info_hash_is_400() {
        let app = router(test_engine().await, "secret".into());
        let response = app
            .oneshot(request(
                "POST",
                "/torrents",
                Some("secret"),
                r#"{"info_hash":"short","file_idx":0}"#,
            ))
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
        let json = body_json(response).await;
        assert_eq!(json["error"]["code"], "bad_request");
    }

    #[tokio::test]
    async fn unknown_torrent_id_is_404() {
        let app = router(test_engine().await, "secret".into());
        let response = app
            .oneshot(request("GET", "/torrents/999", Some("secret"), ""))
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::NOT_FOUND);
        let json = body_json(response).await;
        assert_eq!(json["error"]["code"], "not_found");
    }
}
