# Torrent parity · slice 3, phase 0 — engine decision

- **Executed:** 2026-09-03 (UTC) · **Todo:** `.pi/todos/24a4edc1.md` (research-only, no code)
- **Status:** Decision backed by live upstream evidence; nothing below rests on memory
- **Method:** every claim re-fetched 2026-09-03 from crates.io, GitHub API, and raw upstream source (README, `Cargo.toml`, `session.rs`, `file_info.rs`, `api.rs`, examples). Source list at the end.

## Evaluation frame

Requirements from the slice-3 todo, each verified against engine API: add by **infoHash or magnet** · **select one file by index** · **download to a directory** while allowing **reads mid-download** · macOS **and** Windows · **resume-by-infoHash** · contained dependency weight. Two repo priors frame the verdict: the pure-Rust workspace rule (`docs/rust-poc.md`: greenfield Rust port, `cargo-zigbuild` Windows cross-build) and the tankorent2 autopsy (`02-route-map.md`, `04-instrumentation.md`) — which named PEX, uTP, DHT, and tracker policy as the levers Stremio's JS engine structurally lacks. An engine that already pulls those levers is the strongest candidate, not a tiebreak.

## librqbit (rqbit) — primary candidate

### Identity, license, maintenance (fetched)

| Fact | Value | Source |
|---|---|---|
| Crate / binary | `librqbit` 9.0.1 / rqbit | crates.io, fetched 2026-09-03 |
| **License** | **Apache-2.0** (`Cargo.toml` `license` field + LICENSE file text) | raw `crates/librqbit/Cargo.toml`, `LICENSE` |
| Latest release | 9.0.1, published **2026-08-20** (~2 weeks before this dossier) | crates.io versions API |
| Repository | `ikatson/rqbit`, pushed 2026-09-02, 1,828★, 191 forks, not archived | GitHub API |
| Downloads | 104,393 total · 46,749 recent | crates.io |
| Edition / MSRV | edition **2024** (rustc ≥ 1.85); no `rust-version` pin | `Cargo.toml` |

GitHub's license detector reports NOASSERTION, but the crate manifest and LICENSE file are unambiguous Apache-2.0 — a permissive, workspace-compatible license. **Actively maintained** (two 9.x releases inside the last week of the record), which matters for a network-facing engine: tracker/DHT ecosystem churn lands upstream.

### API fit (verified against `main` source, not docs.rs summaries)

| Requirement | Evidence (file:line/symbol) |
|---|---|
| Add by infoHash / magnet | `Session::add_torrent(AddTorrent::Url(…))`; `session.rs:337` enum `AddTorrent { Url, TorrentFileBytes }`; `SUPPORTED_SCHEMES = [http:, https:, magnet:]`. A bare 40-char hex infoHash is accepted (`len == 40 && Magnet::parse`), and any `magnet:?xt=urn:btih:<hex>` parses; magnet may carry `&dn=`, `&tr=` (trackers), `&so=` (select-only). Local `.torrent` bytes and `http(s)` `.torrent` URLs also supported. |
| Select file by index | `AddTorrentOptions.only_files: Option<Vec<usize>>` (`session.rs:252`) applied at add time; magnet `&so=<idx>`; live re-selection via `Api::api_torrent_action_update_only_files` / `ManagedTorrent.update_only_files`. |
| Download to a directory | `Session::new(dir)` default, or per-add `AddTorrentOptions.output_folder` / `sub_folder` (`session.rs:263-269`). |
| Read-while-downloading | Default storage writes pieces **in place**; streaming is the product's own design — `Api::api_stream(torrent_id, file_id) -> FileStream` (`api.rs:514`) and the HTTP stream route `http://IP:3030/torrents/<id>/stream/<file_id>` with range/seek (README). `FileInfo { relative_filename, offset_in_torrent, piece_range, len }` (`file_info.rs`); per-file piece priority starts first+last piece then fills — the seekable-file trick (`file_info.rs::iter_piece_priorities`). |
| Resume-by-infoHash | `SessionOptions.persistence: Json{folder}` + `fastresume` + per-torrent `overwrite: true` (required to resume an existing download, `session.rs:258`); re-adding a known hash returns `AddTorrentResponse::AlreadyManaged(torrent_id, handle)` (`session.rs:307`). |
| macOS + Windows | rqbit ships Tauri desktop releases for **OSX and Windows** (README: "Download it in Releases for OSX and Windows"); `Cargo.toml` carries `[target.'cfg(windows)']` deps. Pure Rust — no C toolchain, sits on the workspace's existing `cargo-zigbuild` Windows cross line. |
| Dependency weight | Pure-Rust set: `tokio`, `reqwest`, `axum` (optional `http-api` feature), `socket2`, `nix` (unix), `windows` (cfg), plus small internal workspace crates (`bencode`, `dht`, `peer_binary_protocol`, `librqbit-utp`…). No C build. `default-tls` is swappable for `rust-tls`. Feature `disable-upload` exists (see legal posture). |

