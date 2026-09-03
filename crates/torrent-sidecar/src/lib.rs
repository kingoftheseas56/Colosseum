//! Torrent sidecar — the torrent-engine spooler for torrent-parity slice 3.
//!
//! Design authority: `docs/research/torrent-sidecar-phase0/{00-engine-decision.md,
//! 01-sidecar-design.md}`. The sidecar is a **spooler to local cache files**,
//! not a loopback media server: the daemon asks it to download one file of one
//! torrent (`info_hash` + `file_idx`) and it reports a piece-verified,
//! complete file path the player loads via `file://`.
//!
//! This crate is split lib + bin so the offline deterministic tests can drive
//! the [`Engine`] and the control API in-process, without spawning a binary.
//!
//! # Control API (loopback HTTP, axum)
//!
//! Bound to `127.0.0.1:0`; the real port + a per-run random token are written
//! to `<data_dir>/torrent-sidecar.port` (mode 0600) before serving. Every
//! route requires `X-Sidecar-Token` — a torrent engine on loopback is a local
//! attack surface (any local process could otherwise ask it to download
//! arbitrary hashes).
//!
//! - `POST /torrents {info_hash, file_idx}` → `{torrent_id}` (re-add of a
//!   known hash → 409 `already_managed` with the same id, idempotent).
//! - `GET /torrents/{id}` → `{status, progress}` and, once the selected file
//!   is piece-verified complete, `{status: "complete", path}` where `path` is
//!   resolved from `FileInfo::relative_filename` under `<cache>/<ih40>/`.
//! - `DELETE /torrents/{id}` → evict (delete torrent + downloaded files).
//!
//! Errors use the same envelope as the daemon: `{"error":{"code","message"}}`
//! with the dossier taxonomy `bad_request` / `not_found` / `already_managed` /
//! `engine_error` / `metadata_unavailable` / `stalled` (+ `unauthorized` for
//! the token gate).

pub mod engine;
pub mod error;
pub mod http;
pub mod port_file;

pub use engine::{Engine, TorrentStatus};
pub use error::SidecarError;
pub use port_file::PortFile;

#[cfg(test)]
mod tests;

/// The control-API wire request body for `POST /torrents`.
#[derive(Debug, Clone, serde::Deserialize)]
pub struct AddTorrentRequest {
    pub info_hash: String,
    pub file_idx: u64,
}

/// The control-API wire response body for `POST /torrents`.
#[derive(Debug, Clone, serde::Serialize)]
pub struct AddTorrentResponse {
    pub torrent_id: usize,
    pub already_managed: bool,
}
