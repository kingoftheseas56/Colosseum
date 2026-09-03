//! Torrent-sidecar supervision + spool orchestration (daemon side).
//!
//! The daemon spawns the `torrent-sidecar` binary lazily on the first
//! torrent-kind spool request, reads its port/token file, and supervises it
//! (restart-once on crash, kill on shutdown). It then drives the sidecar's
//! control API: add → poll → piece-verified complete path, which it hands back
//! to the UI layer as a `file://` URL. The daemon does **not** own the player
//! (per `01-sidecar-design.md`); it exposes the completed path for slice-4/UI
//! to consume.
//!
//! The sidecar is a separate process on purpose — a torrent engine is a local
//! attack surface and a heavy dependency; the daemon only speaks the tiny
//! loopback control API.

use std::path::{Path, PathBuf};
use std::time::Duration;

use axum::http::StatusCode;
use serde::{Deserialize, Serialize};

/// The port/token handoff file the sidecar writes on boot.
#[derive(Debug, Clone, Deserialize)]
pub struct PortFile {
    pub port: u16,
    pub token: String,
}

/// The sidecar's `GET /torrents/{id}` body (only the fields spooling reads).
#[derive(Debug, Clone, Deserialize)]
pub struct SidecarStatus {
    #[serde(default)]
    pub status: String,
    #[serde(default)]
    pub progress: f64,
    #[serde(default)]
    pub path: Option<String>,
}

/// The daemon's spool response: the completed, piece-verified path as a
/// `file://` URL for the UI layer to hand to `player.load`.
#[derive(Debug, Clone, Serialize)]
pub struct SpoolResponse {
    pub torrent_id: usize,
    pub info_hash: String,
    pub file_idx: u64,
    pub status: &'static str,
    pub path: String,
}

/// The 1:1 candidate → add mapping. The daemon keys spools by the candidate's
/// wire-model row id (`t:<infoHash>:<fileIdx>`), which is exactly the addon
/// crate's `_rowKey` for torrent rows. `kind: direct` rows carry a `u:` id and
/// never reach this route (the player loads their url directly).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpoolRequest {
    pub candidate_id: String,
    pub info_hash: String,
    pub file_idx: u64,
}

/// Parse a torrent candidate row id (`t:<40-hex infoHash>:<fileIdx>`) back into
/// the add request, validating the info hash. `u:` (direct) rows are rejected —
/// they are not a torrent-kind candidate.
pub fn parse_candidate_id(candidate_id: &str) -> Result<SpoolRequest, SpoolError> {
    let Some(rest) = candidate_id.strip_prefix("t:") else {
        return Err(SpoolError::BadRequest(format!(
            "candidate id {candidate_id:?} is not a torrent row (expected `t:` prefix)"
        )));
    };
    let (info_hash, file_idx) = rest.split_once(':').ok_or_else(|| {
        SpoolError::BadRequest(format!(
            "candidate id {candidate_id:?} is malformed (expected `t:<info_hash>:<file_idx>`)"
        ))
    })?;
    let info_hash = info_hash.to_ascii_lowercase();
    if info_hash.len() != 40 || !info_hash.chars().all(|c| c.is_ascii_hexdigit()) {
        return Err(SpoolError::BadRequest(format!(
            "candidate id {candidate_id:?} has a non-40-hex info hash"
        )));
    }
    let file_idx: u64 = file_idx.parse().map_err(|_| {
        SpoolError::BadRequest(format!(
            "candidate id {candidate_id:?} has a non-numeric file_idx"
        ))
    })?;
    Ok(SpoolRequest {
        candidate_id: candidate_id.to_string(),
        info_hash,
        file_idx,
    })
}

/// Convert an absolute path to a `file://` URL (macOS/unix absolute paths).
pub fn file_url(path: &str) -> String {
    format!("file://{path}")
}

/// Spool errors, mapped onto the daemon's Go-style error envelope. The
/// daemon-side taxonomy adds `sidecar_unavailable` (dead/unreachable sidecar)
/// to the sidecar's own set.
#[derive(Debug, thiserror::Error)]
pub enum SpoolError {
    #[error("{0}")]
    SidecarUnavailable(String),
    #[error("{0}")]
    BadRequest(String),
    #[error("{0}")]
    NotFound(String),
    #[error("{0}")]
    Engine(String),
    #[error("{0}")]
    MetadataUnavailable(String),
    #[error("{0}")]
    Stalled(String),
}

