//! Offline seeder for manual control-API testing: creates a deterministic
//! single-file fixture torrent (2 MiB) under the given directory, seeds it on
//! `127.0.0.1:0`, prints the seeder address + info hash, then idles forever.
//!
//! ```text
//! cargo run -p torrent-sidecar --example seed -- /tmp/colosseum-seed
//! ```
//!
//! Then boot the sidecar against it:
//!
//! ```text
//! ./target/debug/torrent-sidecar \
//!   --cache-dir /tmp/colosseum-cache/torrent-cache \
//!   --initial-peers 127.0.0.1:<PORT>
//! ```

use std::time::Duration;

use librqbit::spawn_utils::BlockingSpawner;
use librqbit::{
    create_torrent, AddTorrent, AddTorrentOptions, CreateTorrentOptions, ListenerMode,
    ListenerOptions, Session, SessionOptions,
};

#[tokio::main]
async fn main() {
    let dir = std::env::args().nth(1).expect("usage: seed <dir>");
    let dir = std::path::PathBuf::from(dir);
    std::fs::create_dir_all(&dir).expect("create dir");

    let file = dir.join("movie.bin");
    let bytes: Vec<u8> = (0..2 * 1024 * 1024)
        .map(|i| (i as u8).wrapping_mul(31).wrapping_add(7))
        .collect();
    std::fs::write(&file, &bytes).expect("write fixture");

    let torrent = create_torrent(
        &file,
        CreateTorrentOptions {
            piece_length: Some(256 * 1024),
            ..Default::default()
        },
        &BlockingSpawner::new(1),
    )
    .await
    .expect("create torrent");

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
    .expect("seeder session");

    let handle = session
        .add_torrent(
            AddTorrent::TorrentFileBytes(torrent.as_bytes().expect("torrent bytes")),
            Some(AddTorrentOptions {
                overwrite: true,
                output_folder: Some(torrent.output_folder.to_string_lossy().into_owned()),
                ..Default::default()
            }),
        )
        .await
        .expect("add to seeder")
        .into_handle()
        .expect("handle");

    handle
        .wait_until_completed()
        .await
        .expect("seeder hash-check");

    // stderr is unbuffered: the line is immediately visible to a supervisor
    // that pipes this process.
    eprintln!(
        "SEEDER={} INFO_HASH={}",
        session.listen_addr().expect("listen addr"),
        torrent.info_hash().as_string()
    );

    loop {
        tokio::time::sleep(Duration::from_secs(3600)).await;
    }
}
