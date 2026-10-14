TASK: content-options-06-turso-r2
STATUS: review-ready

# Independent review: Cloudflare Workers Free + Turso Cloud Free (libSQL) + R2 Standard
Checked: 2026-10-14. Scope: architecture/official terms and supplied verified Colosseum baseline only. **No account setup, import, benchmark, code change, or live app test performed.**

## Verdict
**CONDITIONAL for a free-quota, fail-closed staging service; REJECT as a guaranteed hard-$0 production foundation if R2 is mandatory.** Turso removes the specific D1 daily SQL-write bottleneck via a documented SQLite-file import path and 10M monthly *row-written* allowance; neither import accounting nor end-to-end speed has been measured. Workers Free remains a hard 100k inbound requests/day boundary. R2 Standard has perpetual included usage but is a metered subscription: exceeding the included storage/operations can create charges. Cloudflare account billing/R2 activation state is unverified; no paid upgrade or registration is authorized. Keep R2 **off** and keep small rich documents in Turso until an owned-asset case and billing controls justify R2.

Top three decisive tradeoffs:
1. **Import feasibility wins over D1 row replay**: four 165 MB SQLite files can be uploaded through Turso's supported migration path, subject to integrity/format/quota validation; no credible 53-day Turso import estimate exists.
2. **More SQL parity, but a different failure budget**: SQLite/libSQL joins, indexes, FTS/filters can preserve native catalogue semantics server-side; Turso bills scanned rows, so unindexed joins and low cache hit rate can exhaust 500M reads/month.
3. **Operational complexity and hard-$0 exposure**: Worker↔Turso↔R2 adds two providers, two mutable stores, billing exposure on R2, remote latency, and cross-store publication/version coordination.

## Official limits and zero-cost gates [S1-S4]
| Component | Observed recurring terms | Consequence |
|---|---|---|
| Turso Cloud **Free** | $0, no card at signup; **100 databases**, **5 GB storage**, **500M rows read/month**, **10M rows written/month**, **3 GB monthly embedded sync**, **1-day point-in-time restore** [S1] | Adequate *nominal* storage for 165–250 MB base; indices, JSON duplication, versions and backups count toward capacity. Sync quota is **not** a monthly HTTP response/egress allotment. |
| Turso quota enforcement | Per its usage guide, exceeding a limited metric produces **BLOCKED** query errors; writes count inserted/updated rows, including work in aborted transactions; reads are **rows scanned**, including joins and aggregates [S2]. Pricing advertises overages only once enabled [S1]. | Keep overages **disabled** and test billing flags before any provider operation. Quota exhaustion means partial outage, not automatic resilience. Exact Free account state not accessible. |
| Cloudflare Workers **Free** | **100,000 requests/day** (UTC reset), **10 ms CPU/request**, 128 MB memory, 50 normal subrequests/request, six concurrent outgoing connections; free limit returns **1027** and route fail-open/fail-closed is configurable [S3]. | Must fail closed on protected API routes; fail-open would bypass the API gateway. Network wait is excluded from CPU, JSON parse/auth/signing are not. No p95 or execution measurements. |
| R2 **Standard** | **10 GB-month**, **1M Class A operations/month**, **10M Class B operations/month**, free egress; beyond included usage **$0.015/GB-month**, **$4.50/M Class A**, **$0.36/M Class B** with billing-unit rounding [S4]. | **Not a hard-capped $0 service.** Infrequent Access has *no* free tier, so do not use it. Requires separate R2 subscription/checkout and possible billing-method handling; account registration not attempted. |

**Hard $0 truth:** Turso with overages disabled and Workers Free can stop when quotas run out, but availability is not assured. R2 provides no documented automatic hard free-tier cut-off in the checked pricing terms; budget alerts, application limits, or hoped-for low traffic are not guarantees against charges or storage/read overages. R2 account billing can also affect availability. Under a literal “cannot incur charges” requirement, omit R2 instead of promising $0. No paid credits, temporary trials, or paid fallback are assumed.

## Bootstrap and import accounting [S1,S2,S5]
**Supplied baseline (not remeasured):** four SQLite files, 164,626,432 bytes (~157 MiB), 24 tables, 1,133,337 rows. D1 needs ~2.82M indexed writes (~3.18M with ledger), ~53 days at 60k/day. **D1 write costs/timing do not transfer to Turso.**

