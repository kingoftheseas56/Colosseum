# Tankorent search Worker checkpoint — 2026-10-23

Tankorent search is deployed through Workers and verified in the actual Windows app. Native libtorrent and the existing download/file-selection pipeline remain unchanged. This checkpoint does not create or authorize a release.

## Scope and preservation

- Continues from Tankoyomi checkpoint commit `3ba14a02b670a59a2cdeaa7dd3672a96e4bc6759`, preserving the Vault fix and existing Linux account changes in its master ancestry.
- Production `TankorentSearchService` instantiates a generic Worker indexer for the same four stable source IDs. Normal media allowlists, explicit source picks, enabled flags, search handles, cancellation, partial source results and existing ranking consumers remain local.
- Knaben remains outside the normal book/comic allowlists, while explicit/unfiltered searches retain their prior access to it. Its Worker route does not enable it by default.
- Existing `tankorent/indexers/<id>/enabled` and health persistence keys are retained. Generation 16 updates only the built-in extension URL; identity `colosseum.well.indexers`, enabled state, array order, installed timestamp and custom transport URLs survive.
- The main app target stops compiling the four source-specific request/parser implementations. Legacy fixture targets retain them; targets using the shared search service now also link its Worker adapter.
- No libtorrent, downloader, reader, QML, Nyaa, LibGen, AudioBookBay or GetComics implementation was changed. Unrelated work remains in its original checkout.

## Deployment

- Dedicated [Worker health endpoint](https://colosseum-tankorent.colosseum-watchparty-relay.workers.dev/healthz), revision `1.0.2`, JSON contract `1`.
- Deployment ID: `ec3b8d5540874ce0aac7182947bc4217`.
- Bundle SHA-256: `774F0E57218B63F22C0670CCE8E487F1650229E43F5546DD6FA429D689200F99`.
- Public health and manifest returned HTTP 200 and the expected ID/version. The manifest exposes `behaviorHints.configurable`, which the app's manifest sanitizer retains.
- Only `colosseum-tankorent` was created/updated; other Workers and release feeds were untouched.

The Worker bounds each search to 20 seconds and 39 upstream requests, uses exact HTTPS host allowlists with validated redirects, caps bodies/results and permits at most four concurrent EXT detail requests. The native client caps its response at 1 MiB, validates provenance/fields/hash/magnet, rejects redirects and has an independent 25-second deadline. Cancellation aborts pending native requests and suppresses late completion.

## Verification

- Worker tests: **13 passed**, including all four source mappings, contract limits, errors, redirects, EXT fallback/detail parsing, bounded concurrency and request budget. Wrangler dry bundle succeeded.
- Native production `colosseum_runtime_ready` build succeeded. Executable SHA-256: `4C76FC9036BEAC41D0A5964928C459F9C87A2A122764A6157494B1E417F48ACF`.
- Fresh native CTest `colosseum.tankorent_worker_transport_harness` passed in 27.52 seconds. It covers source/settings/category routing, result validation, structured health/errors, rejection cases, cancellation, response bounds and exactly one completion after a hard deadline followed by a late reply. An isolated build/run also passed.
- Existing `comic_torrents_search_harness` compiled and passed, retaining cumulative results, deduplication, partial indexer failure and superseded-search behavior.
- Extension migration/first-run Qt tests: **29 passed**, including built-in/custom URLs and both enabled states. Before implementation, the new built-in URL cases failed as expected.
- Receiving source review found and corrected the remote manifest configurable-hint shape. No remaining concrete correctness blocker was found in this scoped review. Final diff whitespace check passed.

### Actual app

Agent-run proof used the real Main/BiblioBook UI and production backend, an isolated profile and an offscreen rendered window. It waited for the startup splash to disappear and opened the actual book detail route for *Pride and Prejudice* / Jane Austen.

1. Live Worker-backed search rendered five ranked torrent rows in the real Biblio torrent shelf; the screenshot was visually inspected.
2. The existing row action selected the Torrents-CSV result with hash `6feffce1abdab62dd8a01a2caee00d5d7bc0e20d` (reported torrent size 3,130,581 bytes).
3. Native libtorrent resolved metadata for five files. The unchanged file picker selected the EPUB of 2,186,698 bytes, and the downloader entered `downloading` state. The native picked-file log and matching hash corroborated this; `resolving` alone was not accepted as proof.
4. The test immediately called the existing delete API, received removal completion and verified status `none`. It did not wait for a completed ebook download or open a downloaded book.

The same path was repeated after final deployment updates. The final revision `1.0.2` run and its rendered screenshot are recorded outside source control. Literal magnet construction is verified through the existing production call chain; the app does not emit the literal libtorrent input URI independently. This is agent runtime evidence, not a human or installer test.

## Availability and remaining work

Live checks returned HTTP 200 results for Torrents-CSV, EXT and Knaben. PirateBay returned upstream HTTP 429, preserved as a rate-limit error; it did not block healthy source results. These observations do not guarantee future upstream availability or qualify every result/download.

No Linux/Android app build or runtime was performed here. MangaSin and LectorXD remain disabled under their separate unresolved qualification. Nyaa, LibGen, AudioBookBay and GetComics deployment inspection is the next requested stage; no migration of those sources was attempted here. No installer, release feed or 1.1.9 v2 release was published.