impl SpoolError {
    pub fn code(&self) -> &'static str {
        match self {
            SpoolError::SidecarUnavailable(_) => "sidecar_unavailable",
            SpoolError::BadRequest(_) => "bad_request",
            SpoolError::NotFound(_) => "not_found",
            SpoolError::Engine(_) => "engine_error",
            SpoolError::MetadataUnavailable(_) => "metadata_unavailable",
            SpoolError::Stalled(_) => "stalled",
        }
    }

    pub fn status(&self) -> StatusCode {
        match self {
            SpoolError::SidecarUnavailable(_) => StatusCode::SERVICE_UNAVAILABLE,
            SpoolError::BadRequest(_) => StatusCode::BAD_REQUEST,
            SpoolError::NotFound(_) => StatusCode::NOT_FOUND,
            SpoolError::Engine(_) => StatusCode::BAD_GATEWAY,
            SpoolError::MetadataUnavailable(_) => StatusCode::SERVICE_UNAVAILABLE,
            SpoolError::Stalled(_) => StatusCode::GATEWAY_TIMEOUT,
        }
    }
}

/// Errors from the control client (transport vs sidecar-envelope).
#[derive(Debug, thiserror::Error)]
pub enum ControlError {
    #[error("sidecar unreachable: {0}")]
    Unavailable(String),
    #[error("sidecar error: {code} — {message}")]
    Envelope { code: String, message: String },
    #[error("unexpected sidecar response: {0}")]
    BadResponse(String),
}

/// The HTTP control client for the sidecar's loopback API.
#[derive(Debug)]
pub struct SidecarClient {
    http: reqwest::Client,
    base: String,
    token: String,
}

impl SidecarClient {
    pub fn new(port: u16, token: String) -> Self {
        Self {
            http: reqwest::Client::new(),
            base: format!("http://127.0.0.1:{port}"),
            token,
        }
    }

    fn request(&self, method: reqwest::Method, path: &str) -> reqwest::RequestBuilder {
        self.http
            .request(method, format!("{}{path}", self.base))
            .header("x-sidecar-token", &self.token)
    }

    /// `POST /torrents` → the torrent id. Idempotent: a 409 `already_managed`
    /// still carries the id and is treated as success.
    pub async fn add(&self, info_hash: &str, file_idx: u64) -> Result<usize, ControlError> {
        let response = self
            .request(reqwest::Method::POST, "/torrents")
            .json(&serde_json::json!({ "info_hash": info_hash, "file_idx": file_idx }))
            .send()
            .await
            .map_err(|e| ControlError::Unavailable(e.to_string()))?;
        let status = response.status();
        let body: serde_json::Value = response
            .json()
            .await
            .map_err(|e| ControlError::BadResponse(e.to_string()))?;

        match status {
            StatusCode::OK => body["torrent_id"]
                .as_u64()
                .map(|v| v as usize)
                .ok_or_else(|| ControlError::BadResponse("missing torrent_id".into())),
            StatusCode::CONFLICT if body["error"]["code"] == "already_managed" => body
                ["torrent_id"]
                .as_u64()
                .map(|v| v as usize)
                .ok_or_else(|| ControlError::BadResponse("missing torrent_id".into())),
            _ => Err(ControlError::Envelope {
                code: body["error"]["code"]
                    .as_str()
                    .unwrap_or("engine_error")
                    .to_string(),
                message: body["error"]["message"]
                    .as_str()
                    .unwrap_or("sidecar error")
                    .to_string(),
            }),
        }
    }

