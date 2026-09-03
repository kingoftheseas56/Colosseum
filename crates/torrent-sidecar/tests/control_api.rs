//! End-to-end control-API test against the **real `torrent-sidecar` binary**.
//!
//! Boots the real sidecar binary in the offline posture (`--initial-peers` →
//! a local seeder), reads its `torrent-sidecar.port` file, and drives the HTTP
//! surface with reqwest — the exact same surface `curl` hits. Proves the full
//! loop: token auth → add → poll → piece-verified complete `file://` path →
//! eviction, with a hash-equal check against the fixture bytes.
//!
//! The seeder runs on its own dedicated multi-thread runtime in a separate OS
//! thread: a librqbit seeder session and a reqwest client sharing one runtime
//! starve the client, and the real daemon never runs a seeder in-process
//! either.

use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;

use librqbit::spawn_utils::BlockingSpawner;
use librqbit::{
    create_torrent, AddTorrent, AddTorrentOptions, CreateTorrentOptions, ListenerMode,
    ListenerOptions, Session, SessionOptions,
};
use serde_json::json;
use tokio::sync::oneshot;

fn deterministic_bytes(len: usize) -> Vec<u8> {
    (0..len)
        .map(|i| (i as u8).wrapping_mul(31).wrapping_add(7))
        .collect()
}

/// The seeder's ready payload: everything the sidecar + assertions need.
struct SeederHandle {
    addr: SocketAddr,
    info_hash: String,
    bytes: Vec<u8>,
    /// Kept alive so the seeder's tasks keep serving for the test's lifetime.
    _session: Arc<Session>,
}

/// Seed a single-file fixture on a fresh runtime.
async fn seed_inner(root: &Path) -> SeederHandle {
    let name = "movie.bin";
    let bytes = deterministic_bytes(2 * 1024 * 1024);
    tokio::fs::write(root.join(name), &bytes).await.unwrap();

    let torrent = create_torrent(
        &root.join(name),
        CreateTorrentOptions {
            piece_length: Some(256 * 1024),
            ..Default::default()
        },
        &BlockingSpawner::new(1),
    )
    .await
    .unwrap();

    let session = Session::new_with_opts(
        torrent.output_folder.clone(),
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
            AddTorrent::TorrentFileBytes(torrent.as_bytes().unwrap()),
            Some(AddTorrentOptions {
                overwrite: true,
                output_folder: Some(torrent.output_folder.to_string_lossy().into_owned()),
                ..Default::default()
            }),
        )
        .await
        .unwrap()
        .into_handle()
        .unwrap();

    tokio::time::timeout(Duration::from_secs(30), handle.wait_until_completed())
        .await
        .expect("seeder hash-check finished in time")
        .expect("seeder hash-check succeeded");

    let addr = session.listen_addr().expect("seeder listen addr");
    SeederHandle {
        addr,
        info_hash: torrent.info_hash().as_string(),
        bytes,
        _session: session,
    }
}

/// Run the seeder on its own runtime in a dedicated thread; stop it via the
/// returned sender.
fn spawn_seeder(
    root: PathBuf,
) -> (
    oneshot::Receiver<SeederHandle>,
    oneshot::Sender<()>,
    std::thread::JoinHandle<()>,
) {
    let (ready_tx, ready_rx) = oneshot::channel();
    let (stop_tx, stop_rx) = oneshot::channel();
    let handle = std::thread::spawn(move || {
        let rt = tokio::runtime::Builder::new_multi_thread()
            .enable_all()
            .build()
            .expect("seeder runtime");
        rt.block_on(async move {
            let result = seed_inner(&root).await;
            // The receiver always awaits; a failed send just means the test
            // already panicked.
            let _ = ready_tx.send(result);
            // Keep the seeder serving until the test signals stop.
            let _ = stop_rx.await;
        });
    });
    (ready_rx, stop_tx, handle)
}

async fn read_port_file(path: &Path) -> serde_json::Value {
    let deadline = tokio::time::Instant::now() + Duration::from_secs(15);
    loop {
        if let Ok(bytes) = std::fs::read(path) {
            return serde_json::from_slice(&bytes).expect("port file json");
        }
        if tokio::time::Instant::now() >= deadline {
            panic!("sidecar did not write {} in time", path.display());
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn real_binary_control_api_add_poll_complete_evict() {
    let fixture_root = tempfile::tempdir().unwrap();
    let (seeder_rx, seeder_stop, seeder_thread) = spawn_seeder(fixture_root.path().to_path_buf());
    let seeder = seeder_rx.await.unwrap();
    let info_hash = &seeder.info_hash;
    let expected = &seeder.bytes;

    let root = tempfile::tempdir().unwrap();
    let cache_dir = root.path().join("torrent-cache");
    let port_file_path = root.path().join("torrent-sidecar.port");

    let mut child = tokio::process::Command::new(env!("CARGO_BIN_EXE_torrent-sidecar"))
        .arg("--cache-dir")
        .arg(&cache_dir)
        .arg("--initial-peers")
        .arg(seeder.addr.to_string())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::inherit())
        .spawn()
        .expect("spawn sidecar binary");

    let port_file = read_port_file(&port_file_path).await;
    let port = port_file["port"].as_u64().unwrap() as u16;
    let token = port_file["token"].as_str().unwrap().to_string();

    let http = reqwest::Client::builder()
        .timeout(Duration::from_secs(10))
        .build()
        .unwrap();
    let base = format!("http://127.0.0.1:{port}");

    // Token gate: wrong token is rejected.
    let resp = http
        .post(format!("{base}/torrents"))
        .header("x-sidecar-token", "wrong")
        .json(&json!({ "info_hash": info_hash, "file_idx": 0 }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::UNAUTHORIZED);

    // Missing token is rejected too.
    let resp = http
        .post(format!("{base}/torrents"))
        .json(&json!({ "info_hash": info_hash, "file_idx": 0 }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::UNAUTHORIZED);

    // Correct token: add.
    let resp = http
        .post(format!("{base}/torrents"))
        .header("x-sidecar-token", &token)
        .json(&json!({ "info_hash": info_hash, "file_idx": 0 }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::OK);
    let add: serde_json::Value = resp.json().await.unwrap();
    let torrent_id = add["torrent_id"].as_u64().unwrap() as usize;

    // Poll status until the selected file is piece-verified complete.
    let deadline = tokio::time::Instant::now() + Duration::from_secs(30);
    let path = loop {
        let resp = http
            .get(format!("{base}/torrents/{torrent_id}"))
            .header("x-sidecar-token", &token)
            .send()
            .await
            .unwrap();
        assert_eq!(resp.status(), reqwest::StatusCode::OK);
        let status: serde_json::Value = resp.json().await.unwrap();
        if status["status"] == "complete" {
            break status["path"].as_str().unwrap().to_string();
        }
        if tokio::time::Instant::now() >= deadline {
            panic!("torrent {torrent_id} did not complete: {status}");
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    };

    // Hash-equal: the spooled file matches the fixture bytes.
    assert_eq!(&std::fs::read(&path).unwrap(), expected);

    // Evict deletes the file.
    let resp = http
        .delete(format!("{base}/torrents/{torrent_id}"))
        .header("x-sidecar-token", &token)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), reqwest::StatusCode::OK);
    assert!(!Path::new(&path).exists(), "eviction deletes the file");

    let _ = child.kill().await;
    let _ = child.wait().await;
    let _ = seeder_stop.send(());
    let _ = seeder_thread.join();
}
