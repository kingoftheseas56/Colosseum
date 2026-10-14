TASK: content-options-05-r2-indexes
STATUS: review-ready

# Workers + R2 Standard precomputed catalogues, no live SQL
Independent review checked 2026-10-14. Only the assigned stack. [O] = published Cloudflare terms; [L] = user-supplied verified baseline or read-only inspection of native/engine/{ComicsCatalog,MalCatalog,TankobanCatalog,ImdbCatalog}.{h,cpp} plus CatalogVaultClient.h; [E] = estimate; [P] = proposal. No cloud or runtime testing.

## Verdict
**CONDITIONAL** for small, read-heavy, precomputed catalogues and evolving Universe/Tribute JSON. **REJECT** as a proven parity-complete, guaranteed-hard-$0 replacement, and **REJECT** 5,000 DAU at 30 dynamic API requests/day on Workers Free. Object storage removes the D1 bootstrap-row-write bottleneck, not the query engineering, request ceilings or R2 overage exposure.

**Three decisive tradeoffs:** (1) ~hundreds of thousands of object PUTs can replace millions of D1 row writes; (2) faceted indexed search, ranking and joins require offline projections with stringent parity tests, not mere JSON dumps; (3) cheap storage/no direct R2 egress charges cannot overcome Workers' request ceiling or guarantee zero R2 bills. An alternative **Workers + D1 *small* search index + R2 rich documents** could beat this on complex dynamic filters/ranks, if its separate D1 row-import bottleneck is actually solved. It is not assessed here.

## 1. Official limits, payment and free-only eligibility [O]
| Resource | Current published term | Consequence |
| --- | --- | --- |
| Workers Free | **100,000 requests/account/day** resetting midnight UTC; **10 ms CPU/request**, 128 MB isolate, 50 external subrequests/request (1,000 internal service subrequests), 6 concurrent outgoing connections | Over daily limit: **1027**; excess CPU may cause **1102**. Use **fail closed**, never bypass a protected Worker. [S2] |
| R2 Standard | **10 GB-month** storage, **1,000,000 Class A/month**, **10,000,000 Class B/month**, **free direct R2 egress** | PUT and LIST Class A; GET/HEAD Class B; DeleteObject free. Multipart/retries/lists need allowance. [S1] |
| R2 Standard overage | **$0.015/GB-month**, **$4.50/million Class A**, **$0.36/million Class B**; billable-unit rounding | Paid use beyond included thresholds, not a hard $0 service. **Infrequent Access has no free allowance**, charges retrieval: exclude it. [S1] |
| R2 enablement | R2 subscription/checkout required; monthly free usage can still be $0 | **R2 is not enabled**; no subscription, payment or credit-card action authorized; account billing state unknown. [S4] |
| Payment preauthorization | Cloudflare may check usage-based billing method, suspend R2 access after failed validation, and may delete affected data after 30 days unresolved | Real suspension/retention risk, not speculation. [S5] |

**Hard-$0 judgment:** Month-to-month $0 **usage is plausible** if strictly inside Standard allowances; no temporary credit or paid-tier foundation is required in that case. Absolute zero-spend/zero-payment-method assurance is **not established**. Budget alerts, explicit operation budgets, throttling and kill switches reduce risk but do not prove a provider-side hard cap. Billing/subscription inspection was denied, so account eligibility/checkout outcome is **unverified**. This task did not touch them.

## 2. Bootstrap, PUT/object count, storage [L/E/P]
[L] Supplied checked baseline: four SQLite catalogues **164,626,432 bytes (~157 MiB)**; **24 tables, 1,133,337 rows**. Source indexes imply **2,823,109 D1 row writes**, ~**3.18m** with 32-row chunk ledger; supplied conservative importer takes ~**53 days** at 60k/day. Current deployed staging has a protected Worker, two D1 databases, **24,208 rows in 10 complete small tables**, 14 not imported, seven Worker and ten publisher tests green. No client wiring or production guest issuer.