    /// `GET /torrents/{id}` → status.
    pub async fn status(&self, id: usize) -> Result<SidecarStatus, ControlError> {
        let response = self
            .request(reqwest::Method::GET, &format!("/torrents/{id}"))
            .send()
            .await
            .map_err(|e| ControlError::Unavailable(e.to_string()))?;
        let status = response.status();
        let body: serde_json::Value = response
            .json()
            .await
            .map_err(|e| ControlError::BadResponse(e.to_string()))?;

        if status != StatusCode::OK {
            return Err(ControlError::Envelope {
                code: body["error"]["code"]
                    .as_str()
                    .unwrap_or("engine_error")
                    .to_string(),
                message: body["error"]["message"]
                    .as_str()
                    .unwrap_or("sidecar error")
                    .to_string(),
            });
        }
        serde_json::from_value(body).map_err(|e| ControlError::BadResponse(e.to_string()))
    }
}

impl From<ControlError> for SpoolError {
    fn from(err: ControlError) -> Self {
        match err {
            ControlError::Unavailable(msg) => SpoolError::SidecarUnavailable(msg),
            ControlError::BadResponse(msg) => SpoolError::Engine(msg),
            ControlError::Envelope { code, message } => match code.as_str() {
                "bad_request" => SpoolError::BadRequest(message),
                "not_found" => SpoolError::NotFound(message),
                "engine_error" => SpoolError::Engine(message),
                "metadata_unavailable" => SpoolError::MetadataUnavailable(message),
                "stalled" => SpoolError::Stalled(message),
                "already_managed" => SpoolError::Engine(message),
                other => SpoolError::Engine(format!("{other}: {message}")),
            },
        }
    }
}

/// Add → poll → piece-verified complete path. Polls at `poll` cadence up to
/// `timeout`; exceeding the bound is `stalled` (the daemon-side watchdog).
pub async fn spool(
    client: &SidecarClient,
    info_hash: &str,
    file_idx: u64,
    poll: Duration,
    timeout: Duration,
) -> Result<SpoolResponse, SpoolError> {
    let torrent_id = client.add(info_hash, file_idx).await?;

    let deadline = tokio::time::Instant::now() + timeout;
    loop {
        let status = client.status(torrent_id).await?;
        if status.status == "complete" {
            let path = status
                .path
                .ok_or_else(|| SpoolError::Engine("complete status without a path".into()))?;
            return Ok(SpoolResponse {
                torrent_id,
                info_hash: info_hash.to_string(),
                file_idx,
                status: "complete",
                path: file_url(&path),
            });
        }
        tracing::debug!(torrent_id, progress = status.progress, "spooling");
        if tokio::time::Instant::now() >= deadline {
            return Err(SpoolError::Stalled(format!(
                "torrent {torrent_id} did not complete within {timeout:?}"
            )));
        }
        tokio::time::sleep(poll).await;
    }
}

/// Read the sidecar's port/token file. Returns the parsed [`PortFile`].
pub fn read_port_file(path: &Path) -> std::io::Result<PortFile> {
    let bytes = std::fs::read(path)?;
    serde_json::from_slice(&bytes)
        .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
}

/// The supervisor: owns the sidecar child process and the control client.
///
/// The sidecar binary sits next to the daemon binary (same build dir). The
/// cache dir is `<data_dir>/torrent-cache`; the port file is one level up at
/// `<data_dir>/torrent-sidecar.port`.
pub struct SidecarSupervisor {
    cache_dir: PathBuf,
    binary: PathBuf,
    port_file: PathBuf,
    child: Option<tokio::process::Child>,
    restarted: bool,
    client: Option<std::sync::Arc<SidecarClient>>,
}

impl SidecarSupervisor {
    /// Build a supervisor for the given cache dir (the daemon passes
    /// `<data_dir>/torrent-cache`).
    pub fn new(cache_dir: PathBuf) -> Self {
        let port_file = cache_dir
            .parent()
            .unwrap_or_else(|| Path::new("."))
            .join("torrent-sidecar.port");
        let exe = std::env::current_exe().expect("current exe");
        let dir = exe.parent().expect("exe has a parent");
        let binary = dir.join(if cfg!(windows) {
            "torrent-sidecar.exe"
        } else {
            "torrent-sidecar"
        });
        Self {
            cache_dir,
            binary,
            port_file,
            child: None,
            restarted: false,
            client: None,
        }
    }

