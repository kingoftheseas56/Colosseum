//! Sidecar error taxonomy and the shared `{"error":{code,message}}` envelope.
//!
//! Mirrors the daemon's wire envelope (`WriteAPIError` / `write_api_error` in
//! `crates/daemon/src/main.rs`) so both sides of the control channel speak the
//! same shape. Status codes per `01-sidecar-design.md`:
//!
//! | code | status | meaning |
//! |---|---|---|
//! | `unauthorized` | 401 | missing/wrong `X-Sidecar-Token` |
//! | `bad_request` | 400 | malformed body (bad info_hash, etc.) |
//! | `not_found` | 404 | unknown torrent id on status/delete |
//! | `already_managed` | 409 | idempotent re-add of a known hash |
//! | `engine_error` | 502 | librqbit API/storage failure |
//! | `metadata_unavailable` | 503 | magnet add could not fetch metadata |
//! | `stalled` | 504 | watchdog: no peers / no progress |

use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Serialize;

/// Domain error for the sidecar. Variants carry the taxonomy code + HTTP status.
#[derive(Debug, thiserror::Error)]
pub enum SidecarError {
    #[error("{0}")]
    BadRequest(String),
    #[error("{0}")]
    NotFound(String),
    /// Idempotent re-add: the torrent is already managed under `torrent_id`.
    #[error("torrent already managed")]
    AlreadyManaged { torrent_id: usize },
    #[error("{0}")]
    Engine(String),
    #[error("{0}")]
    MetadataUnavailable(String),
    #[error("{0}")]
    Stalled(String),
}

impl SidecarError {
    /// The taxonomy code carried in the error envelope.
    pub fn code(&self) -> &'static str {
        match self {
            SidecarError::BadRequest(_) => "bad_request",
            SidecarError::NotFound(_) => "not_found",
            SidecarError::AlreadyManaged { .. } => "already_managed",
            SidecarError::Engine(_) => "engine_error",
            SidecarError::MetadataUnavailable(_) => "metadata_unavailable",
            SidecarError::Stalled(_) => "stalled",
        }
    }

    /// The HTTP status code.
    pub fn status(&self) -> StatusCode {
        match self {
            SidecarError::BadRequest(_) => StatusCode::BAD_REQUEST,
            SidecarError::NotFound(_) => StatusCode::NOT_FOUND,
            SidecarError::AlreadyManaged { .. } => StatusCode::CONFLICT,
            SidecarError::Engine(_) => StatusCode::BAD_GATEWAY,
            SidecarError::MetadataUnavailable(_) => StatusCode::SERVICE_UNAVAILABLE,
            SidecarError::Stalled(_) => StatusCode::GATEWAY_TIMEOUT,
        }
    }

    /// The message carried in the error envelope.
    pub fn message(&self) -> String {
        match self {
            SidecarError::AlreadyManaged { .. } => "Torrent already managed.".to_string(),
            other => other.to_string(),
        }
    }
}

/// The wire envelope body.
#[derive(Serialize)]
pub struct ErrorEnvelope {
    pub error: ErrorDetail,
}

#[derive(Serialize)]
pub struct ErrorDetail {
    pub code: String,
    pub message: String,
}

/// Build an envelope `Response` for a [`SidecarError`].
pub fn error_response(err: &SidecarError) -> Response {
    write_envelope(err.status(), err.code(), &err.message())
}

/// The `401 unauthorized` body for the token gate (no [`SidecarError`] variant
/// owns it; auth sits in the extractor, outside the domain errors).
pub fn unauthorized_response() -> Response {
    write_envelope(
        StatusCode::UNAUTHORIZED,
        "unauthorized",
        "Missing or invalid X-Sidecar-Token.",
    )
}

fn write_envelope(status: StatusCode, code: &str, message: &str) -> Response {
    let mut response = Json(ErrorEnvelope {
        error: ErrorDetail {
            code: code.to_owned(),
            message: message.to_owned(),
        },
    })
    .into_response();
    *response.status_mut() = status;
    response
}

/// Convert a raw librqbit error into an [`SidecarError`] by classifying the
/// message against the taxonomy. The `metadata_unavailable` split is
/// message-driven because librqbit reports magnet resolution failure as an
/// opaque `anyhow` error.
pub fn classify_engine_error(err: &dyn std::fmt::Display) -> SidecarError {
    let text = err.to_string();
    if text.contains("no known way to resolve peers")
        || text.contains("input address stream exhausted")
        || text.contains("resolve peers")
    {
        SidecarError::MetadataUnavailable(text)
    } else {
        SidecarError::Engine(text)
    }
}