[E/P] Build joins, sorted postings, faceted indexes, detail JSON and bounded response pages **offline from source SQLite**. R2 PUT counts objects, not source rows. Primary anchors: IMDb 61,585 + MAL anime 20,000 + MAL manga 20,000 + comics series 22,710 + Tankoban series 10,000 = **134,295** entity keys. An **illustrative, unmeasured** layout: 134,295 entity docs + 30,000 paged download/detail chunks + 10,000 posting shards + 1,000 manifests/assets = **175,295 PUTs** (two complete versions: 350,590), before retries, lists and multipart operations. By contrast, **one object per source row** needs at least 1,133,337 PUTs and exceeds the 1m Class A free allowance. Never store every tag or mirror as its own object without cost proof.

[E] **165-250 MB** plus posting overhead and a few revisions looks plausible under 10 GB-month, but actual compressed/uncompressed bytes, daily peak, revision retention and uploads **must be measured**. Import can plausibly avoid weeks because offline conversion and R2 object uploads do not consume D1 row-write quotas. This is **not a D1 import exemption**. There is **no upload-time benchmark**: bandwidth, object overhead, retry count, transform time and billing enrollment remain unknown. Require a publisher manifest reporting bytes, object count, Class A estimates and version digests; abort rather than overrun free budget.

## 3. SQL query parity and table dependence [L/P]
R2 exact object retrieval and prefix listing do **not** provide indexed SQL JOIN/WHERE/ORDER BY. Existing C++ methods synchronously query local SQLite; changing to an API requires async adapters. These are the *observed* native dependencies, not completed projections:

| Feature | Source tables and behaviors that must survive | Required projection |
| --- | --- | --- |
| IMDb titleCatalog/search/titleFacts/matchByTitle | **title**, **genre**; genre include/NOT, exact+normalized/LIKE title, media type, rating/votes, year, runtime, origLang and exclusions, isAnime, episode limits; pagination/sort | Per-tt details, normalized substring/token postings, genre membership/complements, numeric filter buckets and deterministic ranked ID lists |
| MAL animeCatalog/mangaById/search/genreEntries/Counts/discoverPageFiltered | **anime**, **manga**, **tag**, **tag_count**, **classification**; medium/genre, multi-axis AND facets, score/vote-floor, scored_by/members, type/status/year, explicit, weighted rank | Per-MAL-id details, tag/classification bitmaps, count summaries, rank candidates/inputs retaining null and vote semantics |
| Comics series/search/shelf/downloadsFor/curated*/discoverPageFiltered | **series**, **series_stats**, **download**, **download_mirror**, **curated_series**, **curated_genre**, **curated_edition**, **curated_source**, **curated_series_coverage**; publisher/decade, fan/official sources, mirrors, formats, availability, coverage, rank, genre AND | Per-series cards, paged post-to-mirrors and edition-to-sources trees, curated coverage summaries, publisher/genre/format/availability postings and ranked pages |
| Tankoban seriesInfo/volumes | **series**, **volumes** via MAL id; synthesize 1..N from known volume count and overlay saved cover/title fields | Per-MAL-id count plus sparse overrides, bounded volume result pages |

[L/P] Preserve the supplied **398,427 download_mirror**, **57,432 download**, **55,598 canonical_issue**, **9,655 curated_edition**, **127,417 MAL tag** and **127,417 classification** row semantics. **canonical_issue** informs publisher-side curated coverage reconciliation; the runtime coverage method reads the already-derived coverage table. Inventory all remaining tables among the 24 and any extra consumers; current limited inspection is **not an exhaustive table proof**.

[P] Precompute field-scoped exact and **substring/LIKE-equivalent** indexes (ngram postings if needed; prefix-only search would regress behavior), compact sorted/segmented bitmaps per facet/value, numeric bins and global sort orders. Intersect bounded AND sets in the Worker, apply negative filters, caps and stable ties. Precompute common query pages where needed; **do not** enumerate every possible filter combination (combinatorial blow-up), scan all entities per request, or download DBs/packs to clients. Large postings and dynamic ranking may exceed Workers Free **10 ms CPU** or **128 MB**; the exact boundary is untested.

**Mandatory parity gate, not executed:** enumerate every queried table/column/WHERE/ORDER/NULL/tie/limit; compare same-version projection outputs with SQLite oracle for common and worst-case normalized/substr/exact search, genres and NOT, multi-facet AND, missing-data scores, vote floors, ranking, counts, pagination, downloads, mirrors, curated sources/editions/coverage and synthesized volumes. Check shard size/read fanout, index completeness, CPU, memory, subrequests, hashes and publisher idempotency. If any existing supported query fails at acceptable measured limits, **reject** this as a feature-complete replacement.