### Levers the tankorent2 autopsy named — present here, not absent

- **PEX**: `ut_pex::UtPex` send and receive paths (`torrent_state/live/mod.rs:978`, `:1100`).
- **uTP**: `ListenerMode::TcpAndUtp` via `librqbit_utp`.
- **DHT** with persistence and configurable bootstrap, plus **tracker policy is ours** (per-add `trackers`, `disable_trackers`).

All three are things Stremio's engine structurally cannot do (`02-route-map.md` PEX finding, uTP `!1`; `04-instrumentation.md` measured DHT 0). A sidecar on librqbit does not inherit the disease the tankorent2 arc was built to diagnose.

## aria2 — the battle-tested non-Rust fallback

| Fact | Value |
|---|---|
| Identity | C++ downloader, 41.9k★, multi-protocol (HTTP/FTP/BT/Metalink), CLI + JSON-RPC over local HTTP/WebSocket |
| License | **GPL-2.0** (COPYING, GitHub detector `gpl-2.0`) |
| Maintenance | last release **1.37.0, 2023-11-15** (~3 years stale); repo pushed 2026-06-25 |

API equivalents exist: `--dir`, `--select-file=<idx>`, `--continue`, magnet input, JSON-RPC control. As a **separate process** its GPL does not reach our code, but the binary must be built or vendored per-OS (breaking the pure-Rust workspace rule and the zigbuild Windows line), reads require JSON-RPC round-trips, and it has **no uTP**. Verdict: genuine fallback, not a contender — revisit only if librqbit stalls in slice 3's offline proof.

## Other engines — scanned, none displaces

crates.io search (fetched 2026-09-03) for Rust clients/engines: `rusty_torrent`, `cratetorrent`, `mtorrent`/`superseedr`/`xerus`, `fx-torrent`, `irontide`, `rustybit` — CLI-grade or young (all ≤ ~10k downloads; several pre-1.0 or last-published 2020-2024); none offers a library-first API **and** active maintenance **and** shipped macOS/Windows binaries like librqbit. **webtorrent/JS** is the same `torrent-stream` lineage the autopsy dissected and a JS embed collides with the pure-Rust rule. **libtorrent (C++)** has no maintained Rust crate bindings. Not contenders.

## Legal / abuse posture (from the old research)

- The house line is "stay on the safe side of the §1201 anti-circumvention boundary" — Crunchyroll's 2026-03 DMCA against MegaCloud key-extraction tooling, and P-Stream's ALPA DMCA death (`docs/research/theatre-http-source/anime-source-landscape-2026-08-07.md`, `01-existing-addons.md`). None of that touches this decision: we consume the **public BitTorrent protocol** and third-party source rows (`/sources/imdb` candidates) — the same transport the shipped QML app used; no encryption-defeating, no scraping.
- One posture lever is engine-specific: **seeders are distributors**. The sidecar must be a pure downloader — compile librqbit with `disable-upload`, or pin `upload_bps` to 0 — so it never re-uploads spooled content. librqbit makes this a feature flag; aria2 would need a config knob with the same intent.
- `01-existing-addons.md`'s "defer our own engine" verdict governed the **hosted-HTTP lane** (NoTorrent vs P-Stream-style addons), not this one: torrent parity restores a transport the product already ships, and the two lanes coexist on the sources sheet.

## Verdict

**Engine: `librqbit` (rqbit) 9.0.1 — Apache-2.0.** Rationale, in weight order: (1) only candidate that is library-first, actively maintained (release 2 weeks old), and ships macOS+Windows from a pure-Rust build that fits the workspace rule and the zigbuild Windows line; (2) its full API for add-by-magnet/infoHash, `only_files` selection, output-dir download, in-place reads mid-download, and session persistence is verified above — every slice-3 requirement maps to a first-class call, no subprocess glue; (3) it already pulls the PEX/uTP/DHT/tracker levers the tankorent2 autopsy proved Stremio's engine lacks. **License result: Apache-2.0 (permissive), clean to add.** Record in `docs/rust-poc.md` "Library decisions" when slice 3 wires the dependency (this dossier owns no tracked file outside `docs/research/torrent-sidecar-phase0/`). Fallback: aria2 1.37.0 only if the slice-3 offline proof stalls, accepting the binary-dep tradeoff.

### Sources (fetched 2026-09-03)

crates.io API (`/api/v1/crates/librqbit`, `/versions`) · GitHub API `ikatson/rqbit` (repo, contents, releases) · raw files: root `Cargo.toml`, `LICENSE`, `README.md`, `crates/librqbit/Cargo.toml`, `crates/librqbit/src/{lib.rs, session.rs, api.rs, file_info.rs, torrent_state/mod.rs, torrent_state/live/mod.rs}`, `crates/librqbit/examples/{ubuntu.rs, custom_storage.rs, simulate_traffic.rs}` · aria2 GitHub API + `COPYING` + `README.rst` · crates.io search (`q=bittorrent client`, `q=torrent engine`).
