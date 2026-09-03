//! Native Rust add-on/sources layer — torrent-parity slice 1.
//!
//! The Stremio add-on protocol is plain HTTP-JSON, so the client is
//! Rust-native: no embedded JS engine, no reuse of the QML `.pragma library`
//! module structure. The QML files are *behavior oracles* only — the
//! parse/rank semantics in [`rank`] port their functions one-for-one, and every
//! place this port deliberately diverges is called out in a `// Divergence:`
//! note.
//!
//! # Wire model
//!
//! `GET /catalog/series/{id}/sources` returns (see [`registry::Sources`]):
//!
//! ```json
//! {
//!   "candidates": [
//!     {
//!       "id": "t:aaaaaaaa…:0",
//!       "addon": "Torrentio",
//!       "kind": "torrent",
//!       "label": "Zeta.Release.2022.2160p.WEB-DL…",
//!       "quality": "4K",
//!       "info_hash": "aaaaaaaa…",
//!       "file_idx": 0,
//!       "size_bytes": 19778434547
//!     },
//!     {
//!       "id": "u:https://cdn.example/…",
//!       "addon": "NoTorrent",
//!       "kind": "direct",
//!       "label": "Demo.Series.Beta.S01E01.2160p…",
//!       "quality": "4K",
//!       "url": "https://cdn.example/…",
//!       "size_bytes": 23730033541
//!     }
//!   ],
//!   "counts_by_addon": { "NoTorrent": 3, "Torrentio": 6 },
//!   "installed_addons": [
//!     { "id": "com.stremio.torrentio.addon", "name": "Torrentio", "priority": 0 },
//!     { "id": "org.notorrent.addon", "name": "NoTorrent", "priority": 1 }
//!   ]
//! }
//! ```
//!
//! A candidate is either a torrent row (`info_hash` + `file_idx`, no `url`) or
//! a direct row (`url`, no `info_hash`) — mirroring the transport model in
//! `qml/player2host/Player2Page.qml` (`streamCandidates`, `infoHash`,
//! `fileIdx`). `id` is the JS `_rowKey` (`t:<infoHash>:<fileIdx>` /
//! `u:<url>`) and is stable across restarts. The optional fields (`quality`,
//! `url`, `info_hash`, `file_idx`, `size_bytes`) are omitted from the JSON when
//! absent. `counts_by_addon` keys are add-on names; `installed_addons` is in
//! install (priority) order.
//!
//! One deliberate wire-model divergence from the QML oracle: the QML client
//! funnels direct/debrid/HTTP rows through the torrent play chain by storing
//! `infoHash = "url:<url>"` on those rows. The native wire instead carries the
//! transport explicitly — a `kind: "direct" | "torrent"` discriminator plus
//! either `url` or `info_hash` + `file_idx` — which is the slice's candidate
//! shape. The `u:`/`t:` prefix still survives in each candidate's `id` (the
//! JS `_rowKey`), so direct-row identity keeps the same routing-prefix idea.
//!
//! # Ranking precedence
//!
//! [`rank::compare`] orders candidates quality (4K → 1080p → 720p → 480p →
//! SD) → seeders (descending, unknown -1 last) → release (ascending) →
//! language (fewer languages first, then ISO codes) → add-on install priority
//! (ascending). The QML oracle (`AddonClient._sortRows`) instead leads with
//! install priority; that is the documented burial bug this slice fixes.
//!
//! # Live client (`ADDONS_LIVE`)
//!
//! [`Registry::seeded`] wires the two fixture-backed fakes (the offline
//! default). When the daemon runs with `ADDONS_LIVE=1`, it additionally
//! constructs a live [`providers::cinemeta::Cinemeta`] — Stremio's official
//! catalog add-on — and a live [`providers::torrentio::Torrentio`] — the real
//! torrent stream add-on. Cinemeta is consulted for `/catalog/search`,
//! returning real IMDb `tt` ids and names through the async
//! [`providers::CatalogSearch`] trait; Torrentio answers a `tt` id with real
//! torrent candidates through [`providers::StreamSearch`], and the daemon
//! hands the fetched `{ "streams": [...] }` body to [`sources_for_addon`] so
//! it flows through the same [`rank::parse`] / [`rank::sort_rows`] pipeline as
//! the seeded fakes.

pub mod manifest;
pub mod providers;
pub mod rank;
pub mod registry;
pub mod stream;

pub use manifest::Manifest;
pub use providers::{CatalogSearch, Cinemeta, LiveError, MetaPreview, StreamSearch, Torrentio};
pub use rank::{compare, sort_rows, Kind, Quality, RankedStream};
pub use registry::{sources_for_addon, Addon, Candidate, InstalledAddon, Registry, Sources};
pub use stream::{BehaviorHints, Stream, StreamResponse};