## 4. Geography, caching, API authorization [O/E/P]
[O] Edge Workers are near callers, but uncached R2 GETs depend on storage placement, geography and distribution; no cross-region latency or p95/p99 measurements exist for this unprovisioned variant. Additional shards/read amplification affect latency and CPU for intersections. Worker CPU spent awaiting network I/O is not billed CPU; parsing/intersection/serialization is. [S2]

[O] **Workers Cache**, updated 2026-10-12, supports tiered pre-execution GET/HEAD cache and request collapsing. A cache HIT skips execution/CPU **but still counts as a Workers request at the standard request rate**. **Authorization-header requests and Set-Cookie responses bypass it.** Thus HMAC guest leases do not automatically yield high pre-Worker cache hits. [S3]

[P] Put catalogue documents in a private R2 bucket; validate guest lease **before** accessing protected records; optionally cache sanitized immutable responses/shards **within** the Worker after verification, so calls still count toward Workers Free. A separate publicly cacheable immutable route is safe only for genuinely public information needing no lease. Keys must include revision, world, dataset, canonical query/filters, locale, ranking, page and encoding; never include bearer credentials in URLs or response bodies. Long-tail terms, cold geography, revisions, auth bypass and low cache reuse inflate R2 reads. No hit-rate/performance claim is made.

[P] A distributable anonymous C++/Qt app cannot cryptographically prove 'Colosseum apps only': clients/guest tokens can be copied and requests imitated. No privileged static secret in the binary. Use server-held issuer/HMAC key, short-lived scoped read leases, issuance throttling, page/range caps, monitoring and publisher-only write credentials. Guest access requires no third-party account. This limits misuse, not eliminates scraping. Local identity, library/history/progress/preferences **remain device-authoritative**; never leak them into shared caches.

## 5. Selective APIs, Universe/Tribute updates, integrity [P/O]
[P] Proposed bounded GET routes: /v1/{world}/records/{id}, /search?q=&filters=&cursor=, /ranked/{kind}?cursor=, /records/{id}/sources?cursor=, /universes/{slug}/manifest, /tributes/{slug}/manifest. Return schema/revision, ETag, stable revision-bound cursor, strict page/field caps, counts, typed 404/partial/version errors. The 20 KB compressed response in workload assumptions is **not a size guarantee**.

[P] Five worlds: **Tankoban, Biblio, Theatre, Universes, Vault**. Tributes are **separate styled franchise experiences**, not a sixth world. Four counted SQLite DBs do not prove all Biblio remote catalogue data is covered; Vault content/local user state stays local. Publisher may update Universe/Tribute manifests for stories, maps, regions, chronology, art URL pointers and text without an app release. New QML/native renderer abilities still require a native release. Assets can be externally hosted; verify actual source usage/redistribution eligibility before serving. Existing cached MusicBrainz record can inform selective enrichment (optional Wikidata); **no full MusicBrainz mirror** or Vinyl/provider research.

[P/O] Publish an immutable versions/{hash}/ object graph with a manifest of object SHA-256, schema version and linked IDs; validate all before moving one active-version pointer. Pin cursor/read/cache to that revision. Retain a prior version for rollback/offline grace. R2 direct reads are **strongly globally consistent per object**, but multi-object uploads aren't a transaction and CDN caches can serve stale overwritten/deleted keys; favor immutable keys and short pointer TTL over in-place replacement. [S6] Rollback flips only the pointer; isolate publisher/admin and guest read routes. No integrity or rollback drill has run.

## 6. Workload sensitivity (assets separately) [E]
Assume 30 days, **30 dynamic API calls per DAU per day**, 20 KB *compressed* per response, all through the same Worker. Let **N** = monthly calls, **h** = fraction requiring **zero R2 GETs** due to effective reuse, and **a** = R2 GETs per miss. Monthly R2 Class B GETs approx **N*(1-h)*a**, ignoring HEAD, manifest, retries and assets. **Every Worker request counts even on Workers Cache hits**.