    /// Spawn the sidecar (or return the existing client if already running)
    /// and wait for its port/token file to appear.
    pub async fn ensure_running(&mut self) -> Result<std::sync::Arc<SidecarClient>, SpoolError> {
        if let Some(client) = &self.client {
            return Ok(client.clone());
        }
        self.spawn().await?;
        Ok(self.client.clone().expect("client set after spawn"))
    }

    /// Kill the current child and respawn once (restart-once supervision).
    pub async fn restart(&mut self) -> Result<std::sync::Arc<SidecarClient>, SpoolError> {
        self.kill_child().await;
        self.spawn().await?;
        Ok(self.client.clone().expect("client set after spawn"))
    }

    async fn spawn(&mut self) -> Result<(), SpoolError> {
        std::fs::create_dir_all(&self.cache_dir)
            .map_err(|e| SpoolError::SidecarUnavailable(format!("create cache dir: {e}")))?;
        // Clear any stale port file so a fresh boot's write is observable.
        let _ = std::fs::remove_file(&self.port_file);

        let child = tokio::process::Command::new(&self.binary)
            .arg("--cache-dir")
            .arg(&self.cache_dir)
            .env("RUST_LOG", "torrent_sidecar=info")
            .spawn()
            .map_err(|e| SpoolError::SidecarUnavailable(format!("spawn sidecar: {e}")))?;
        self.child = Some(child);

        let port_file = self.read_port_file_timeout(Duration::from_secs(15)).await?;
        tracing::info!(port = port_file.port, "sidecar up");
        self.client = Some(std::sync::Arc::new(SidecarClient::new(
            port_file.port,
            port_file.token,
        )));
        Ok(())
    }

