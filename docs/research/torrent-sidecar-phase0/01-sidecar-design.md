# Torrent parity · slice 3, phase 0 — sidecar design (`crates/torrent-sidecar`, future)

- **Executed:** 2026-09-03 (UTC) · **Todo:** `.pi/todos/24a4edc1.md` (research-only) · **Status:** Architecture spec; slice-3 implementation todo derived from it
- **Inputs:** `00-engine-decision.md` (librqbit), the slice-2 finding below, the verified addons wire model (`crates/addons/src/rank.rs`, `registry.rs`), the resume payload in `qml/PlayerPage.qml`, data-dir convention in `crates/daemon/src/paths.rs`.

## Slice-2's finding comes first: the player path is `file://`, not http

`crates/player/src/avfoundation.rs` (probe 2026-09-03, commit 8901215): `AVURLAsset` loads tracks and duration over `http://127.0.0.1`, but `AVAssetReader(asset:)` fails with **`AVFoundationErrorDomain -11838`** (`AVErrorOperationNotSupportedForAsset`) — an asset-type rejection, not a headless artifact, so it fails in an app context too. AVPlayer would stream http but never advances without a full app event loop (the original headless lesson).

**Simplification this buys:** the sidecar is a **spooler to local cache files**, not a loopback media server. No Range/http server is needed for the player path — delete that from slice-3 scope. The daemon hands `player.load(file://<path>)` a **complete, static** file, gated on the selected file being piece-verified complete, so AVAssetReader always sees a fully seekable immutable file. librqbit's read-while-downloading machinery (FileStream / `/torrents/<id>/stream/<file_id>`) is deliberately **not** used here; it stays for progress reporting and the future libmpv lane (`docs/rust-poc.md` player-libmpv), which can speak http+Range and may later relax the spool-to-complete gate. Loopback http may return as an optional debug/other-consumer surface then — never for the AVAssetReader path.

## Control API — loopback HTTP on `127.0.0.1` (picked over unix socket)

1. **Windows.** The daemon cross-builds `x86_64-pc-windows-gnu` (`docs/rust-poc.md` zigbuild). Unix sockets do not exist there; a named-pipe special case for one local consumer is not worth it. TCP loopback is one code path everywhere.
2. **Stack reuse.** Daemon and sidecar both speak axum/reqwest today; typed JSON both ends, `curl`-able for debugging, and the daemon's own error-envelope idiom `{"error":{"code","message"}}` carries over.
3. **Observability.** Same reason the daemon's own API is loopback HTTP (its router binds `127.0.0.1`).

Port/lifecycle mechanics: the sidecar binds `127.0.0.1:0` (ephemeral) and writes its actual port **plus a per-run random token** to `<data_dir>/torrent-sidecar.port` (mode 0600) before serving; the daemon spawns it and reads that file. Auth: strict loopback bind **and** `X-Sidecar-Token` on every request — a torrent engine on loopback is a local attack surface (any local process could otherwise ask it to download arbitrary hashes). No CORS (no browser origin). Port is not fixed, so no 11470-style adoption hazard (see the port dissection in `00-specimen.md`).

## Lifecycle — daemon spawns; sidecar owns downloads

- **Daemon spawns** the sidecar binary lazily on the first torrent-kind play/spool request (one sidecar per data dir) and supervises it: restart once on crash if work is pending, kill on shutdown. The daemon is already the process owner and the only crate that knows where data lives (`paths.rs`).
- **Sidecar owns** the librqbit session, the downloads, and the cache dir. Handoff is argv/env only (`--cache-dir <path>`, `RUST_LOG`) — no config file; the port file doubles as the single-instance guard.

## Spool flow: info_hash + file_idx → cache file → `player.load(file://…)`

Wire model verified in-repo: a torrent candidate is `Candidate { id, info_hash, file_idx, … }` where `id = "t:<lowercased info_hash>:<file_idx>"` (`rank.rs::row_key`); direct rows carry `url` and never touch the sidecar.

