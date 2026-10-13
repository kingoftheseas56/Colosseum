# Content API integration into the scoped 1.1.9 candidate

Date: 2026-10-14. This is a source-reviewed implementation plan, not a release approval.
Target reviewed read-only: `D:\Colosseum-1.1.9\worktrees\codex-main-three`.
Verified HEAD: `d7f86f4411300f5df840377eaca7ff5859ae2879`.
Scope: the already decided 1.1.9 changes, excluding Feria and Vinyl.
No candidate application files were changed while preparing this plan.

## Decision and release boundary

Replace catalogue delivery through GitHub REST and automatic release-asset DB downloads.
Deliver bounded semantic catalogue queries through the content API; do not reconstruct full DBs by row pagination.
Keep Colosseum's local identity, library, history, progress and preferences authoritative.
Preserve existing ct1 title identities and provider-ID mappings; storage migration must not remint titles or detach progress.
Fresh installations must open and use local files, readers, playback and local library without catalogue downloads or third-party accounts.
Existing valid local catalogue DBs may remain compatible read-only caches; do not delete or overwrite them during migration.
No MusicBrainz/Vinyl product UI and no Feria or Vinyl branch merge belong in this release.
The API's current raw-table pagination is infrastructure evidence, not native feature parity.
API-only catalogue cutover remains blocked until the explicit gates below pass.

## Live native seams and required contracts

`native/main.cpp:1406-1506` resolves four DB paths, creates four catalogue owners,
exposes their existing QML names and creates `VaultIdentifier` with Comics/MAL/IMDb owners.
The resolver tries `data/*.db`, then `applicationDirPath()/../../`, then AppData `/catalog-vault`.
`main.cpp:1478-1506` constructs `CatalogVaultClient` with the GitHub API URL,
connects synchronous `aboutToReplace`/`databaseUpdated`, and schedules `checkAndFetch()` on startup.
`CatalogVaultClient.h` owns manifest freshness, state.json, serial bulk downloads and swap signalling.
`CatalogVaultClient.cpp:260-344` checks 24-hour freshness and reads `/releases/latest` asset URLs.
These call paths must become incapable of launching the prohibited catalogue transport in the release build.
Disabling a launch timer alone is insufficient while callable `checkAndFetch()` retains the same transport.
`qml/Main.qml:4733-4762` exposes fetching/tag and counts four `ready()` values via `readyChanged`.
Preserve `ready()` as an invokable: the existing headers explain its QML property-name collision.

| Existing C++ owner | Semantic API coverage required before its remote cutover |
| --- | --- |
| `ImdbCatalog.h/.cpp` | `titleCatalog`, `matchByTitle`, prefix `search`, bounded batch `titleFacts`, `titlePresentation` |
| `MalCatalog.h/.cpp` | `genreEntries`, true `genreCount`/`genreCounts`, `animeCatalog`, title identity/search, `mangaById`, Discover facets and AND-filtered pages |
| `ComicsCatalog.h/.cpp` | series/detail/search/exact matches, downloads with mirrors, curated rank/detail/coverage/by-normalized-title/genre shelves/downloadability, shelves and AND-filtered Discover pages |
| `TankobanCatalog.h/.cpp` | `seriesInfo` and complete per-series `volumes` result with numeric-aware order and count-based synthesis |

IMDb source at `ImdbCatalog.cpp:72-144` joins title/genre and binds all query values.
Its real allowlist also includes `notGenre`; preserve exclusion lists, language rules, unknown-value rejection and series/mini semantics.
MAL source at `MalCatalog.cpp:199-270` joins tags for anime filtering; rows stay Jikan-shaped.
MAL Discover uses classification joins and scores; preserve explicit-content rules and the documented trending fallback.
Comics source at `ComicsCatalog.cpp:88-198` joins `series_stats` and `download_mirror`.
Search requires every typed word, ranked exact phrase then prefix then other matches; preserve availability ranking and stale-schema degradation.
Curated discovery also requires its coverage/source tables and house ranking; importing `series` alone cannot make Comics ready.
Tankoban source at `TankobanCatalog.cpp:105-194` combines series counts and baked volume rows.
It synthesizes `1..N`, overlays covers/names, retains out-of-range baked entries and sorts numerically; raw volume rows alone lose behavior.

## Stage 1: isolate delivery while preserving local operation

Remove the GitHub catalogue URL/REST headers, automatic asset downloads and remote DB-swap path from the release catalogue transport.
Retain catalogue constructors and local SQLite readers for previously available data and explicit developer fixtures.
Give the existing `CatalogVault` integration an honest bounded-query refresh/status contract, or migrate its exact callers in the same slice.
Update fetching/tag/readyCount meaning together; a healthy API must not imply a complete catalogue.
Keep `VaultIdentifier` synchronous local/cached lookups usable; remote matches may enrich asynchronously and must preserve ambiguity rejection.
Do not add a new identity authority or replace `VaultLibrary`/local progress stores.
Verify a clean AppData launch with no reachable `data/` directory; development DB overrides would hide transport failures.

## Stage 2: establish production auth and versioned semantic reads

