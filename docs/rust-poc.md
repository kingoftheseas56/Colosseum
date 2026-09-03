# Rust core POC — architecture and decisions

Record for the `feature/rust-core-poc` branch: the greenfield Rust port that
will grow into the native core. Go (`server/account-service`) remains the
source of truth for account wire shapes, error codes, and domain policy until
each slice is ported and its spec tests go green. Changes to module boundaries
or the dependency set should update this file.

## Current status (2026-09-03)

Workspace members (8): `catalog`, `account`, `addons`, `daemon`, `player`,
`torrent-sidecar`, `ui-widgets`, `ui-gpui`.

Live daemon routes (all on branch HEAD):

| Route | Notes |
|---|---|
| `GET /healthz` `GET /readyz` | readiness |
| `POST /v1/accounts` `POST /v1/sessions` `POST /v1/sessions/refresh` | Go-core port (create/sign-in/refresh; oracle-reconciled) |
| `GET /catalog/search?q=` | seeded offline; live Cinemeta rows when `ADDONS_LIVE=1` |
| `GET /catalog/home` | `continue_watching` + `trending` from the seeded store |
| `GET /catalog/series/{id}` `GET /catalog/series/{id}/sources` | detail + seeded fake-addon sources |
| `GET /catalog/meta/{type}/{tt}` | live Cinemeta detail (live-gated) |
| `GET /sources/imdb/{tt}` | live Torrentio candidates, ranked (live-gated; 404 offline) |
| `POST /torrents/spool` `{candidate_id}` | librqbit sidecar spool → piece-verified `file://` path |

Milestones (each runtime-verified, OCR/hash evidence on record):

- **Cores + smoke**: catalog (SQLite), account (Go port, 8-leg smoke), daemon; windows cross-build green.
- **Native video**: `crates/player` pull contract, macOS AVAssetReader → BGRA `RenderImage`; transport
  boundary `file://`/`http(s)://` only; finding: AVAssetReader rejects remote assets (`-11838`) →
  spool-to-local-file is the required adapter. No audio yet.
- **Torrent lane complete end-to-end** (native Rust, no JS engine): addons layer + ranking (slice 1),
  live Cinemeta + Torrentio providers (slices 2b/3), librqbit sidecar spool (slice 3), and the GPUI
  journey search → detail-by-imdb → SourcesSheet → spool → player (slice 4). Debrid slice deferred
  (needs a provider account). Known gap: >15-min/very-large spools surface `stalled`.
- **UI**: `ui-widgets` kit (theme tokens audited vs `qml/Theme.qml` + `Glass.qml`, poster/rail/grid),
  phase-a shell (Home rails → detail → player), Qt-look restyle (glass chrome, scrims, gold, type
  scale), `Colosseum.app` via cargo-bundle. Legacy single-pane view + dev hooks preserved.
- **Docs**: `docs/parity.md` (24-surface QML→GPUI ledger), torrent-sidecar phase-0 dossier.

Open / next: `player-libmpv` audio/sync lane (nothing can make sound until it lands); Home-from-live
rails + continue-watching writeback (last wave queued); parity B/C breadth per the ledger; account
Go migration slices 1–7 (below).

## Module boundaries

Workspace members are `catalog`, `account`, `daemon` (the domain trio
below), plus `addons`, `player`, `torrent-sidecar`, `ui-widgets` and
`ui-gpui` (see Player architecture / UI decision).

- **Domain crates (`catalog`, `account`) expose a typed contract only**: one
  entry type (`Catalog`, `Service`), the serde DTOs, and a `thiserror` `Error`
  whose variants carry the Go wire code. Everything else — SQL, hashing, storage
  layout — stays module-private so callers cannot couple to it.
- **`catalog` owns its storage.** SQLite behind `rusqlite` (bundled); callers
  pass a path (`Catalog::open`) or ask for in-memory. Search is LIKE-based for
  the core slice; the public API must not leak which engine is behind it so
  tantivy can replace it later without a caller change.