Turso documents `turso db import` and authenticated **SQLite-file upload** after WAL checkpoint [S5]. Four imports fit the 100-database count, but cross-database JOINs are not assured; merge source copies only after schema collision checks. This path avoids D1-style row-by-row replay. **Import write-meter exemptions and throughput are undocumented/unmeasured; do not assume either.**

**Write model (estimate only):** 1,133,337 base writes = **11.33% of 10M/month**, leaving an **8.82×** nominal amplification budget. Turso index-maintenance and file-upload accounting are **not** proven equivalent to D1 writes. Check before/after Turso billing counters with an authorized disposable sample; retry/trigger costs remain unknown.

**Import acceptance gate:** On source copies, validate integrity/foreign keys, 24 table counts, indexes/triggers, FTS/collations/ranks and golden query results; check WAL/upload acceptance and provider usage counters on one authorized sample. No import run. **Never distribute the database to clients.**

**R2 objects:** one JSON object per source row means **1,133,337 Class A uploads**, over the 1M free monthly limit even before retries/assets and potentially billable with rounding. Keep mirror/join tables in Turso; optional detail-by-title objects for 61,585 IMDb + 40k MAL + 22,710 comic + 10k Tankoban titles total **134,295 candidate objects** (illustrative, not an upload mandate). Budget art and retained versions separately.

## Query and feature parity contract (proposal, not proof)
| Existing function | Explicit known source dependencies | Required server behavior / validation |
|---|---|---|
| Theatre movie/TV search, genre browse, ranked lists | IMDb `title` (61,585) + `genre` (143,615); other unnamed tables TBD | Indexed title/alias/prefix or measured FTS; stable rank+ID ordering; `EXISTS`/joins for **AND** facets, cursors, result counts; compare native sample and negative cases. |
| Theatre anime/manga facets | MAL `anime` (20k), `manga` (20k), `tag` (127,417), `classification` (127,417) | Preserve join/filter semantics, deduplicate IDs, tag relationships, AND intersections, deterministic pagination and ranking. |
| Tankoban manga/comic coverage and downloads | Tankoban `series` (10k), `volumes` (28,839); comics `series` (22,710), `canonical_issue` (55,598), `download` (57,432), `download_mirror` (398,427) | Indexed series→issue→source/mirror joins and link ordering; volume synthesis/issue mappings must match C++ outputs. **Never** replace mirror joins with client full scans or bulk JSON packs. |
| Curated coverage | Comics `curated_series` (1,134), `curated_edition` (9,655), plus relationship tables TBD | Exact edition relationships, scoped joins, eligibility and sort semantics; document and test missing/duplicate editions. |
| Biblio, Universes, Vault; Tributes (separate experience type) | Existing four SQLite inventories **do not establish** full datasets/routes for these experiences | Treat as separate content contracts; immutable franchise/version manifests for Universes vs Tributes, local Vault behavior untouched. Do not claim five-world parity from these 24 tables. |

Unlisted dependencies among the **24 tables** require a complete native-C++ query→table ledger and golden parity tests. Existing D1 staging (24,208 rows in 10 complete tables, 14 missing, 7 Worker + 10 publisher tests passing) does **not** establish Turso readiness. Qt/C++ needs async cancellable paged API adapters and bounded offline caches. **Do not expand 1.1.9 to Feria/Vinyl.**

## Query compatibility, locality and cold cache
libSQL permits credible SQLite-style indexed joins, FTS, ranking and filters, **not guaranteed parity** for old tokenizers, extensions, collations/UDFs or query plans. Whitelist parameterized queries, cap page sizes, preserve stable cursors; test AND facets via indexed intersections or grouped `HAVING COUNT(DISTINCT facet)` against fixtures. `COUNT(*)`, joins and wildcard scans can charge many more read rows than returned [S2]. Only precompute narrow rankings after measured plans.

Global Workers still fetch from regional Turso on cache misses; nearby free replicas are **not assumed**. No cold/warm latency claims. Future benchmarks must capture p50/p95/p99, CPU, scans, Turso fan-out, R2 origin Gets, cache TTL/version coherence and throttled-network failures.

