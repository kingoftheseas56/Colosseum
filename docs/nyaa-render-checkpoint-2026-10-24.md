# Nyaa metadata hosting checkpoint — 2026-10-24

Superseded by the user's October 25 decision: [Nyaa is native again](nyaa-native-checkpoint-2026-10-25.md). This receipt preserves the Render experiment; the app no longer uses Render for Nyaa search.

Render successfully runs the existing Nyaa search handler and the production Windows app completed live search followed by native torrent retrieval, hash verification and file indexing. The service remains a free qualification instance. Its idle-start behavior is a release gate; no release or master merge was performed.

## Hosting decision

Two fresh, spaced probes reproduced the Worker receiving upstream HTTP 429 while matching direct Nyaa RSS requests returned HTTP 200. This supports an egress-dependent restriction, but does not establish Nyaa's exact rule. No alternate Nyaa origin, challenge bypass, rotating proxy or direct native RSS fallback was added.

The portable Node adapter reuses `createWorker` unchanged: the same fixed RSS origin, bounded request/upstream bodies, deadlines, parser, versioned JSON contract and canonical torrent URLs. It streams request and response bodies with backpressure, bounds headers, forwards client disconnect cancellation and drains on shutdown. Torrent files and media are never fetched by the hosted handler.

- Workspace: user-selected My Workspace; isolated service `colosseum-nyaa-metadata`.
- [Service dashboard](https://dashboard.render.com/web/srv-dbe7urks728c73bavlm0); service ID `srv-dbe7urks728c73bavlm0`.
- [Public health](https://colosseum-nyaa-metadata.onrender.com/healthz); search route `/v1/nyaa/search`; [manifest](https://colosseum-nyaa-metadata.onrender.com/manifest.json).
- Free Node service in Singapore; Node `22.23.4`; automatic deploys disabled; existing services untouched.
- Deployment `dep-dbe7uscs728c73bavo0g` reached `live` from adapter commit `1163fbb21394be4e2b3ba19aae57910f23f94aad` on `codex/tankoyomi-workers-checkpoint`.
- Health and manifest returned HTTP 200, contract `1`, revision `1.0.0`, stable extension ID `colosseum.well.nyaa`. Two distinct real searches returned HTTP 200 with 7 and 75 rows.

The app's fixed search endpoint and built-in manifest now point to Render. Defaults generation 18 migrates only the exact original native built-in URL or prior built-in Worker URL. Enablement, order, identity, installed timestamps and custom URLs are preserved; Nyaa remains disabled on a fresh install. Native query planning, trust, ranking, local identity, metainfo retrieval/hash checking, file indexing, libtorrent and payload downloads keep their existing owners.

## Verification

- Shared handler and Node HTTP adapter: 20 tests passed, including real local HTTP, request/header bounds, structured errors, timeouts and graceful draining.
- Extension first-run/migration Qt tests: 41 passed. Generation 16/17 cases cover both old built-in URLs and a custom URL, each enabled and disabled, plus ordering/timestamp/identity and persisted generation 18 checks.
- Production Windows `colosseum_runtime_ready` build passed with real libtorrent. Executable SHA-256: `48AE84FA2EF6C7EE2D886494917B5398668E85BA879F0890C6528E9B5EA401F1`. QML fingerprint: `1d6a08e55c64219e885f880c9260c71887fbd35e01259ba733afc89dd77a8cb6`.
- The real Main/series/embedded Sources page ran in an isolated profile, offscreen, without an account, with Nyaa explicitly enabled. Only One Piece series/Vol. 1 catalogue metadata was supplied through `TankobanVolumes.prepareSeries`; no provider rows, service responses, torrent bytes or mappings were substituted. The separate fresh-profile live catalogue route remains unqualified.
- The app driver exited 0, rendered 11 live sources and saved a screenshot that was inspected. Native verification produced hash `fde15e8e87d7c32e7e92e9ad7a6ca094f4378053`, file index `0`, path `Volume 1 - Romance Dawn.cbz`, evidence `metainfo_exact_filename`, parser `arc18.1`.
- Read-only inspection of that profile confirmed 4 torrent identities, 923 indexed file rows, 118 verified volume mappings and search state `error_class=none`, `result_count=11`. The Vol. 1 hash/file mapping above was persisted as verified. The installed extension stored the Render manifest and generation 18.
- No payload action was invoked. Acquisition state remained `none`; acquisition-failure signals, LocalDownloads jobs/items/active counts and bytes all remained zero.
- Existing native transport/metainfo/indexer/service harness results from the preceding checkpoint remain recorded there; they were not rerun for this endpoint-only native change. Linux/Android runtime and installers were not tested here.

## Release gate

[Render's free-service documentation](https://render.com/docs/free) states that free services sleep after 15 idle minutes and waking takes about one minute. That exceeds the app's current 20-second request deadline. This run qualifies a warm instance only; idle-start tolerance was not tested or implemented. Before release, use an approved always-on hosting plan or implement and qualify explicit bounded cold-start handling. No paid upgrade was authorized or made, and no keepalive was added.

All existing Vault, Linux account, Tankoyomi and Tankorent changes remain preserved. MangaSin/LectorXD qualification and LibGen, AudioBookBay and GetComics deployment are outside this hosting follow-up. PR 19 stays draft; no installer, update feed or 1.1.9 v2 release was published.