- **`account` knows nothing about HTTP or disks.** It consumes typed requests
  and returns `Session`/`Error`, mirroring `server/account-service` wire shapes
  and error codes. Storage is in-memory for the core slice; persistence is a
  separate roadmap item, not this crate's concern yet.
- **`addons` is the Stremio add-on client layer** (sources + catalog). It owns
  the `Addon` trait, the seeded fixture-backed fakes (the offline default), and
  the live providers behind the daemon's `ADDONS_LIVE=1` flag. Live providers
  are async-only — the sync `Addon` trait can't express network I/O — so
  Cinemeta (search + meta) and Torrentio (streams) implement async seam traits
  (`CatalogSearch`, `StreamSearch`) plus the sync `Addon` trait for install
  identity. Transport is `reqwest` (rustls, no API key; plain HTTPS-JSON).
- **`torrent-sidecar` is the spooler** (torrent-parity slice 3): a separate
  binary owning the librqbit session, downloads, and the cache dir; daemon
  spawns/supervises it. Token-gated loopback control API; spools
  `infoHash+file_idx` to a piece-verified cache file the player loads via
  `file://` (the `-11838` adapter — it never streams http to the player).
  Design: `docs/research/torrent-sidecar-phase0/`.
- **`daemon` is the composition root.** It owns HTTP routing (axum), data-dir
  discovery (`paths.rs` via `directories`), builds `AppState`, and maps domain
  contracts onto routes and status codes. It is the only crate that knows where
  data lives on disk; domains receive paths, never discover them.
- **Dependencies flow one direction: `daemon` → {`catalog`, `account`, `addons`} and
  `ui-gpui` → `player`.** Domain crates never depend on each other and never
  see axum/tokio/directories. An
  inter-domain or domain→daemon dep is a boundary violation; revise this file
  before adding one.

## Library decisions

Chosen in `[workspace.dependencies]`; adding a dependency there should come
with a line here.

| Choice | Rationale |
|---|---|
| `tokio` | async runtime for the daemon; `full` features keep the door open for timers/fs tasks |
| `axum` | typed routes, extractors, and state — daemon maps domain contracts onto HTTP without hand-rolled plumbing |
| `serde` (derive) | JSON (de)serialization of the wire DTOs, matching Go shapes field-for-field |
| `rusqlite` (bundled) | embedded SQLite for the catalog; bundled compile removes per-OS system-lib installs from the build |
| `argon2` | argon2id password hashing, per the Go oracle (`internal/account/password.go`) |
| `rand` | CSPRNG for opaque tokens and salts |
| `uuid` | v4 ids for accounts, devices, challenges |
| `directories` | cross-platform data-dir discovery — daemon only (see `paths.rs`) |
| `reqwest` (rustls, `json`) | async HTTP client for live add-on providers (`crates/addons`); no native TLS, no API key — Cinemeta (search/meta) and Torrentio (streams), both landed |
| `rquickjs` (planned) | embedded JS engine when the daemon must execute addon/provider/extension code (stremio-style addons, qml-era JS glue) — NEVER hand-rolled interpreters or silent re-ports; `boa_engine` as pure-Rust fallback, `deno_core` only if V8 isolation is required |
| `tracing` + `tracing-subscriber` (`env-filter`) | structured logs with runtime filter control via env, defaulting to `daemon=info` |
| `thiserror` | typed domain errors with `Display`; `Error::code()` carries the Go wire vocabulary |
| `time` | RFC 3339 serde/formatting for timestamps, matching the Go service's wire format |
| `librqbit` 9.0.1 (no default-tls, `rust-tls` + `disable-upload`) | torrent engine for the `crates/torrent-sidecar` spooler (torrent-parity slice 3); Apache-2.0, library-first, pure-Rust — the only engine meeting add-by-infoHash, `only_files` selection, download-to-dir, Json persistence/fastresume, and macOS+Windows from one build (`docs/research/torrent-sidecar-phase0/00-engine-decision.md`). `rust-tls` keeps the build ring/rustls-only (no native TLS); `disable-upload` + upload-rate 0 pin the pure-downloader posture (seeders are distributors) |
| `cargo-zigbuild` | cross-compiles `x86_64-pc-windows-gnu` from macOS; landed — `mise run build-windows` produces `daemon.exe` |