## 30-day demand model: 30 API calls/DAU/day, 20 KB compressed response
Assume the full 30 calls enter a Worker, one SQL query on each Turso miss, **100 rows scanned/query** in the shown read-budget column, a 30-day month, and no additional bot/publisher traffic. Edge hit rate is on *whole API responses*; 80% is a sensitivity case, **not** an observed cache rate. GB below is decimal HTTP response volume, **not** R2 egress billing or Turso embedded-sync consumption.

| DAU | Worker requests/day (month) | Compressed API payload/mo | Turso backend queries/mo, 0% / 80% edge hits | Turso rows scanned/mo at 100 rows/miss, 0% / 80% | Worker Free fit |
|---:|---:|---:|---:|---:|---|
| 100 | 3,000 (90,000) | 1.8 GB | 90k / 18k | 9M / 1.8M | Yes, nominally |
| 1,000 | 30,000 (900,000) | 18 GB | 900k / 180k | 90M / 18M | Yes, nominally |
| 5,000 | **150,000 (4.5M)** | 90 GB | 4.5M / 900k | 450M / 90M | **NO: exceeds 100k/day by 50k** |

**Read-amplification sensitivity:** At 1,000 rows scanned/uncached SQL call, the same Turso rows read become **90M/18M** (100 DAU), **900M/180M** (1k), and **4.5B/900M** (5k) for 0%/80% hit rates. Thus 1k DAU with costly joins already fails Turso's 500M/month limit absent cache/index changes; 5k fails Worker even with perfect *internal Worker* caching, because incoming calls still count. With 5k and zero Turso cache, even **112 scanned rows per API call** exceed 500M/month (4.5M×112=504M). Backend SQL fan-out of 2–3 queries/miss amplifies Turso reads and outgoing fetches; never price the system as one row read or one query per HTTP call. A served empty query can still incur scans.

**R2/asset sensitivity:** At 1 uncached R2 Get/API call: **90k/900k/4.5M** Class B monthly at 100/1k/5k DAU; at 3 Gets: **270k/2.7M/13.5M**, breaching the 10M R2 allowance at 5k. Multiply by fraction using R2 and origin-cache misses. Assets are separate: 5 images per API view at 5k DAU means **22.5M client image requests/month**; if external, none hits R2; if all rehosted and uncached, it breaches R2. R2 egress is free [S4]; client requests are not themselves R2 operations on CDN hits. Actual asset counts/size unknown.

Apply Worker throttles, indexed bounded queries, compressed DTOs, ETags, circuit breakers and usage alerts with margins below 100k/day, 500M SQL reads/month and 10GB/1M A/10M B R2. **These are mitigations, not enforceable $0 billing caps.**

## Publishing, cache/auth/isolation, rollback and offline (proposal)
**While R2 is pending:** Store small rich detail JSON in Turso; leave the protected D1 staging environment untouched. If later authorized, move only reusable immutable documents/owned assets to R2 Standard; keep indexed search/facets/mirrors in Turso. **One active version pointer**; never serve two competing catalogue authorities.

**Publish/rollback:** Admin validates schema, content hashes, join/coverage fixtures and client compatibility; uploads immutable `universes/<slug>/<version>/...` and distinct `tributes/<slug>/<version>/...` manifests, then transactionally switches a Turso active-version pointer. APIs return version, hashes and selective pages. Pin a request chain to its version and preserve old objects through cache TTL/client grace period. **No cross-service R2/Turso transaction**: handle stale pointers and roll back by pointer switch, never in-place object overwrite.

**Content policy:** Existing QML renderer supports Universe/Tribute data updates without releases; new native capabilities or schemas require releases with `min_client_version`. Worlds: Tankoban, Biblio, Theatre, Universes, Vault; Tributes are *separate experiences*. Cache MusicBrainz public API enrichment (optionally Wikidata), **not** a full mirror. External art URLs need not be imported into R2.

**Cache/auth:** Offer short-lived (~30-minute) signed guest leases without signup; Turso's privileged token stays in Worker secrets, publishing write credentials separate, none in C++ binary. Do **not** share-cache admin/lease-dependent responses. Authenticate before gated cache lookup; cache public immutable pages by version, locale/filter/page with TTL and `ETag`, bounded invalidation and stale fallback. Apply rate/page/scan limits against replay and enumeration.

