//! `torrent-sidecar` binary — the sidecar process the daemon spawns.
//!
//! Args: `--cache-dir <data_dir>/torrent-cache` (required) and optional
//! `--initial-peers <addr,addr,…>` (selects the offline posture: DHT/trackers
//! off, initial peers as the only discovery — the deterministic path for a
//! local fixture seeder). On boot: open the librqbit session, bind the control
//! API to `127.0.0.1:0`, write the real port plus a per-run random token to
//! `<data_dir>/torrent-sidecar.port` (0600), then serve. `RUST_LOG` controls
//! tracing (default `torrent_sidecar=info`).

use std::net::SocketAddr;
use std::path::PathBuf;

use torrent_sidecar::port_file;
use torrent_sidecar::Engine;

fn parse_args() -> (PathBuf, Option<Vec<SocketAddr>>) {
    let mut args = std::env::args().skip(1);
    let mut cache_dir = None;
    let mut initial_peers: Option<Vec<SocketAddr>> = None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--cache-dir" => {
                cache_dir = Some(args.next().unwrap_or_else(|| {
                    eprintln!("--cache-dir requires a path");
                    std::process::exit(2);
                }));
            }
            "--initial-peers" => {
                let value = args.next().unwrap_or_else(|| {
                    eprintln!("--initial-peers requires a comma-separated list");
                    std::process::exit(2);
                });
                let peers = value
                    .split(',')
                    .filter(|p| !p.is_empty())
                    .map(|p| {
                        p.parse::<SocketAddr>().unwrap_or_else(|_| {
                            eprintln!("invalid --initial-peers entry: {p}");
                            std::process::exit(2);
                        })
                    })
                    .collect::<Vec<_>>();
                initial_peers = Some(peers);
            }
            other => {
                eprintln!("unknown argument: {other}");
                std::process::exit(2);
            }
        }
    }
    let Some(cache_dir) = cache_dir else {
        eprintln!("usage: torrent-sidecar --cache-dir <path> [--initial-peers <addr,addr,…>]");
        std::process::exit(2);
    };
    (cache_dir.into(), initial_peers)
}

/// A 256-bit random token, hex-encoded.
fn random_token() -> String {
    let bytes: [u8; 32] = rand::random();
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "torrent_sidecar=info".into()),
        )
        .init();

    let (cache_dir, initial_peers) = parse_args();

    // Supplying `--initial-peers` selects the offline posture (DHT/trackers
    // off, initial peers as the only discovery) — the deterministic path for
    // a local fixture seeder. Without it the sidecar runs the live posture.
    let engine = match initial_peers {
        Some(peers) => Engine::new_offline_with_peers(cache_dir.clone(), Some(peers))
            .await
            .expect("init offline engine"),
        None => Engine::new(cache_dir.clone()).await.expect("init engine"),
    };
    let listener = tokio::net::TcpListener::bind(("127.0.0.1", 0))
        .await
        .expect("bind loopback");
    let port = listener.local_addr().expect("local addr").port();
    let token = random_token();
    let port_file =
        port_file::write_port_file(&cache_dir, port, &token).expect("write port/token file");

    tracing::info!(?port_file, port, "sidecar control API on loopback");

    axum::serve(listener, torrent_sidecar::http::router(engine, token))
        .await
        .expect("serve");
}
