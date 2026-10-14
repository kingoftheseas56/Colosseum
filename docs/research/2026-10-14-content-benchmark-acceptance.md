# Content backend benchmark acceptance

2026-10-14. Scope: preparing a fair D1/libSQL comparison and receiving its evidence.
This document records live source seams and proposed acceptance, not completed native runtime proof.
The content-service plan and separate asset/Universe audit remain companion inputs.
User emphasis: assets, Universe page loading and all Tankoban flows except chapter mode depend on the transition.
Chapter reading/provider chapter traversal is outside this benchmark. Volume and comic browse/source doors are included.
Feria/Vinyl remain excluded from scoped 1.1.9 integration. No app implementation or cloud action is authorized by this file.

## Tankoban journeys the corpus and receiving checks must represent

| Journey | Live source anchors | Required result and receiving check |
| --- | --- | --- |
| Manga landing and genre door | `qml/TankobanMangaTab.qml:58-82`; `qml/GenreApi.js:179-185` | Curated landing remains navigable; genre entries, true totals, score/member order and usable cover descriptors agree with native/catalogue semantics |
| Comics landing | `qml/TankobanComicsTab.qml:81-163`; `qml/ComicsDb.js:121-151` | Curated rank, genre mosaics and shelf mappings preserve identity/order; do not eagerly recreate all shelves or fetch every card detail |
| Manga Discover + multiple facets | `qml/TankobanDiscoverApi.js:234-248,395-426`; `MalCatalog.cpp:552-680` | Genre/demographic AND intersection, explicit policy, scoped counts/Bayesian mean, popular/top-rated/new/trending fallback, next page and empty results |
| Comics Discover + multiple facets | `TankobanDiscoverApi.js:492-494`; `ComicsCatalog.h` discovery contract | Genre/publisher/format/availability AND filters, coverage/community categories and native house ranking; availability remains a boost rather than silently removing unavailable titles |
| Search across manga/comics | `qml/WorldSearch.js:170-207,221-262`; `ComicsCatalog.cpp:114-159` | Current manga lane is AniList, comics lane native catalogue; all-word comic matching, escaped wildcards, exact/prefix tiers, distinct same-name runs, cancellation and failure marker |
| Manga title/ID door | `qml/MangaSeries.qml:825-854`; `MalCatalog.h:matchByTitle/mangaById` | Pinned MAL ID or exactly one title candidate, complete Jikan-shaped masthead, unknown/ambiguous remains unresolved, stable `mal:` identity/progress joins |
| Manga volume shelf | `MangaSeries.qml:139-153,397-433`; `TankobanCatalog.cpp:105-194` | Count-basis provenance, synthesized 1..N with baked overlays, out-of-range rows retained, numeric ordering, missing cover/unknown count handled honestly |
| Volume source entry and local readiness | `MangaTankobanService.h:169-181`; `native/main.cpp:1538-1545` | Correct prepared series/volume IDs handed to existing service; cached/local readiness and primary action survive. Actual Nyaa/torrent/provider resolution is separate evidence, not SQL timing |
| GCD comics detail/release door | `qml/Main.qml:1356-1368`; `ComicsCatalog.cpp:88-110,160-198`; `qml/ComicSeries.qml:192-216` | Series/stat joins, ordered releases/mirrors; baked releases bypass live tag resolution; stale missing-mirror schema degrades without fabricated URLs |
| Curated comic series/edition/source door | `qml/ComicsDb.js:190,252-264`; `ComicSeriesPage.qml:12-16`; `ComicResolve.js:40-88` | Curated edition/coverage/claim evidence, correct GetComics post mapping and downloadable state; LOCG/GetComics resolution remains its existing owner and must fail honestly |
| Library/Continue and fresh offline use | `qml/TankobanLibraryTab.qml`; `qml/TankobanLibraryApi.js`; local state ownership in migration plan | Catalogue outage does not erase local library/progress or block existing files; no background DB reconstruction or prohibited bulk-download fallback |

The live manga Search lane is not the same owner as MAL catalogue discovery. Do not claim a MAL SQL benchmark proves AniList search or change that provider implicitly.
`ComicsDbLoader.qml:13-16` currently hands over a ready native engine synchronously.
`ComicsDb.js:26-38` captures engine availability and synchronous readiness assumptions; async migration must revisit these seams explicitly.
`MangaSeries.qml:820-825` similarly assumes synchronous native catalogue resolution.
SQL/provider fixtures cannot prove a remote result triggers QML readiness/rebinding correctly.

## Cover and provider boundaries

`native/main.cpp:280-301` owns a pinned network-manager path and a QNetworkDiskCache; some live-source paths explicitly bypass caching.
Cover URL correctness is distinct from fetch, decode, layout and usable first paint.
Corpus outputs must retain cover fields and provenance; missing artwork must use an honest fallback.
Installed runtime checks need cold/warm artwork caches, failure/empty-body handling and bounded image sizes.
Do not count issuing a JSON response as completing Tankoban's visible page load.
No repeated upstream lookup per card is assumed; service metadata cannot replace acquisition/resolver ownership.
Provider source doors may be proven first with deterministic adapters, but real source availability remains separately unverified.
Actual download/ingest/volume-open evidence is outside backend SQL measurements and must not be inferred from them.

