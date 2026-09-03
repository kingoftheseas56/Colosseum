//! The [`Engine`] — a thin, typed wrapper over a librqbit [`Session`] that
//! turns `info_hash` + `file_idx` into a piece-verified cache file.
//!
//! The session is a **pure downloader**: upload disabled (both the
//! `disable-upload` feature flag and the session option), upload rate pinned to
//! 1 B/s as belt-and-braces, Json persistence + fastresume so a completed file
//! is the resume fast path. Discovery posture is split:
//!
//! - [`production_session_options`]: DHT + trackers on — the live swarm path.
//! - [`offline_session_options`]: DHT off, trackers off — peers come only from
//!   add-time `initial_peers` (the deterministic offline gates).

use std::net::SocketAddr;
use std::num::NonZeroU32;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use librqbit::api::TorrentIdOrHash;
use librqbit::limits::LimitsConfig;
use librqbit::{
    AddTorrent, AddTorrentOptions, AddTorrentResponse, Magnet, Session, SessionOptions,
    SessionPersistenceConfig,
};
use serde::Serialize;

use crate::error::{classify_engine_error, SidecarError};

/// The engine: one librqbit session bound to a cache dir.
pub struct Engine {
    session: Arc<Session>,
    cache_dir: PathBuf,
    /// Peers applied to every add when the caller passes none — the offline
    /// posture's discovery mechanism, injectable so the real binary can be
    /// pointed at a local seeder (`--initial-peers`).
    default_initial_peers: Option<Vec<SocketAddr>>,
}

/// The result of [`Engine::add`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AddOutcome {
    pub torrent_id: usize,
    pub already_managed: bool,
}

/// The `GET /torrents/{id}` wire body.
#[derive(Debug, Clone, Serialize)]
pub struct TorrentStatus {
    pub torrent_id: usize,
    pub info_hash: String,
    pub file_idx: u64,
    /// `"downloading"` or `"complete"`.
    pub status: String,
    /// `progress_bytes / total_bytes` in `0.0..=1.0`, `0.0` while metadata is
    /// still resolving.
    pub progress: f64,
    /// The resolved on-disk path, present only once the selected file is
    /// piece-verified complete.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
}

/// Session options shared by both postures: pure downloader + Json persistence
/// in the cache dir + fastresume + IPv4-only + no incoming TCP listener.
fn base_session_options(cache_dir: &Path) -> SessionOptions {
    SessionOptions {
        fastresume: true,
        persistence: Some(SessionPersistenceConfig::Json {
            folder: Some(cache_dir.to_path_buf()),
        }),
        disable_upload: true,
        ipv4_only: true,
        listen: None,
        ratelimits: LimitsConfig {
            // Belt-and-braces on top of `disable_upload`: never advertise a
            // rate higher than the minimum (NonZeroU32 has no zero).
            upload_bps: NonZeroU32::new(1),
            download_bps: None,
        },
        ..Default::default()
    }
}

/// The live posture: DHT + trackers on (the magnet carries no trackers, so DHT
/// is the discovery workhorse for real swarms).
pub fn production_session_options(cache_dir: &Path) -> SessionOptions {
    base_session_options(cache_dir)
}

/// The offline posture: DHT and trackers off; the only peer source is the
/// per-add `initial_peers` list (a local seeder in the deterministic gates).
pub fn offline_session_options(cache_dir: &Path) -> SessionOptions {
    SessionOptions {
        dht: None,
        disable_trackers: true,
        ..base_session_options(cache_dir)
    }
}

impl Engine {
    /// A production-posture engine ([`production_session_options`]).
    pub async fn new(cache_dir: PathBuf) -> Result<Self, SidecarError> {
        Self::new_with_peers_and_options(
            cache_dir.clone(),
            production_session_options(&cache_dir),
            None,
        )
        .await
    }

    /// An offline-posture engine ([`offline_session_options`]) for the
    /// deterministic tests.
    pub async fn new_offline(cache_dir: PathBuf) -> Result<Self, SidecarError> {
        Self::new_with_peers_and_options(
            cache_dir.clone(),
            offline_session_options(&cache_dir),
            None,
        )
        .await
    }

    /// An offline-posture engine with a default peer list for every add — the
    /// real binary's `--initial-peers` path (offline posture is selected when
    /// initial peers are supplied).
    pub async fn new_offline_with_peers(
        cache_dir: PathBuf,
        peers: Option<Vec<SocketAddr>>,
    ) -> Result<Self, SidecarError> {
        Self::new_with_peers_and_options(
            cache_dir.clone(),
            offline_session_options(&cache_dir),
            peers,
        )
        .await
    }

    /// Construct the engine with explicit session options.
    pub async fn new_with(
        cache_dir: PathBuf,
        options: SessionOptions,
    ) -> Result<Self, SidecarError> {
        Self::new_with_peers_and_options(cache_dir, options, None).await
    }

    /// Construct the engine with explicit session options and a default peer
    /// list applied to every add that doesn't supply its own.
    pub async fn new_with_peers_and_options(
        cache_dir: PathBuf,
        options: SessionOptions,
        default_initial_peers: Option<Vec<SocketAddr>>,
    ) -> Result<Self, SidecarError> {
        tokio::fs::create_dir_all(&cache_dir)
            .await
            .map_err(|e| SidecarError::Engine(format!("create cache dir {cache_dir:?}: {e}")))?;
        let session = Session::new_with_opts(cache_dir.clone(), options)
            .await
            .map_err(|e| SidecarError::Engine(format!("init librqbit session: {e:#}")))?;
        Ok(Self {
            session,
            cache_dir,
            default_initial_peers,
        })
    }

