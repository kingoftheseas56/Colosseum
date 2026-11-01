# Vinyl slice 5: song and playlist search, pagination

Source baseline: `eb3255cf0774881744110fe395bb5085719ec22f` on `codex/vinyl-production-flow`. This records branch qualification; it does not establish main-branch integration or a release.

## Implemented

- Extended the existing Python provider, native catalogue facade and QML session with typed Albums, Artists, Songs and Playlists search pages. The native facade registers exact recording/video identities before cards can play or save them. Standalone songs remain recording containers rather than fabricated album releases.
- Pagination follows the actual upstream continuation. It does not request an increasingly large prefix. Raw source offsets survive filtering; appended search cards deduplicate by stable identity while retaining source order. Each category retains its own loaded pages for the current query.
- Reused the existing Discover shelves, See all grid, category controls and Song/collection detail routes. Icon-only load-more controls expose accessible names, loading and recoverable errors. Online results hide unsupported album metadata filters. Query replacement, cancellation, profile retirement and Back restore fence stale responses; a failed append retains the preceding results and its retry cursor.
- Public playlist browse requests one server page. Its existing detail page can append further pages, preserving repeated recordings as distinct positions and keeping returned unavailable/deleted rows. Provider search `VL` browse IDs are normalized to playlist IDs.
- Play, Shuffle and whole-playlist Queue fetch the entire server listing before replacing/appending the existing native queue. An incomplete provider response cannot be admitted as a complete playlist. Selected-row playback checks that the fetched full list still contains the intended recording at that position; changed order produces an error instead of silently playing another track.
- Removed the old 200-track whole-playlist ceiling. The existing 10,000-track queue limit remains explicit; an oversized or nonadvancing listing fails rather than silently truncating it. Unknown header counts are permitted when the server continuation explicitly exhausts. A header/listing discrepancy is retained as `omittedTracks`, displayed as unavailable entries; the app does not invent tracks the server never returned.
- Existing stores, source resolver, player and metadata cache remain authoritative. Cache keys include the complete page request. Resolved audio URLs/headers remain ephemeral. Native source retention now preserves terminal total/omission metadata when reopening a shorter preview of a retained complete playlist.
- `colosseum_runtime_ready` now refreshes the app-owned provider helper even when only Python/QML changed and no native relink occurs. Source and built helper SHA256 matched at runtime qualification.

Luna xhigh handled the bounded QML controls, provider contract tests, native-bridge QML tests and receiving reviews. Root implemented/integrated provider/native/session routing and independently ran builds, tests and actual Main flows. Review findings about omission metadata and incomplete enqueue admission were fixed and covered by regressions.

## Verification

Release targets passed: `vinyl_catalogue_source_test`, `vinyl_native_session_bridge`, `colosseum_runtime_ready`. QML manifest: `229e593d15bfd797a0409d6e00e12544084463aae74e379d4ff852013c8ddffe`.

- Seven focused CTest registrations passed, zero failures, 40.42 seconds: catalogue transport/facade, personal-library contracts, and LiveWorld/personal-library/Home/Discover/catalogue-page native-bridge QML suites.
- Python provider suite: 51 cases, 50 passed and one pre-existing optional live Eminem smoke test skipped. Fourteen new page-contract cases cover typed records, cursor context/bounds, exact continuations, raw offsets, search deduplication, duplicate/unavailable playlist positions, terminal omissions, region retention and complete-list aggregation.
- New native-bridge QML suite: ten test cases plus lifecycle. It covers four-category fanout/cached switching, append/deduplication, failed retry, stale query/profile fences, tab/Back restoration, typed destinations, playlist page errors and duplicate preservation, full Play admission and incomplete enqueue rejection.
- Native facade regression covers exact source registration from later pages and reopening a partial preview without losing the retained full playlist's omission count.

### Actual Colosseum app

`tests/vinyl-live/SearchPagesFlow.qml` instantiates production Main with the real profile stores, provider, native session and player. The driver invokes existing shared control/tab handlers and captures the actual scene. It does not replace the UI or claim physical mouse/keyboard automation. Runs use a disposable profile; successful reruns reused that isolated profile, so these are not cold-start latency measurements.

The final visible app run passed:

1. Discover searched Songs for `Nas`, loaded 20 first-page records, activated See all and the load-more control, then retained 36 unique cards from 40 raw provider rows. Counts and rankings vary upstream.
2. A second-page Song card opened Song without changing the queue. Save used Collection. Explicit Play started **Hero (feat. Keri Hilson)**, recording `e6aeb288-1c43-5658-9dcd-a236c77db2a4`, through the existing player with source `youtube` and position above one second. Back restored the query, Songs category and appended grid.
3. Playlist search for `Nas all songs` appended a second page: 37 unique cards from 40 raw rows. A focused `Nas Unreleased Songs` query located the larger public-list fixture independently of its changing broad-search ranking.
4. Opened playlist `PL2VjCRqHWLaSxCQBGuHAEx17yMSkoiJii`, canonical ID `6ee9ac10-e1b4-542c-97e9-46fc51c12f5d`. Browse showed 100 rows from a declared 131, with continuation available.
5. Pressing Play while only those 100 rows were visible fetched a **127-row queue** with complete server exhaustion and `omittedTracks=4`. It played the exact first recording, **Nas - Amongst Kings**, ID `a5f225f4-61f4-5e88-8f7b-14b484e05327`, source `youtube`, with advancing position above one second.
6. The playlist load-more control appended the remaining 27 browse rows; the detail contained 127 tracks, no continuation and the same explicit omission count. A declared 131 versus returned 127 establishes a discrepancy, not the specific reason each missing entry was unavailable.
7. Back restored the searched grid; clearing the query restored all four public Discover shelves. Home remained independent and the saved searched song was still present.

Separate direct live provider probes also returned successive search pages and the same 100/27 playlist pages. A final visible Main inspection run opened the public playlist with the retained full queue paused and remained open for review. Captures were visually inspected at the actual 1920x1080 scene size.

Evidence directory: `D:/Colosseum-Vinyl-Slice5-20261021` contains final build/test logs, `live-err.log` with `VINYL_SEARCH_PASS live`, `inspect-err.log` with `VINYL_SEARCH_PASS inspect`, provider probes and scene captures. Logs, databases, raw responses and screenshots remain outside Git.

## Limits

- Anonymous public catalogue, default region IN, was live-qualified. The region contract is unit-tested separately; account/private playlists, subscriptions and personalized YouTube recommendations are not enabled.
- Search and playlist continuations depend on the bundled ytmusicapi 1.12.3 parser/request shapes. Protocol-change, repeated-token and invalid-page checks fail explicitly. Upstream continuation expiry requires a fresh search/reopen; there is no promise of immutable playlists across requests.
- Explore/charts landing shelves still expose their bounded discovery snapshots. This slice adds categorized search and public-playlist pagination, not an exhaustive discovery-feed continuation API. Suggestions, search-history management and radio/autoplay remain separate checklist work.
- Whole-playlist fetching remains subject to the existing provider deadline and response-size/10,000-track limits. This slice does not establish a latency guarantee for arbitrarily large playlists or newly qualify a torrent success. Both new actual-app playback routes used YouTube fallback.
- Existing `Main.qml` `universeHallLayer` and offscreen font warnings remain unrelated. Some remote artwork requests failed or were throttled; a successful catalogue/playback flow does not guarantee every upstream image loads.
- Early driver attempts selected a hidden Library grid and assumed the large playlist retained its position in broad search. Visible-control selection and a dedicated title query corrected those test assumptions; only the final passing run is accepted here.