Deferred (deliberately not decided yet):

- **sqlx vs rusqlite** — revisit when the account Postgres persistence slice
  lands; rusqlite stays for the local catalog either way.
- **tantivy** — planned full-text search upgrade behind `catalog`'s public API;
  not a dependency until the API is proven storage-agnostic.
- **JWT vs opaque tokens** — sessions currently issue opaque tokens; revisit
  when auth hardening (revocation, expiry policy) is ported.
- **Persistence layer** — account storage is in-memory for the core slice; the
  Go `internal/database` port (below) is where this gets decided.
- **Embedded-JS engine when parity needs it**: `rquickjs` (QuickJS — small,
  fast-start, embeddable) leads; decide the crate BEFORE the extension/addon
  slice starts (torrent-parity slice 1), not during. Prefer Rust re-implementations
  for glue that is small and owned; embed JS only where the ecosystem to reuse
  (addons/extensions) is itself JS. Never invoke a JS interpreter we didn't pick.

### Oracle reconciliation (per TODO.md)

Assumptions in `crates/account` checked against Go while account-core lands;
Go wins on disagreement. Status as of this writing:

- **Access-token TTL**: 15 min — crate constant matches Go
  `accessTokenLifetime = 15 * time.Minute` (`service.go`).
- **Username rules**: 3–24 chars matching `^[A-Za-z0-9](?:[A-Za-z0-9_]{1,22}[A-Za-z0-9])?$`,
  NFC + trim, display vs lowercase canonical, reserved-name list rejected
  (`username.go`).
- **Password policy floor**: NFC-normalized, 8–128 runes, blocklist plus
  context values (`colosseum`, `brotherhood`, username) and their `123`/`1234`
  variants; hashed argon2id (`password.go`).
- **Device-trust acquisition**: the account-creating device is trusted; on
  sign-in a missing/revoked/untrusted device only routes to a challenge when
  `protect_new_device_signins` is on — otherwise it becomes trusted
  (`identity.go`).

Re-run this check at the end of account-core and correct any drift.

## UI decision: GPUI

`gpui` 0.2.2 (crates.io) wins the UI lane. Spike evidence (visual, moving
video): `crates/ui-gpui` fetches `/catalog/search` from the daemon over HTTP
and paints a window with the catalog list beside frames pulled from
`crates/player`'s native backend, uploaded as `RenderImage` on a 10 ms timer
pump. The daemon stays UI-agnostic; `qml/` remains the visual reference to
port, not a runtime dependency.

- **Evaluated, rejected for the app path**: gpui-video-player / GStreamer —
  GStreamer runtime issues on the dev Mac, and the app needs platform-native
  video. GStreamer survives only as `crates/player`'s future Linux backend
  (linux-only dep; never in the macOS app path).
- **Fallbacks if GPUI stalls**: Slint, then Tauri — either is a rewrite of the
  QML visual language, not a drop-in.

**Build consequence**: the UI is a native Rust binary — no Qt, no webview.
Only the daemon cross-builds via zigbuild; the UI builds natively per-OS
(macOS needs the Metal toolchain).

**Parity ledger**: the surface-by-surface QML→GPUI inventory (per top-level
`qml/` surface: reference files, daemon endpoints it needs, GPUI widgets it
implies, phase) is [docs/parity.md](parity.md).

## Player architecture

`crates/player`: one pull-based contract, per-OS decode behind it.

- **Contract** (`Player` trait): `load / play / pause / seek / position /
  duration / next_frame / event`. Pull by design — the backend keeps only the
  newest decoded frame, `next_frame` is latest-wins backpressure, and the UI
  pumps at its own render cadence (~10 ms).
- **NOT `Send`**: objc objects carry thread affinity, so a `Player` is pinned
  to the thread that created it (`player::native()` → `Box<dyn Player>`); it
  never crosses threads.
