# Rust core POC — architecture and decisions

Record for the `feature/rust-core-poc` branch: the greenfield Rust port that
will grow into the native core. Go (`server/account-service`) remains the
source of truth for account wire shapes, error codes, and domain policy until
each slice is ported and its spec tests go green. Changes to module boundaries
or the dependency set should update this file.

## Module boundaries

The workspace is three crates: `catalog`, `account`, `daemon`
(`Cargo.toml [workspace]`).

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
- **`daemon` is the composition root.** It owns HTTP routing (axum), data-dir
  discovery (`paths.rs` via `directories`), builds `AppState`, and maps domain
  contracts onto routes and status codes. It is the only crate that knows where
  data lives on disk; domains receive paths, never discover them.
- **Dependencies flow one direction: `daemon` → {`catalog`, `account`}.** Domain
  crates never depend on each other and never see axum/tokio/directories. An
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
| `tracing` + `tracing-subscriber` (`env-filter`) | structured logs with runtime filter control via env, defaulting to `daemon=info` |
| `thiserror` | typed domain errors with `Display`; `Error::code()` carries the Go wire vocabulary |
| `time` | RFC 3339 serde/formatting for timestamps, matching the Go service's wire format |
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

## UI options (decision deferred)

The daemon is UI-agnostic: every option below talks to it over HTTP. Candidates
and their stakes:

- **(a) cxx-qt + QML as a separate UI process** — keeps the cinematic QML
  language the app is built in. The existing `qml/` tree on this branch is the
  porting reference for screens, not a runtime dependency.
- **(b) Slint** — Rust-native declarative UI; one toolchain, but a rewrite of
  the QML visual language.
- **(c) Tauri** — web-tech UI in the system webview; richest ecosystem, furthest
  from the current QML design language.

**Playback plan gates all of it**: prove the libmpv render API through Rust
bindings before any visual investment — the player is the one irreplaceable
native piece, and every UI lane must embed it.

**Build consequence**: whichever lane wins, the UI process links Qt or a
webview and builds natively per-OS. Only the daemon cross-builds via zigbuild;
the UI never does.

## Migration roadmap

Ported so far (`crates/daemon/src/main.rs`): `GET /healthz`, `GET /readyz`,
`POST /v1/accounts`, `POST /v1/sessions`, `POST /v1/sessions/refresh`, plus the
POC-only `GET /catalog/search`. Remaining Go surface from
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