| DAU | Worker calls/day (monthly) | API transfer/mo | R2 GETs a=1/h=0% | R2 GETs a=4/h=0%,50%,90% |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 3,000 (90k) | 1.8 GB | 90k | 360k / 180k / 36k |
| 1,000 | 30,000 (900k) | 18 GB | 900k | 3.6m / 1.8m / 360k |
| 5,000 | **150,000 (4.5m)** | 90 GB | 4.5m | **18m / 9m / 1.8m** |

[E] 100/1,000 DAU **can** fit nominal Workers/R2 reads under this model, not guaranteed due to account-wide Worker usage, peaks, background requests, token issue, abuse and publisher/asset operations. **5,000 DAU exceeds 100k/day Workers Free even at a 100% cache hit rate**. At a=4, h=0, R2 Class B also exceeds **10m/month**. The 50%/90% hits are scenarios, **not measured**. External image URLs have separate host limits; R2-hosted images have separate Class B/storage costs and may benefit from public CDN cache. Do not count assets as unlimited/free API traffic.

## 7. R2-pending plan, migration, failures, portability [P]
**Pending R2:** Keep the protected staging Worker/two D1 DBs only for **ten verified small complete tables, 24,208 rows**. Explicit unsupported/partial responses for the other fourteen; never pretend complete data or initiate the ~53-day importer. Keep the current native offline SQLite paths pending a separately scoped migration. Staging's passing **7+10 tests** does not establish guest production issuer or client readiness.

**After separately authorized R2 activation:** inventory every source dependency, run offline publisher from source snapshot, validate object/write/storage budgets and all SQLite-oracle comparisons, publish immutable Standard version, shadow API responses, then consider a gated cutover. Final R2-only target uses **no live catalogue SQL database**; temporary D1 fallback is not a hidden permanent dependency. No setup or publishing was performed.

**Native boundary:** CatalogVaultClient presently manages replacement/downloads of the four full SQLite files and each native catalogue synchronously queries SQL. To honor **no bulk DB/content-pack downloads after release**, the future approved API contract must retire that path, use asynchronous C++ network adapters, cancellation, retry/backoff, bounded per-record/page offline LRU (e.g. proposed <=64 MB, never wholesale DB), result-shape parity and graceful 1027/1102/404/offline handling. **No app code was edited.** Existing Colosseum **1.1.9 scope remains frozen**, explicitly excluding Feria and Vinyl.

Failure modes: metered overage/preauthorization suspension; quota/CPU exhaustion; fanout/large shard memory spikes; invalid/partial manifests; broken joins and coverage counts; cache staleness; version mixing; high-cardinality probing/scraping; upstream enrichment outage; regional cold-cache misses; schema/client mismatch. Use fail-closed, immutable rollback, reproducible publisher snapshots, and bounded local stale pages. Portable parts: deterministic JSON, posting files, schema-manifests, publisher. Cloudflare-specific parts: Worker/R2 bindings, cache, auth/issuer, rate handling and billing.

## Primary official sources (six URLs, all checked 2026-10-14)
[S1] R2 Standard pricing, free operations/egress, billable rates, updated 2026-10-01: https://developers.cloudflare.com/r2/pricing/
[S2] Workers Free request/CPU/memory/subrequest limits, updated 2026-10-12: https://developers.cloudflare.com/workers/platform/limits/
[S3] Workers Cache counted requests and Authorization bypass, updated 2026-10-12: https://developers.cloudflare.com/workers/cache/
[S4] R2 subscription checkout prerequisite, updated 2026-04-21: https://developers.cloudflare.com/r2/get-started/
[S5] Billing policy and R2 preauthorization/suspension risk, updated 2026-05-29: https://developers.cloudflare.com/billing/understand/billing-policy/
[S6] R2 per-object strong consistency and cache caveats, updated 2026-04-30: https://developers.cloudflare.com/r2/reference/consistency/

**Evidence boundary:** Official terms and scoped read-only native inspection; baseline counts from user, not recounted. No R2 activation, account billing verification, publisher run, parity/latency/load tests, subscription, code change, app migration, runtime proof or production-readiness claim. Source-specific usage/licensing eligibility was not evaluated.