    async fn read_port_file_timeout(&self, timeout: Duration) -> Result<PortFile, SpoolError> {
        let deadline = tokio::time::Instant::now() + timeout;
        loop {
            match read_port_file(&self.port_file) {
                Ok(port_file) => return Ok(port_file),
                Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                    if tokio::time::Instant::now() >= deadline {
                        return Err(SpoolError::SidecarUnavailable(format!(
                            "sidecar did not write {} within {timeout:?}",
                            self.port_file.display()
                        )));
                    }
                    tokio::time::sleep(Duration::from_millis(50)).await;
                }
                Err(e) => {
                    return Err(SpoolError::SidecarUnavailable(format!(
                        "read port file: {e}"
                    )))
                }
            }
        }
    }

    async fn kill_child(&mut self) {
        if let Some(mut child) = self.child.take() {
            let _ = child.kill().await;
            let _ = child.wait().await;
        }
        self.client = None;
    }

    /// Kill the sidecar on daemon shutdown.
    pub async fn shutdown(&mut self) {
        self.kill_child().await;
    }

    /// Whether a restart has already been used (restart-once).
    pub fn restarted(&self) -> bool {
        self.restarted
    }

    pub fn mark_restarted(&mut self) {
        self.restarted = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_candidate_id_maps_torrent_row_key_and_rejects_direct_rows() {
        let request = parse_candidate_id("t:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:3").unwrap();
        assert_eq!(
            request,
            SpoolRequest {
                candidate_id: "t:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:3".into(),
                info_hash: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".into(),
                file_idx: 3,
            }
        );

        // Uppercase info hash is lowercased (the wire model lowercases it too).
        let request = parse_candidate_id("t:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:0").unwrap();
        assert_eq!(
            request.info_hash,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        );

        // Direct rows (`u:<url>`) never reach the sidecar.
        let err = parse_candidate_id("u:https://cdn.example/f.mkv").unwrap_err();
        assert_eq!(err.code(), "bad_request");

        // Malformed / wrong-length hashes are rejected.
        assert_eq!(
            parse_candidate_id("t:short:0").unwrap_err().code(),
            "bad_request"
        );
        assert_eq!(
            parse_candidate_id("t:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:xyz")
                .unwrap_err()
                .code(),
            "bad_request"
        );
    }

    #[test]
    fn file_url_is_absolute_file_scheme() {
        assert_eq!(file_url("/tmp/x/a.bin"), "file:///tmp/x/a.bin");
    }

    #[test]
    fn port_file_parses() {
        let pf: PortFile = serde_json::from_str(r#"{"port":41234,"token":"tok"}"#).unwrap();
        assert_eq!(pf.port, 41234);
        assert_eq!(pf.token, "tok");
    }

    #[test]
    fn spool_error_codes_match_taxonomy() {
        assert_eq!(
            SpoolError::SidecarUnavailable("x".into()).code(),
            "sidecar_unavailable"
        );
        assert_eq!(
            SpoolError::MetadataUnavailable("x".into()).code(),
            "metadata_unavailable"
        );
        assert_eq!(SpoolError::Stalled("x".into()).code(), "stalled");
        assert_eq!(SpoolError::Engine("x".into()).code(), "engine_error");
    }

    // ── spool orchestration against a mock sidecar (no real process) ──────

    async fn serve_mock(app: axum::Router) -> u16 {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        tokio::spawn(async move {
            axum::serve(listener, app).await.unwrap();
        });
        port
    }

    #[tokio::test]
    async fn spool_adds_then_polls_until_complete_and_returns_file_url() {
        use axum::routing::get;
        async fn mock_add() -> axum::Json<serde_json::Value> {
            axum::Json(serde_json::json!({ "torrent_id": 7, "already_managed": false }))
        }
        async fn mock_status() -> axum::Json<serde_json::Value> {
            axum::Json(
                serde_json::json!({ "torrent_id": 7, "status": "complete", "progress": 1.0, "path": "/tmp/x/a.bin" }),
            )
        }
        let app = axum::Router::new()
            .route("/torrents", axum::routing::post(mock_add))
            .route("/torrents/{id}", get(mock_status));
        let port = serve_mock(app).await;

        let client = SidecarClient::new(port, "tok".into());
        let response = spool(
            &client,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            0,
            Duration::from_millis(10),
            Duration::from_secs(5),
        )
        .await
        .unwrap();
        assert_eq!(response.torrent_id, 7);
        assert_eq!(response.status, "complete");
        assert_eq!(response.path, "file:///tmp/x/a.bin");
    }

    #[tokio::test]
    async fn spool_propagates_sidecar_envelope_codes() {
        use axum::http::StatusCode;
        async fn mock_add() -> (axum::http::StatusCode, axum::Json<serde_json::Value>) {
            (
                StatusCode::SERVICE_UNAVAILABLE,
                axum::Json(
                    serde_json::json!({ "error": { "code": "metadata_unavailable", "message": "no peers" } }),
                ),
            )
        }
        let port =
            serve_mock(axum::Router::new().route("/torrents", axum::routing::post(mock_add))).await;

        let client = SidecarClient::new(port, "tok".into());
        let err = spool(
            &client,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            0,
            Duration::from_millis(10),
            Duration::from_secs(5),
        )
        .await
        .unwrap_err();
        assert_eq!(err.code(), "metadata_unavailable");
    }

    #[tokio::test]
    async fn spool_returns_sidecar_unavailable_when_nothing_listens() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener); // port is now free → connection refused

        let client = SidecarClient::new(port, "tok".into());
        let err = spool(
            &client,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            0,
            Duration::from_millis(10),
            Duration::from_secs(2),
        )
        .await
        .unwrap_err();
        assert_eq!(err.code(), "sidecar_unavailable");
    }

    #[tokio::test]
    async fn spool_times_out_as_stalled() {
        async fn mock_add() -> axum::Json<serde_json::Value> {
            axum::Json(serde_json::json!({ "torrent_id": 1, "already_managed": false }))
        }
        async fn mock_status() -> axum::Json<serde_json::Value> {
            axum::Json(
                serde_json::json!({ "torrent_id": 1, "status": "downloading", "progress": 0.5 }),
            )
        }
        let app = axum::Router::new()
            .route("/torrents", axum::routing::post(mock_add))
            .route("/torrents/{id}", axum::routing::get(mock_status));
        let port = serve_mock(app).await;

        let client = SidecarClient::new(port, "tok".into());
        let err = spool(
            &client,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            0,
            Duration::from_millis(5),
            Duration::from_millis(100),
        )
        .await
        .unwrap_err();
        assert_eq!(err.code(), "stalled");
    }
}
