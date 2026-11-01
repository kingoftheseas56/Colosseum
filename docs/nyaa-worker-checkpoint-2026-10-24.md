# Nyaa search Worker checkpoint — 2026-10-24

Current architecture: [Nyaa is native again](nyaa-native-checkpoint-2026-10-25.md) following the user's October 25 decision. The hosted experiments below are historical.

Hosting follow-up: the [Render checkpoint](nyaa-render-checkpoint-2026-10-24.md) records successful live search and native metainfo/file-mapping qualification using the same handler on Render. The Worker-specific failure and error-path evidence below remain historical; production hosting readiness is tracked in the follow-up.

The Nyaa search migration is implemented and deployed, but live qualification is blocked by upstream HTTP 429 responses to Worker requests. Torrent retrieval, metainfo decoding and hash verification, file indexing, identity storage and downloads remain native. This remains a draft checkpoint and must not be released as a qualified migration.

## Scope

- Continues from Tankorent checkpoint `1e49e62845e0d899059e43890191f2eb301eadf2`, preserving the Tankoyomi work, Vault fix and Linux account fixes in the branch ancestry.
- Native query families, per-query partial results, uploader trust, volume matching, ranking, magnet construction and discovery provenance retain their existing owners. The production binary has no direct RSS fallback; legacy RSS parsing is compiled only for its logic fixtures.
- The Worker receives one query and returns bounded metadata from the fixed Nyaa RSS origin/category. It never fetches a `.torrent`, joins a swarm, computes file indices, stores the library or transfers media.
- Native responses are schema checked, capped at 1 MiB and bounded by an independent deadline. Native torrent URLs are restricted to canonical HTTPS Nyaa download paths, with redirects rejected and metainfo bodies capped at 4 MiB.
- Defaults generation 17 moves only the built-in Nyaa manifest URL. Stable identity `colosseum.well.nyaa`, disabled-by-default consent, enabled state, ordering, installed timestamp and custom transport URLs are retained.
- Search failures use a separate `sourcesFailed` signal, preserving acquisition `failed` and the Downloads centre. Repeated provider errors collapse to one message; verified cached sources remain usable.
- LibGen, AudioBookBay, GetComics and unrelated working-tree changes are outside this checkpoint.

## Deployment

- [Worker health](https://colosseum-nyaa.colosseum-watchparty-relay.workers.dev/healthz): revision `1.0.0`, JSON contract `1`.
- Deployment ID: `fc58334a46f04f8d9af29ee2e55706de`.
- Bundle SHA-256: `71141B6C359B5484627ECAEF9756BD58F7BB25EA26AE9076C02AA7A49CBD72F8`.
- Compatibility date is `2026-10-23`, the current UTC date at deployment (local date was October 24).
- Public health and manifest returned HTTP 200 with the expected extension identity and manga stream classification. Only `colosseum-nyaa` was deployed; other Workers and feeds were untouched.

## Verification

- Worker fixture/API tests: 13 passed. Wrangler 4.150.0 dry bundle succeeded.
- Extension first-run and migration Qt tests: 33 passed, including enabled/disabled built-in/custom URL cases.
- Production `colosseum_runtime_ready` build succeeded with real libtorrent enabled. Executable SHA-256: `EC06839BED341F4AF407D6892F75E05C4EA1BCB0806BCFBF23E5002AEC90FE53`.
- New native `colosseum.manga_nyaa_worker_harness` CTest passed 1/1 (0.58 seconds) in an isolated MSVC/Qt 6.11.1 build. It covers strict JSON/provenance, request and result bounds, partial queries, cancellation/duplicate/late completion and absolute deadlines. Native metainfo fetcher cases exercise URL rejection, redirect rejection, the 4 MiB streaming cap, reply destruction and the absolute deadline.
- Existing `manga_tankoban_logic_harness`, `manga_torrent_indexer_harness` and `manga_tankoban_service_harness` compiled and passed. This retains fixtures for RSS/ranking/volume matching, real metainfo decoding/hash mismatch rejection, raw file indices, verified volume mappings, cached-empty/error behavior and the service pipeline.
- Source review and final whitespace checks passed. Actual app qualification remains open below.

## Live availability

Three spaced public searches across two queries returned HTTP 502 with the structured error `Nyaa returned HTTP 429`. Two direct requests from this machine to the same RSS endpoint returned HTTP 200 from DDoS-Guard, including one using the same Accept header and no User-Agent. No Retry-After was exposed. This points to a Worker-egress-specific restriction; IP reputation versus another edge rule is unproven. No proxy, alternate origin or bypass was added.

The first actual-app run booted the production Main UI in an isolated profile, continued without an account and enabled only Nyaa. It stopped before search because its fresh-profile One Piece catalogue did not prepare (`resolvedMalId:0`, no shelf); this is not evidence of Worker-backed search success. A separate agent-run check then used explicit One Piece/Vol. 1 metadata through the actual `TankobanVolumes.prepareSeries` API and real Main/series/embedded Sources page. This supplied catalogue metadata only; no provider rows, Worker responses, metainfo or mappings were substituted.

The final production app visibly rendered `Some sources didn’t answer — Nyaa returned HTTP 429`; its screenshot was inspected. The dedicated `sourcesFailed` reason reached the page once. Acquisition-failure signals remained zero, acquisition status stayed `none`, and native LocalDownloads reported zero items, jobs, active transfers and bytes. A read-only query of that isolated profile recorded `provider_error`, no expiry and zero torrent/file/mapping rows. The driver exited 22 because its strict successful-result/verified-mapping criterion was not met; only the error path passed. Source and fixture checks alone are not a successful live migration.

The error path uses a separate signal because the generic acquisition `failed` signal also feeds Downloads. The service regression explicitly checks it is not emitted for search errors and that verified cached cards remain usable. No Linux/Android runtime or installer test was performed here.

Successful Worker-backed results followed by native torrent retrieval/hash/file mapping remain unverified. No installer, release feed or 1.1.9 v2 release has been published. Do not merge this draft Nyaa migration into a release candidate until that live gate passes.