## Fair same-data correctness contract

Freeze source hashes, extraction version, schema/indexes, sample selector and relationship closure for both engines.
Use identical typed binds and logical queries; record backend-specific SQL if a compatibility adapter is required.
Root samples must include their relevant genre/classification, stats, volumes, editions, downloads and mirrors.
A sample omitting relationships changes query behavior, not merely its speed.
Generate local read-only SQLite oracle outputs once per query over the same selected population.
Use full source files with a selected-ID population restriction or a deterministic local sampled DB to compute sample expectations.
Store unrestricted full-source oracle outputs separately; sampled counts and scoped means cannot be compared to full-population expectations.
Record ordered qualified IDs, relevant mapped values, totals, facets and provenance, not just row count.
Test nulls, Unicode normalization/case behavior, literal wildcard/backslash escaping, unknown filters, explicit exclusions, ambiguity, ordering and paging.
Identify native unspecified ties; compare valid tie groups or define and independently review the new stable ordering rather than silently calling it parity.
Comics final ranking includes C++ processing: SQL-only rows/aggregates are partial evidence until the same ranking/mapping stage is checked.
Tankoban synthesized volumes similarly require its native algorithm or an independently checked equivalent.
D1's LIKE-pattern byte limit requires tests at 49/50/51 UTF-8 bytes, including generated wildcards and escapes and multibyte input.
Over-limit input needs defined rejection or a semantic-preserving query alternative; silent truncation changes matches.
A passing tiny fixture proves its tested semantics only, not full native parity or full-catalogue performance.

## Measurements needed for a backend decision

Capture import wall time, source/output bytes, rows inserted, indexes/control writes, retries and provider usage before/after.
Treat modeled writes separately from observed provider meters; unexplained meter lag is uncertainty, not zero cost.
Capture per-query rows scanned/read, rows returned and bytes; API/card count does not measure query amplification.
Include facets, AND filters/scoped ranking and worst bounded details, not only primary-key lookups.
Measure direct hosted database HTTP and end-to-end Worker semantic HTTP separately with identical request corpus/auth/cache policy.
Direct D1 REST and libSQL HTTP exercise different gateways; report that limitation and do not label them production API latency.
Record client location, timestamp, connection reuse, sample count, errors, p50/p95 and raw observations.
A single local client cannot establish India-wide or multi-region latency. Remote engine placement is recorded configuration, not a tested user region.
Warm means repeated same query after a successful priming call under an explicitly stated cache policy.
Cold means a stated idle interval/new connection/cache condition; label it first-request-after-idle unless server cache/isolate coldness is independently known.
Interleave backends or alternate blocks to reduce changing-network bias; keep query/result sizes/populations equal.
Separate application/edge cache hits from warm database queries; no assumed 90% hit rate.
Worker CPU may be unavailable through accessible telemetry: record unavailable and do not infer it from wall time or database timing.
Use local EXPLAIN/index evidence to identify scans, then validate observed hosted meters; local query plans are not hosted utilization proof.

## Proposed practical latency reporting and stop gates

First establish the existing local/native journey and network baselines. No fixed 500 ms pass/fail is a user requirement.
Suggested investigation targets: warm cached metadata should feel immediate; a normal remote list request should aim near one second when network permits.
Report visible-first-card and full-request timings separately; artwork and provider enrichments can finish progressively.
Treat repeated multi-second metadata responses, excessive fan-out or UI stalls as issues to diagnose, with workload and network context.
These are proposals for later acceptance calibration, not guarantees or authority to remove features for speed.
Before hosted work, reserve the bounded fixture import/query allowance and verify actual free plan/account budget.
Stop on unknown billing/paid resources, unauthorized account setup, parity failures, unexpectedly amplified reads/writes, quota/429 or source hash drift.
No full-source remote import is justified by a tiny proof; extrapolate explicitly and approve the next bounded slice on observed costs.
Do not choose a measured winner if Turso hosted access is missing, only one backend was measured or both were not tested on the same data.
Recommendation may remain conditional on semantics and budget; label the missing measurements precisely.

## Remaining acceptance beyond preparation

Required: hosted same-data parity, full-source semantic coverage, measured bootstrap/update budget and representative production-scale query shape.
Required: production guest issuance/renewal, mixed-revision refusal, operation readiness, selective page/details and content/asset cache integrity.
Required: installed Tankoban journeys above with clean profile, existing DB cache, warm response cache, offline, cancellation and late result rejection.
Required: explicit evidence that unavailable cloud content cannot masquerade as a successful empty result or mutate canonical local state.
Required: independent Universe content update/rollback and asset first-paint evidence from the companion audit.
Required: final 1.1.9 receiving diff, no Feria/Vinyl, no GitHub REST catalogue transport and no DB/content-pack transfers.
Prepared scripts, local oracle passes, hosted SQL results, Worker results and installed runtime proof remain separate evidence states.
