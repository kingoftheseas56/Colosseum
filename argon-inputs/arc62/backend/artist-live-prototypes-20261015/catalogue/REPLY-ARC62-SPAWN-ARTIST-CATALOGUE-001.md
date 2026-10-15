TASK: arc62-spawn-artist-catalogue-001
STATUS: DONE (isolated native proof; not production integration)

# Artist catalogue: verified worker result

## Authority and source safety

- BDC on Laptop 1. Source read-only: `C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum`.
- Branch `codex/vinyl-live-listening`, starting and final SHA `860b96ae27a59c955f6aa9bf4642351ed8ce21e3` (read-only Git verification).
- Initial/final dirty source status unchanged: 9 modified files (artist doc, 6 Vinyl QML/JS files, ArtistMedia fixture and QML test) plus 2 untracked Artist QML components. Did not edit source, index, other owners' directories, commits, remotes or deployments.
- All code, tests and build outputs written only in this owned `catalogue/` directory. Read-only BG1/BA0 source is compiled through CMake, never modified.

## Authored files (relative to owned directory)

- `ArtistCatalogueQuery.h` (4,140 bytes): provider-neutral native interface, source provenance, BA0/BG1 strong types, typed Artist results and page session.
- `ArtistCatalogueQuery.cpp` (26,562 bytes): full resolution, identity checks, release group dedup/merge, sort/filter, continuation, cancel/stale/revision fencing.
- `test_artist_catalogue.cpp` (19,283 bytes): queued fake provider and real assertion harness with adversarial negative controls.
- `CMakeLists.txt` (1,007 bytes): standalone Qt Core C++17 target and CTest registration.
- `README.md`: exact build/run and adopter instructions.
- Actual receipts `configure.log`, `build.log`, `test.log`, `ctest.log`; built executable in `build/Release/vinyl_artist_catalogue_harness.exe`; all build/generated files remain in `build/` here. No archive.

## Contract proven

- Reuses BA0 `ArtistMbid`, `ReleaseGroupMbid`, `ReleaseMbid`, `RecordingMbid` and `ProviderItemRef`, and BG1 `Artist`, `ReleaseGroup`, `Release`, `Recording`, `PageRequest`, `PageCursor`, `CatalogueErrorCode`. Provider browse/video IDs and provenance remain source associations; never canonical identity.
- Injectable `IArtistCatalogueProvider`: `lookup(selector,ticket,callback)`, `releaseGroups(artist,ticket,callback)`, `cancel(ticket)`; no transport, credentials or live endpoint in proof.
- `begin` resolves by canonical ArtistMbid or exactly matching name/alias. Explicit typed `Found`, `NotFound`, `Redirected`, `Unavailable`, `Ambiguous` and `InvalidQuery`; same display name belonging to two MBIDs fails closed. No fuzzy/name-to-MBID fabrication or automatic redirect follow.
- `page(sessionVersion, PageRequest, callback)` emits **Release Groups** rather than duplicated concrete editions; merges distinct ReleaseMbid editions/statuses and preserves qualified recording/artist credits and alternative source mappings. Provider canonical conflicts fail `Unavailable`.
- Typed primary/secondary type, release-year, edition-status filters; typed Newest/Oldest/Title sort. OR within field, AND across fields; one group returned if any edition matches selected release status, retaining edition provenance. Deterministic MBID final tie-breaker.
- Opaque cursors bind artist, filter/sort, immutable corpus revision, session generation and offset; limit 1..100; wrong/stale/malformed cursor -> `InvalidPage`, invalid ranges/limits -> `InvalidQuery`.
- Provider tickets and generation fencing suppress late callbacks after cancel, supersession or query destruction, even when fake provider ignores cancellation. The view is latest-request-wins; callback delivery is required to be serialized on owning thread.

## Executed evidence on Windows

- Configure: CMake `C:\Qt\Tools\CMake_64\bin\cmake.exe`, Visual Studio 17 2022 x64, MSVC 19.44.35223.0, Qt 6.11.1; `CONFIGURE_EXIT=0`.
- First build exposed a fixture misuse of const Qt `at()` references, corrected only in owned test. Final build `BUILD_EXIT=0`; final `build.log` had no warnings/errors.
- Actual harness `HARNESS_EXIT=0`; `test.log`: `RESULT PASS checks=78 provider=queued-in-process-fake transport=none`.
- CTest Release `CTEST_EXIT=0`: `1/1 Test #1 vinyl_artist_catalogue_harness Passed`, `100% tests passed, 0 tests failed`.
- Positive/negative checks include: colliding displayed artist names/different MBIDs; exact name/alias vs prefix; canonical ID and redirect; unknown/offline artist; paginated release groups and duplicate editions; Official/Promotion/Bootleg statuses; sourced artist and recording credits; reordered-provider deterministic cursor; malformed/foreign/old cursor; invalid limits and year range; filters and latest-wins sort; cancel and stale/out-of-order/post-destructor callbacks; changed corpus version; offline discography; mismatched release-group owner and conflicting duplicate-group metadata.

## Contract mismatches, non-goals, adoption

- BG1 core currently provides `ICatalogueClient::artistById`, `artistDiscography(Page<ReleaseGroup>)`; no exact-name ambiguity state, async view ticket, typed edition-status discography filter or enriched sourced `GroupRow`. This is an additive prototype interface; do not claim frozen BG1 has changed. Preserve `Ambiguous` explicitly during adoption rather than collapsing to Found/NotFound.
- Frozen `scripts/vinyl/provider.py` currently supports YTM album-filtered `search`, `album`, `resolve`. No real artist endpoint or exhaustive artist-alias/discography adapter. Its `uuid5` identifiers are provider labels, NOT MusicBrainz MBIDs. No live server/protocol was invented or exercised.
- Fake exposes immutable-version full-discography snapshots per page. This proves deterministic query semantics, not efficient production pagination. Separate adoption must build MusicBrainz ArtistMbid-to-provider browse-ID mappings with provenance, guarantee complete exact-alias candidate results, implement a real artist/source adapter and corpus-revision pagination (ideally server-side), and marshal threaded callbacks to the native query owner.
- Separately wire the adapter into existing `VinylProvider`/Vinyl native view service, then QML as another owner's task. Artwork, saved library state, media playback and bridge were not touched.
- **Not verified:** live YouTube Music artist adapter, MusicBrainz/Arc65 production corpus, real transport/offline behavior, threaded adapter, UI/bridge, full app build, performance or deployment. `DONE` applies solely to compiled, executed **isolated backend proof**.

## Review commands

Reproduce via `README.md`. Read the 78 PASS lines in `test.log`, final CTest output in `ctest.log`, and MSVC output in `build.log`; all receipts are in this same `catalogue/` directory.