Add bounded server endpoints per method family above, using the imported namespaced tables and bound parameters.
Expose dataset/source hash, schema/API version, last publication time and logical catalogue readiness.
Readiness requires every dependency table for that logical operation to be complete at a compatible source version.
Implement server filtering/joins/search and stable pagination; never download all rows and perform joins in the client.
Use explicit query allowlists, maximum page/batch sizes, deterministic tie-breakers and query-cost bounds.
Return distinct unavailable/not-ready, empty-success and authentication-expired outcomes.
Preserve native QVariant/Jikan row shapes through C++ mapping so QML card renderers keep their contracts.
Implement a production guest/account lease issuer in the existing identity service integration.
The staging-only `/admin/session` and embedded `ADMIN_TOKEN`/`SESSION_SECRET` are not client production credentials.
The installed application receives short-lived read leases; it carries no signing/import/admin secrets.
Guest access must work without signup; expiry renewal is bounded and unavailable auth does not disable local core use.
Require live issuer, renewal, revocation/expiry and throttling evidence before enabling production reads.

## Stage 3: asynchronous native requests with bounded offline caches

Extend the existing four C++ catalogue owners with a shared C++ transport helper; they continue to own result shaping and availability.
Their existing Q_INVOKABLE accessors are synchronous. Do not wait on HTTP or spin nested event loops inside them.
Return valid local/cached results immediately; add request IDs and explicit completion/error signals for remote cache misses.
Update only the consuming QML interactions needed to request/refill pages and ignore stale responses after navigation/filter changes.
Use existing cache ownership if suitable; inspect its storage/eviction contract before introducing a catalogue-response cache.
Cache keys include API/schema version, source version, medium, normalized query, explicit flag and page/detail identity.
Store bounded returned pages, facets and viewed details with atomic writes, size/age limits and last-good retention.
Distinguish cached stale content from current server content; never turn network failure into a successful empty catalogue.
Do not mark a cached page as a complete offline dataset or backfill every API row in the background.
Cancel/coalesce obsolete work, bound timeouts/retries, and respect existing foreground-pressure scheduling.
Offline identity uses only local/cached facts; unknown remains unknown rather than inventing a match.

## Stage 4: publication, packaging and acceptance

Only small source tables are ready now; full joined native queries cannot be assumed available.
The four source DBs total approximately 165 MB, rather than the earlier 250 MB estimate.
Baseline candidate worktree has no `data/*.db` files; size/bootstrap figures come from the publishing inventory, not those missing files.
Free-tier bootstrap estimate is about 2.82 million weighted data writes plus control overhead, roughly 53 days at the 60,000 daily safety cap.
Treat that as an estimate; checkpointed import status and dependency completeness determine actual enablement dates.
Publish versioned logical catalogue capabilities only after exact source counts and dependent-table checks pass.
Do not accelerate publication by silently abandoning the free-tier cap or issuing bulk client downloads.
`scripts/installer/package_release.sh:61-64` currently assumes no catalogue DBs ship and first-launch GitHub delivery.
Replace that assumption with the query-service configuration and no-download/offline contract; verify actual packaged first launch.
Package only the production API URL, supported schema version and safe feature configuration, with no administrative secrets.
Keep unrelated release updater behavior outside this catalogue slice; no catalogue GitHub REST or bulk-DB fallback may remain.

## Gates and rollback

Gate A: all required semantic endpoints, joins, filters, shapes and dependency readiness pass native/API parity fixtures.
Gate B: production guest leases and account integration work without distributing privileged secrets.
Gate C: complete source publication and free-tier query/import budget evidence support the selected feature set.
Gate D: clean packaged launch, upgrade with existing DB caches, offline use, expiry and service failures pass on the scoped candidate.
Gate E: receiving review confirms the final candidate diff contains the intended non-Feria/non-Vinyl changes only.
Until these pass, do not declare API-only cutover or 1.1.9 release readiness.
Rollback disables remote catalogue queries and keeps valid existing DB/response caches and all canonical local state.
Fresh offline installs continue local core use with honest unavailable catalogue enrichment; rollback never restores automatic GitHub downloads.
Retain versioned last-good server publication for compatible queries; invalidate only incompatible response-cache entries.

## Verification work to implement, not evidence already obtained

Use `imdb_catalog_harness`, `mal_catalog_rows_harness`, `mal_catalog_discover_harness`,
`comics_catalog_engine_harness` and `comics_catalog_arc21_harness` contracts as semantic parity anchors.
Add native local-server fixtures for transport, auth refresh, timeout, cancellation and asynchronous result generations.
Compare SQLite and API rows/ranking on the same tiny source fixtures, including ambiguous titles and `notGenre`.
Exercise Tankoban numeric order/synthesis, Comics mirrors/coverage, MAL explicit/AND facets and true totals.
Test partial source readiness and mixed source versions cannot advertise a complete logical catalogue.
Add installed-package network assertions rejecting catalogue GitHub REST, release assets and bulk DB transfers.
Run clean-profile, warm-cache, upgraded-cache and offline core journeys without user-piloted prerequisites.
Record source review, tests, builds, runtime, commit/push and release approval as separate evidence states.