1. **Add** — daemon → `POST /torrents {info_hash, file_idx}`. Sidecar builds `magnet:?xt=urn:btih:<hex>` (the wire model carries no name/trackers) and calls `Session::add_torrent` with `AddTorrentOptions { only_files: [file_idx], output_folder: <cache>/<ih40>/, overwrite: true }` — `only_files` is the selection lever (`&so=` on the magnet is redundant: librqbit only consults it when `only_files` is absent). Response `{torrent_id}`; re-add of a known hash returns the existing id (`AlreadyManaged`).
2. **Wait** — daemon polls `GET /torrents/{id}` at ~1 Hz (the app's own stats cadence) for status/progress.
3. **Complete gate** — the selected file's pieces complete → sidecar resolves the real path under `<cache>/<ih40>/` from metadata (`FileInfo.relative_filename`: single-file torrent → direct child; multi-file → `name/…` subfolder), size-checks it, returns `{status:"complete", path}`.
4. **Play** — daemon hands `file://<path>` to `player.load`.

Layout under the `io.Brotherhood.Colosseum` data dir (`directories` crate, `paths.rs:data_dir()`): `<data_dir>/torrent-cache/<info_hash_40>/…`. One dir per infoHash → resume, eviction, and human inspection are all trivial. Concurrent plays = multiple torrents in one sidecar session.

## 1:1 mapping — `/sources/imdb` candidates → add commands

Both the live `GET /sources/imdb/{tt_id}` route and the offline `GET /catalog/series/{id}/sources` return the same `addons::Sources` shape (candidates ranked quality→seeders→release→language). The mapping is row-for-row:

| Candidate | Sidecar action |
|---|---|
| `kind: Torrent` (`info_hash`, `file_idx` present) | `POST /torrents {info_hash, file_idx}` — one add per candidate |
| `kind: Direct` (`url` present) | never the sidecar — player loads the url directly |

The row's `id` (`t:<ih>:<file_idx>`) is the natural client key: re-picking the same ranked candidate is idempotent (`already_managed`), so switching a stream back and forth never double-downloads. Add-on id/priority are UI concerns; the sidecar never sees them.

## Cache policy

- **Location:** under the data dir (daemon is the only crate that discovers paths; the sidecar receives them — same convention as every domain crate).
- **Resume:** partial downloads stay in their `<ih40>/` dir; on sidecar restart the session persists (`SessionPersistenceConfig::Json`) and re-adds by infoHash (`fastresume`, `overwrite: true`). A completed file on disk = the resume fast path: status `complete` with zero network.
- **Eviction:** LRU over completed torrent dirs against a byte cap (default 20 GB, env-tunable). In-flight torrents are never evicted; eviction deletes the dir. Progress/resume state is cheap and kept.

## RESUME path (old-app parity)

The shipped app resumes with `mediaResumeHash` (infoHash) + `mediaResumeFileIdx` + `position`, persisted as `{"resume":{"infoHash","fileIdx","localPath","position"}}` (`qml/PlayerPage.qml:2034`). Reopen: if the cache holds the completed file → `player.load(file://…)` immediately and seek to `position`; otherwise the same `POST /torrents` → wait → play. Resume is therefore not a new command — it is the cache-hit fast path of a fresh play. (The legacy `localPath` field is not trusted as a source of truth; the sidecar re-resolves the path from infoHash+fileIdx.)

## Error taxonomy

Same envelope as the daemon: `{"error":{"code","message"}}`. Status codes: 400 bad_request · 404 not_found (unknown torrent id on status/delete) · 409 already_managed (informational, idempotent re-add) · 502 engine_error (librqbit API/storage failure) · 503 metadata_unavailable (magnet add could not fetch metadata from the swarm) · 504 stalled (watchdog: 0 peers / no progress). Daemon-side, a dead/unreachable sidecar is `sidecar_unavailable` with restart-and-retry-once.

## Offline vs network — what slice 3 must prove without the internet

**Offline (deterministic gates):** the test helper builds a small fixture torrent with librqbit's own `create_torrent` (1–4 MiB random bytes, single-file *and* multi-file layouts) and a second **in-process** librqbit session listens on `127.0.0.1:0` and seeds it. The sidecar session runs DHT off and trackers off; each add passes `initial_peers: [127.0.0.1:<seeder port>]` — the only discovery mechanism when both discovery systems are off. This proves: add-by-infoHash, `only_files` selection, download-to-dir, duplicate-add idempotence, resume across a sidecar kill+respawn, eviction, path resolution for both file layouts, full control-API round trips with the error envelope, and — on macOS — `player.load(file://…)` of the spooled file. Hash-equality closes the loop.

**Network-only (live smoke, never a deterministic gate — house rule, mirroring tankorent2's instrumentation discipline):** magnet metadata fetch from a real swarm, tracker/DHT discovery, real-world peer behavior, and the `metadata_unavailable`/`stalled` taxonomy entries. These get a manual/live label in the slice, exactly like the tankorent2 throughput measurements that were withheld pending a quiet machine.

## Deferred

The libmpv player lane (`player-libmpv`, TODO-124cbd1e) can consume loopback http + Range, at which point the spool-to-complete gate may relax to librqbit piece-priority streaming — the engine support (FileStream, stream route) already exists and this design keeps the control API shape that would admit it. The AVAssetReader path stays `file://`-only. The player's http backend (slice 2) remains for direct/debrid URLs, never torrent bytes.
