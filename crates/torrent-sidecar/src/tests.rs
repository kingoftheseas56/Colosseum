//! Offline deterministic gates for the torrent sidecar.
//!
//! Never network-gated: fixture torrents are built with librqbit's own
//! `create_torrent` (1–4 MiB random bytes, single-file and multi-file
//! layouts), a second in-process librqbit session on `127.0.0.1:0` seeds it,
//! and the sidecar client adds with `initial_peers` — the only discovery
//! mechanism when DHT and trackers are off. This proves: add → complete →
//! hash-equal, `only_files` selection, duplicate-add idempotence, resume
//! across kill+respawn (Json fastresume), and eviction.

use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;

use librqbit::spawn_utils::BlockingSpawner;
use librqbit::{
    create_torrent, AddTorrent, AddTorrentOptions, CreateTorrentOptions, CreateTorrentResult,
    ListenerMode, ListenerOptions, Session, SessionOptions,
};

use crate::engine::{Engine, TorrentStatus};

const PIECE_LENGTH: u32 = 256 * 1024;

/// A source fixture: the original files + the `.torrent` describing them.
struct Fixture {
    /// Owns the source tree so it outlives the seeder.
    _root: tempfile::TempDir,
    torrent: CreateTorrentResult,
    /// `(relative_filename, bytes)` for every file in the torrent.
    files: Vec<(String, Vec<u8>)>,
    info_hash_hex: String,
}

/// A live seeder session bound to loopback, seeding the fixture.
struct Seeder {
    #[allow(dead_code)]
    session: Arc<Session>,
    addr: std::net::SocketAddr,
}

fn random_bytes(len: usize) -> Vec<u8> {
    let mut v = vec![0u8; len];
    rand::fill(&mut v[..]);
    v
}

async fn write(path: &Path, bytes: &[u8]) {
    if let Some(parent) = path.parent() {
        tokio::fs::create_dir_all(parent).await.unwrap();
    }
    tokio::fs::write(path, bytes).await.unwrap();
}

async fn single_file_fixture() -> Fixture {
    let root = tempfile::tempdir().unwrap();
    let name = "movie.bin";
    let bytes = random_bytes(2 * 1024 * 1024); // 2 MiB
    write(&root.path().join(name), &bytes).await;

    let torrent = create_torrent(
        &root.path().join(name),
        CreateTorrentOptions {
            piece_length: Some(PIECE_LENGTH),
            ..Default::default()
        },
        &BlockingSpawner::new(1),
    )
    .await
    .unwrap();

    Fixture {
        info_hash_hex: torrent.info_hash().as_string(),
        torrent,
        files: vec![(name.to_string(), bytes)],
        _root: root,
    }
}

async fn multi_file_fixture() -> Fixture {
    let root = tempfile::tempdir().unwrap();
    let dir = root.path().join("multi");
    let files = vec![
        ("a.bin".to_string(), random_bytes(1024 * 1024)),
        ("sub/b.bin".to_string(), random_bytes(1024 * 1024 + 1234)),
        ("c.bin".to_string(), random_bytes(512 * 1024)),
    ];
    for (rel, bytes) in &files {
        write(&dir.join(rel), bytes).await;
    }

    let torrent = create_torrent(
        &dir,
        CreateTorrentOptions {
            piece_length: Some(PIECE_LENGTH),
            ..Default::default()
        },
        &BlockingSpawner::new(1),
    )
    .await
    .unwrap();

    Fixture {
        info_hash_hex: torrent.info_hash().as_string(),
        torrent,
        files,
        _root: root,
    }
}

/// Seed a fixture: add the `.torrent` bytes with `overwrite: true` and the
/// create_torrent output folder, listen on loopback, wait until the initial
/// hash-check marks the existing files complete.
async fn seed(fixture: &Fixture) -> Seeder {
    let session = Session::new_with_opts(
        fixture.torrent.output_folder.clone(),
        SessionOptions {
            dht: None,
            disable_trackers: true,
            ipv4_only: true,
            listen: Some(ListenerOptions {
                mode: ListenerMode::TcpOnly,
                listen_addr: "127.0.0.1:0".parse().unwrap(),
                ipv4_only: true,
                ..Default::default()
            }),
            ..Default::default()
        },
    )
    .await
    .unwrap();

    let handle = session
        .add_torrent(
            AddTorrent::TorrentFileBytes(fixture.torrent.as_bytes().unwrap()),
            Some(AddTorrentOptions {
                overwrite: true,
                output_folder: Some(fixture.torrent.output_folder.to_string_lossy().into_owned()),
                ..Default::default()
            }),
        )
        .await
        .unwrap()
        .into_handle()
        .unwrap();

    // Wait until the seeder has hash-checked the on-disk data and can serve.
    tokio::time::timeout(Duration::from_secs(30), handle.wait_until_completed())
        .await
        .expect("seeder hash-check finished in time")
        .expect("seeder hash-check succeeded");

    let addr = session.listen_addr().expect("seeder listen addr");
    Seeder { session, addr }
}