- **macOS backend = AVAssetReader, not AVPlayer**: AVPlayer +
  `AVPlayerItemVideoOutput` does not advance outside a full app event loop —
  verified with a Swift CLI repro (item status never leaves `unknown`).
  AVAssetReader is synchronous and callback-free: it runs headless and maps
  1:1 onto the pull contract. `copyNextSampleBuffer` is non-blocking — never
  busy-spin on `nil`. (No published objc2 crate covers AVFoundation; the few
  classes used are hand-declared externs and the framework is force-linked.)
- **Frames**: `VideoFrame` is tightly packed BGRA8 (`stride == width * 4`);
  `ui-gpui` wraps the bytes as-is into `RenderImage` — BGRA on the wire, no
  channel swap.
- **Known gaps (POC scope)**: no audio, no A-V sync, software decode (no HW
  accel), no subs, no precise seek.

## IINA lesson + libmpv upgrade lane

Production macOS video apps embed libmpv instead of hand-rolling decode —
IINA (`github.com/iina/iina`) is the reference. Queue a `player-libmpv`
backend (deferred past phase A, TODO-124cbd1e) behind the same contract once
audio/sync/subs matter.

GPUI integration cost, in rising order:

1. **RenderImage CPU upload** (current) — decode → BGRA → GPU texture per frame.
2. **IOSurface / MTLTexture zero-copy** — decoded surfaces pass straight to the
   compositor, skipping the CPU copy.
3. **mpv native layer beside/over the GPUI window** — escape hatch if GPUI
   interop plateaus.

## Migration roadmap

Ported so far: the Go-core surface (`POST /v1/accounts`, `POST /v1/sessions`,
`POST /v1/sessions/refresh` + `healthz`/`readyz`) plus the POC-only catalog/addons
routes above (see Current status). Remaining Go surface from
`server/account-service`, in suggested slice order (each slice = routes +
domain behind them, spec tests green before the next):

1. **Session close-out** — `DELETE /v1/sessions/current` (logout-current),
   `POST /v1/sessions/logout-all`, `POST /v1/sessions/revoke-refresh`
   (`sessions.go`, `devices.go`). Smallest delta; builds on the session/device
   model the core slice already has.
2. **Profile & password change** — `GET /v1/profile`,
   `PATCH /v1/profile/username` (30-day rename cooldown), `POST /v1/password/change`
   (`profile.go`, `password.go`).
3. **Recovery** — `POST /v1/password/recover`,
   `POST /v1/password/trusted-recovery` (+`/poll`), `POST /v1/recovery-key/replace`
   (`recovery.go`, `recovery_service.go`, `session_cipher.go`). Reuses the
   password and session-cipher machinery from 2.
4. **Persistence layer** — port `internal/database` (`postgres.go`,
   `migrate.go`, migrations 0001–0004) and swap the in-memory account store
   behind the `Service` contract. Decide sqlx-vs-rusqlite here.
5. **Security cluster** — device challenges (`/v1/challenges/device/poll`,
   `/recovery-key`), approvals (`GET /v1/approvals`,
   `POST /v1/approvals/{kind}/{challengeID}`), device management
   (`GET /v1/devices`, `DELETE /v1/devices/{deviceID}`),
   `PUT /v1/security/new-device-protection` (`challenges.go`, `identity.go`).
   Needs the durable store from 4 — challenges/approvals are cross-device state.
6. **Avatar store** — `PUT /v1/profile/avatar/builtin`,
   `POST /v1/profile/avatar/upload` (`internal/avatar/store.go`). Standalone.
7. **Sync handlers** — `POST /v1/sync/push`, `GET /v1/sync/pull`
   (`sync_*.go`, `httpserver/sync_handlers.go`); cipher + policy on top of 4.

## The rapid loop

Wired in `.mise.toml`:

- **inner (on save)**: `mise watch -t loop` — `cargo check` then `cargo test`
  on the workspace.
- **gate (pre-commit)**: `mise run lint` — `cargo fmt --check` + clippy with
  warnings fatal.
- **outer (on demand)**: `mise run smoke` — boots the daemon, hits the
  endpoints, kills it.
- **cross**: `mise run build-windows` (zigbuild) / `mise run build-macos`
  (native release).