    /// Validate and normalize an info hash to 40 lowercase hex chars.
    pub fn normalize_info_hash(info_hash: &str) -> Result<String, SidecarError> {
        let hex = info_hash.trim().to_ascii_lowercase();
        if hex.len() != 40 || !hex.chars().all(|c| c.is_ascii_hexdigit()) {
            return Err(SidecarError::BadRequest(format!(
                "info_hash must be a 40-char hex string, got {info_hash:?}"
            )));
        }
        Ok(hex)
    }

    /// Add a torrent, downloading only `file_idx` into `<cache>/<ih40>/`.
    ///
    /// Idempotent: if the info hash is already managed the existing id is
    /// returned without re-resolving metadata (which would otherwise require a
    /// peer). `initial_peers` is the offline discovery mechanism (the control
    /// API always passes `None`; the deterministic tests pass a local seeder).
    pub async fn add(
        &self,
        info_hash: &str,
        file_idx: u64,
        initial_peers: Option<Vec<SocketAddr>>,
    ) -> Result<AddOutcome, SidecarError> {
        let hex = Self::normalize_info_hash(info_hash)?;
        let magnet = format!("magnet:?xt=urn:btih:{hex}");
        let peers = initial_peers.or_else(|| self.default_initial_peers.clone());

        // Idempotent re-add fast path: consult the session's own database by
        // hash before asking librqbit to re-add (a magnet re-add would try to
        // re-resolve metadata from peers first).
        if let Some(id20) = Magnet::parse(&magnet)
            .map_err(|e| SidecarError::BadRequest(format!("invalid info_hash: {e}")))?
            .as_id20()
        {
            if let Some(handle) = self.session.get(TorrentIdOrHash::Hash(id20)) {
                return Ok(AddOutcome {
                    torrent_id: handle.id(),
                    already_managed: true,
                });
            }
        }

        let options = AddTorrentOptions {
            only_files: Some(vec![file_idx as usize]),
            output_folder: Some(self.cache_dir.join(&hex).to_string_lossy().into_owned()),
            overwrite: true,
            initial_peers: peers,
            ..Default::default()
        };

        let response = self
            .session
            .add_torrent(AddTorrent::Url(magnet.into()), Some(options))
            .await
            .map_err(|e| classify_engine_error(&e))?;

        match response {
            AddTorrentResponse::Added(id, _) => Ok(AddOutcome {
                torrent_id: id,
                already_managed: false,
            }),
            AddTorrentResponse::AlreadyManaged(id, _) => Ok(AddOutcome {
                torrent_id: id,
                already_managed: true,
            }),
            AddTorrentResponse::ListOnly(_) => Err(SidecarError::Engine(
                "unexpected list-only response".to_string(),
            )),
        }
    }

    /// Status for one torrent: progress, and — once the selected file is
    /// piece-verified complete — its resolved on-disk path.
    pub async fn status(&self, id: usize) -> Result<TorrentStatus, SidecarError> {
        let handle = self
            .session
            .get(TorrentIdOrHash::Id(id))
            .ok_or_else(|| SidecarError::NotFound(format!("no torrent with id {id}")))?;

        let stats = handle.stats();
        if let librqbit::TorrentStatsState::Error = stats.state {
            return Err(SidecarError::Engine(
                stats.error.unwrap_or_else(|| "torrent errored".to_string()),
            ));
        }

        let file_idx = handle
            .only_files()
            .and_then(|f| f.first().copied())
            .unwrap_or(0);
        let info_hash = handle.info_hash().as_string();
        let progress = if stats.total_bytes > 0 {
            (stats.progress_bytes as f64 / stats.total_bytes as f64).clamp(0.0, 1.0)
        } else {
            0.0
        };

        // Complete gate: metadata resolved + all needed pieces finished + the
        // file exists on disk at the expected length (size-check).
        let mut path: Option<String> = None;
        if stats.finished {
            if let Some(metadata) = handle.metadata.load_full() {
                if let Some(file_info) = metadata.file_infos.get(file_idx) {
                    let full = handle.output_folder().join(&file_info.relative_filename);
                    if let Ok(md) = tokio::fs::metadata(&full).await {
                        if md.len() == file_info.len {
                            path = Some(full.to_string_lossy().into_owned());
                        }
                    }
                }
            }
        }

        Ok(TorrentStatus {
            torrent_id: id,
            info_hash,
            file_idx: file_idx as u64,
            status: if path.is_some() {
                "complete".to_string()
            } else {
                "downloading".to_string()
            },
            progress,
            path,
        })
    }

    /// Evict a torrent and delete its downloaded files.
    pub async fn evict(&self, id: usize) -> Result<(), SidecarError> {
        if self.session.get(TorrentIdOrHash::Id(id)).is_none() {
            return Err(SidecarError::NotFound(format!("no torrent with id {id}")));
        }
        self.session
            .delete(TorrentIdOrHash::Id(id), true)
            .await
            .map_err(|e| classify_engine_error(&e))
    }

    /// Stop the session (pause torrents + cancel background work). The daemon
    /// kills the sidecar process instead, but this is the clean in-process
    /// shutdown used by the resume test between respawns.
    pub async fn shutdown(&self) {
        self.session.stop().await;
    }
}