**App-only limit:** Guest desktop binaries and their protocol can be copied/scripted; a signup-free lease cannot attest an untampered Colosseum app. CORS, user-agent and hidden keys are not proof. Keep local identity/library/history/progress/preferences authoritative; LRU-cache only visited page/detail DTOs with version/hash and explicit offline-stale UI, never replica/DB/full packs. Back off on 429/5xx/1027/Turso BLOCKED.

## Failure modes, effort, and portability
- **Outages/cost:** Worker 1027 at daily cap or 1102 at CPU cap; Turso BLOCKED read/write/storage quota, regional network faults, API token expiration/revocation, expensive SQL scans; R2 404/rate/checkout/billing interruptions or unexpectedly metered storage/reads. Fail closed on admin routes and protect public metadata against cache poisoning/version mismatch. Free plan has only 1-day Turso PITR [S1], so keep verified, separately controlled offline source snapshots.
- **Data correctness:** Four-DB cross-join assumptions, SQLite collation/FTS differences, incorrect ranked lists, missing mirror or curated joins, broken range/pagination semantics, inconsistent version across R2/SQL, stale CDN pointers, remote-response ordering/cancellation in QML. Require reproducible query plans, golden source-vs-API fixtures and deliberate offline/retry tests before wiring a client.
- **Effort:** Medium–high: operator Turso registration/import checks, Worker secrets/client, indexed typed endpoints, version/rollback pipeline, quota monitoring, Qt async adapter/offline cache and parity tests. Existing D1 tests do not establish Turso integration.
- **Exit path:** Preserve canonical SQLite source exports and migrations, JSON schema/manifest specification, SQL fixtures and provider-neutral Worker handler boundaries; Turso is libSQL/SQLite-aligned but not a drop-in replacement for every pragma/extension; R2 object API is S3-like but version pointers and authorization are app contracts. Supabase announced on **2026-10-02** that it is acquiring Turso; it says existing databases/APIs/workflows continue, but this does not guarantee unchanged Free quotas or product roadmap [S6]. No automatic migration to PostgreSQL, no new account action here.

## Alternative that could beat this stack
**Workers Free + Turso Free storing rich details directly (NO R2)** is better for the current size and strict no-paid-foundation preference: preserves SQL, offers the documented direct-file import, avoids R2 subscription, Class A object bootstrap charges and dual-store consistency, and works while R2 is pending. Move owned high-volume or rich immutable assets to R2 **only** if authorized and a measured ledger shows value within free limits. A *much smaller proven* D1 search projection + Workers Static Assets (no R2) might be simpler long-term and avoid Turso dependency, **but** cannot be recommended until it proves all search, facets, mirrors, curated joins and ranked lists and resolves the already evidenced D1 write-budget problem. For the assigned Turso+R2 combination, **no production approval** without explicit billing disposition, sample import/usage proof, full query parity, and workload monitoring.

## Primary official evidence (six URLs, all checked 2026-10-14)
- **[S1]** Turso Cloud pricing and Free quotas, overage language: https://turso.tech/pricing
- **[S2]** Turso usage semantics, rows scanned/written, BLOCKED, indexing cost: https://docs.turso.tech/help/usage-and-billing
- **[S3]** Cloudflare Workers Free limits, CPU and exceeded-request behavior: https://developers.cloudflare.com/workers/platform/limits/
- **[S4]** Cloudflare R2 Standard free tier, billable operation categories/rates: https://developers.cloudflare.com/r2/pricing/
- **[S5]** Turso official SQLite-file import and authenticated upload guide: https://docs.turso.tech/cloud/migrate-to-turso
- **[S6]** Turso's 2026-10-02 Supabase acquisition/continuity announcement: https://turso.tech/blog/turso-is-joining-supabase

**Evidence boundary:** The numerical Colosseum source counts, D1 state, tests, and projected 53-day import are supplied verified premises, not independently reproduced here. Price/limit statements above are observed provider publications as of the checked date; load, cache rate, SQL query plans, object counts, migration speed, server health, R2 billing state and integration readiness are estimates or open validations, **not** executed evidence.