async fn wait_complete(engine: &Engine, id: usize) -> TorrentStatus {
    for _ in 0..600 {
        let status = engine.status(id).await.expect("status");
        if status.status == "complete" {
            return status;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    panic!("torrent {id} did not complete within the timeout");
}

/// A fresh cache dir + offline engine.
async fn offline_engine() -> (tempfile::TempDir, Engine) {
    let dir = tempfile::tempdir().unwrap();
    let engine = Engine::new_offline(dir.path().to_path_buf()).await.unwrap();
    (dir, engine)
}

#[tokio::test]
async fn single_file_add_completes_and_matches_fixture_bytes() {
    let fixture = single_file_fixture().await;
    let seeder = seed(&fixture).await;
    let (cache, engine) = offline_engine().await;

    let outcome = engine
        .add(&fixture.info_hash_hex, 0, Some(vec![seeder.addr]))
        .await
        .unwrap();
    assert!(!outcome.already_managed);

    let status = wait_complete(&engine, outcome.torrent_id).await;
    assert_eq!(status.status, "complete");
    let path = PathBuf::from(status.path.expect("complete path"));
    // The file lives under <cache>/<ih40>/<relative_filename>.
    assert_eq!(
        path,
        cache
            .path()
            .join(&fixture.info_hash_hex)
            .join(&fixture.files[0].0)
    );
    assert_eq!(std::fs::read(&path).unwrap(), fixture.files[0].1);
}

#[tokio::test]
async fn multi_file_only_files_downloads_just_the_chosen_file() {
    let fixture = multi_file_fixture().await;
    let seeder = seed(&fixture).await;
    let (cache, engine) = offline_engine().await;

    // The torrent's file indices follow the fixture directory walk order, which
    // we deliberately don't pin, so pick an index and resolve *which* file it is
    // from the complete path (order-independent).
    let chosen = 1usize;
    let outcome = engine
        .add(
            &fixture.info_hash_hex,
            chosen as u64,
            Some(vec![seeder.addr]),
        )
        .await
        .unwrap();
    let status = wait_complete(&engine, outcome.torrent_id).await;
    assert_eq!(status.status, "complete");
    assert_eq!(status.file_idx as usize, chosen);

    let base = cache.path().join(&fixture.info_hash_hex);
    let full_path = PathBuf::from(status.path.expect("complete path"));
    let chosen_rel = full_path
        .strip_prefix(&base)
        .expect("resolved path under the cache dir")
        .to_path_buf();
    let (_, chosen_bytes) = fixture
        .files
        .iter()
        .find(|(rel, _)| Path::new(rel.as_str()) == chosen_rel.as_path())
        .expect("chosen file is one of the fixture files");
    assert_eq!(std::fs::read(&full_path).unwrap(), *chosen_bytes);

    // `only_files` selects the *pieces* intersecting the chosen file, so a
    // piece that straddles a file boundary spills a fragment into a neighbor.
    // The guarantee is: every non-chosen file stays strictly incomplete (never
    // its full length) — only the chosen file is piece-verified complete.
    for (rel, bytes) in &fixture.files {
        if Path::new(rel.as_str()) == chosen_rel.as_path() {
            continue;
        }
        let len = std::fs::metadata(base.join(rel))
            .map(|m| m.len())
            .unwrap_or(0);
        assert!(
            len < bytes.len() as u64,
            "non-selected file {rel:?} must not be fully downloaded (len {len}, full {})",
            bytes.len()
        );
    }
}

#[tokio::test]
async fn duplicate_add_is_idempotent_even_without_peers() {
    let fixture = single_file_fixture().await;
    let seeder = seed(&fixture).await;
    let (_cache, engine) = offline_engine().await;

    let first = engine
        .add(&fixture.info_hash_hex, 0, Some(vec![seeder.addr]))
        .await
        .unwrap();
    assert!(!first.already_managed);
    wait_complete(&engine, first.torrent_id).await;

    // Re-add without any peers: the session-db lookup must return the existing
    // id idempotently, without re-resolving metadata from the swarm.
    let second = engine.add(&fixture.info_hash_hex, 0, None).await.unwrap();
    assert!(second.already_managed);
    assert_eq!(second.torrent_id, first.torrent_id);
}

#[tokio::test]
async fn resume_across_kill_and_respawn_returns_complete_fast_path() {
    let fixture = single_file_fixture().await;
    let seeder = seed(&fixture).await;
    let (cache, engine) = offline_engine().await;

    let outcome = engine
        .add(&fixture.info_hash_hex, 0, Some(vec![seeder.addr]))
        .await
        .unwrap();
    let id = outcome.torrent_id;
    wait_complete(&engine, id).await;
    engine.shutdown().await;
    drop(engine);

    // Json persistence must have recorded the session + metadata + bitfield.
    let cache_root = cache.path();
    assert!(
        cache_root.join("session.json").exists(),
        "session.json persisted"
    );
    assert!(
        cache_root
            .join(format!("{}.torrent", fixture.info_hash_hex))
            .exists(),
        "torrent metadata persisted"
    );

    // Respawn a fresh engine on the same cache dir: the session auto-restores
    // the torrent (metadata from the persisted .torrent, bitfield from
    // fastresume), and a re-add returns the same id without any peers.
    let engine2 = Engine::new_offline(cache_root.to_path_buf()).await.unwrap();
    let re_add = engine2.add(&fixture.info_hash_hex, 0, None).await.unwrap();
    assert!(re_add.already_managed);
    assert_eq!(re_add.torrent_id, id);

    let status = wait_complete(&engine2, id).await;
    assert_eq!(status.status, "complete");
    assert_eq!(
        std::fs::read(status.path.unwrap()).unwrap(),
        fixture.files[0].1
    );
}

#[tokio::test]
async fn eviction_deletes_the_downloaded_file() {
    let fixture = single_file_fixture().await;
    let seeder = seed(&fixture).await;
    let (cache, engine) = offline_engine().await;

    let outcome = engine
        .add(&fixture.info_hash_hex, 0, Some(vec![seeder.addr]))
        .await
        .unwrap();
    let status = wait_complete(&engine, outcome.torrent_id).await;
    let path = PathBuf::from(status.path.unwrap());
    assert!(path.exists());

    engine.evict(outcome.torrent_id).await.unwrap();

    assert!(!path.exists(), "eviction deletes the downloaded file");
    // The torrent is gone from the session: status now 404s, and the
    // `<cache>/<ih40>/` dir holds no downloaded file.
    let err = engine.status(outcome.torrent_id).await.unwrap_err();
    assert_eq!(err.code(), "not_found");
    let base = cache.path().join(&fixture.info_hash_hex);
    if base.exists() {
        assert!(
            std::fs::read_dir(&base).unwrap().next().is_none(),
            "eviction leaves the torrent dir empty"
        );
    }
}

#[tokio::test]
async fn invalid_info_hash_is_rejected_as_bad_request() {
    let (_cache, engine) = offline_engine().await;
    let err = engine.add("not-hex", 0, None).await.unwrap_err();
    assert_eq!(err.code(), "bad_request");
}

// ── control-API surface (in-process router) ─────────────────────────────────

use axum::body::Body;
use axum::http::{Request, StatusCode};
use tower::ServiceExt;

fn http_request(method: &str, uri: &str, token: Option<&str>, body: &str) -> Request<Body> {
    let mut builder = Request::builder().method(method).uri(uri);
    if let Some(t) = token {
        builder = builder.header("x-sidecar-token", t);
    }
    builder
        .header("content-type", "application/json")
        .body(Body::from(body.to_string()))
        .unwrap()
}

async fn http_body_json(response: axum::response::Response) -> serde_json::Value {
    let bytes = axum::body::to_bytes(response.into_body(), usize::MAX)
        .await
        .unwrap();
    serde_json::from_slice(&bytes).unwrap()
}

#[tokio::test]
async fn control_api_re_add_is_409_and_status_reports_complete_path() {
    let fixture = single_file_fixture().await;
    let seeder = seed(&fixture).await;
    let (_cache, engine) = offline_engine().await;

    // Add + complete via the engine (offline peers) so the control API sees an
    // already-managed torrent.
    let outcome = engine
        .add(&fixture.info_hash_hex, 0, Some(vec![seeder.addr]))
        .await
        .unwrap();
    wait_complete(&engine, outcome.torrent_id).await;

    let app = crate::http::router(engine, "tok".into());

    // Re-add → 409 already_managed with the same id.
    let response = app
        .clone()
        .oneshot(http_request(
            "POST",
            "/torrents",
            Some("tok"),
            &format!(
                r#"{{"info_hash":"{}","file_idx":0}}"#,
                fixture.info_hash_hex
            ),
        ))
        .await
        .unwrap();
    assert_eq!(response.status(), StatusCode::CONFLICT);
    let json = http_body_json(response).await;
    assert_eq!(json["error"]["code"], "already_managed");
    assert_eq!(json["torrent_id"], outcome.torrent_id as u64);

    // Status → complete with a resolved path.
    let response = app
        .oneshot(http_request(
            "GET",
            &format!("/torrents/{}", outcome.torrent_id),
            Some("tok"),
            "",
        ))
        .await
        .unwrap();
    assert_eq!(response.status(), StatusCode::OK);
    let json = http_body_json(response).await;
    assert_eq!(json["status"], "complete");
    assert!(json["path"].is_string());
    assert_eq!(json["file_idx"], 0);
}
