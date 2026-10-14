TASK: content-options-01-d1-r2
STATUS: review-ready
CHECKED: 2026-10-14. Independent official-document review; no code, deployment, billing or runtime changes.
COMBINATION: Cloudflare Workers + D1 search/filter index + R2 Standard JSON/assets.

## Verdict: CONDITIONAL; not approved as guaranteed $0

A thin query-complete D1 index with selective immutable R2 rich records is architecturally plausible. It is **not production- or release-ready**: full indexed D1 import takes weeks, R2 is a metered service with billing exposure even inside a recurring free tier, and 5,000 daily users * 30 Worker requests exceed the Workers Free cap. **If zero possible recurring charge is an absolute condition, reject R2 as a mandatory production dependency.** Otherwise consider a <=1,000 DAU measured pilot only after projection parity, quota and billing approval. No paid plan or R2 activation is authorized.

Top three tradeoffs: (1) D1 server-side joins/ranks/AND facets versus indexed writes and scanned-read quotas; (2) R2 object-level content updates/rollback versus metered operations and two-store consistency; (3) guest Worker API simplicity versus 100k/day cap and inability to authenticate a copied desktop binary.

## Baseline and evidence boundaries

User-provided verified facts, **not repeated by this reviewer**: four SQLite databases, 164,626,432 bytes, 24 tables, 1,133,337 rows. Estimated 2,823,109 indexed D1 writes, ~3.18m including 32-row chunk ledger. Two D1 staging databases and protected Worker exist; 24,208 rows across 10 completed small tables, 14 tables unimported, verified cached MusicBrainz record, 7 Worker and 10 publisher tests passed. R2 unregistered/unavailable; account billing unverified; production guest issuer and native remote adapter missing. Current C++ uses synchronous local SQLite. 1.1.9 excludes Feria and Vinyl. Research estimates below are **not** benchmark results.

## Official Free terms, cost traps and disqualifiers

| Component | Official terms checked 2026-10-14 | Impact |
|---|---|---|
| Workers Free | 100,000 incoming Worker requests/day/account, midnight UTC reset, 10ms CPU/request, 128MB isolate, 50 ordinary external subrequests/request (separate internal-service limit). 1027 at request limit; CPU excess 1102. [S1] | Cache inside Worker saves origin use, not the incoming Worker count. Protect routes with fail-closed behavior. |
| D1 Free | 5m rows read/day, 100k rows written/day, 5GB storage total/account. Index writes count, including index creation; Wrangler/dashboard SQL consumes quotas. At read/write cap queries fail; storage cap blocks writes/DDL. No D1 egress fee. [S2] | Quotas shared by all D1 databases, not multiplied by shards or import tools. |
| D1 hard limits | 10 DBs/account on Free; 500MB/DB; 7-day Time Travel; 100 query parameters, 100KB SQL statement, 2MB max row/BLOB, 50-byte LIKE/GLOB pattern, 30s query, 5GB execute import file. [S3] | Watch per-DB size and long Unicode searches. |
| R2 **Standard** | 10 GB-month storage, 1m Class A/month (including PUT/LIST), 10m Class B/month (GET/HEAD), free internet egress. Above: $0.015/GB-month, $4.50/million A, $0.36/million B with rounded billing units. No Free allowance for Infrequent Access. [S4] | Rich JSON, art, retained versions, public object reads and repeated publishing share the same metered account budget. |
| Billing | Valid payment method for enabling subscriptions, possible preauthorization. Payment failure can suspend R2 access; related data may be deleted after 30 days unresolved. [S5] | R2 is **not yet enabled**; present account eligibility/billing state cannot be asserted. |

**Hard $0 verdict:** Workers/D1 on Free stop serving at quota limits rather than silently transitioning to a paid plan. R2's recurring free *allowance* is not a documented hard financial ceiling. Alerts and application throttles are not guarantees against billed Class A/B or storage overages, especially if R2 is exposed publicly. No subscription or payment action was taken. If the user forbids any possibility of charges, the assigned architecture **fails that requirement** even when projected average usage seems low.

## Bootstrap arithmetic and required alternative to a full mirror

Cloudflare confirms dashboard, Wrangler, INSERT and CREATE INDEX work all count against D1 metrics [S2]. SQL batching, deferred indexes, multiple DBs and different importers are **not** exempt. No full SQLite/DB pack may be shipped to app clients.

| Import shape (estimate, NOT observed execution) | Billable D1 writes | Min at 100k/day | At 60k/day |
|---|---:|---:|---:|
| All 1,133,337 raw rows, before indexes | >=1,133,337 | 12 days | 19 days |
| Supplied indexed projection | ~2,823,109 | 29 days | 48 days |
| Supplied indexed + chunk ledger | ~3,180,000 | 32 days | 53 days |
| 134,295 hypothetical title entities + one index each | >=268,590 | 3 days | 5 days |
| Same entities + two indexes each | >=402,885 | 5 days | 7 days |

**Corrected root counts (parent-verified):** all root tables total **135,429**, including 1,134 curated series. Attaching 10,000 Tankoban IDs to matching MAL manga yields **125,429 rich-root documents**. The 134,295 illustration above is only a pre-dedup *subset* (IMDb 61,585 + MAL 40,000 + comics 22,710 + Tankoban 10,000), excluding curated series; it is not the 125,429-R2-document count. FTS/facet postings, rank, revisions and ledger raise D1 writes. At 60k/day, <=7 days allows <=420k writes; <=3 days, <=180k. Measure D1 query meta, not a speculative import exemption. R2 PUTs do not erase D1 writes.

**Proposed split (not yet proved):** D1 holds one compact searchable record per entity (stable source/world ID, type, title/search tokens, ranks/sort, indexed AND facet membership, curated/provider availability where queryable, object revision/hash/key). R2 holds only requested rich entity/issue/volume JSON, mirror arrays, franchise scene nodes and permitted assets. D1 returns bounded ranked pages with deterministic keyset cursors; rich GET resolves specific IDs. Neither Worker nor native Qt client may scan/download full R2 inventories or bulk content packs to answer search.

## Mandatory table-to-feature compatibility map

| Existing feature | Known source dependencies and parity condition |
|---|---|
| Theatre title search/ranks/AND genre/ID | IMDb title 61,585; genre 143,615. Indexed D1 title/rank, complete genre membership or equivalent postings; rich detail R2. Unindexed JSON genres are not equivalent. |
| MAL anime/manga classification/IDs | anime 20k, manga 20k, tag 127,417, classification 127,417. Indexed typed IDs and combinable facets; rich R2 metadata. Preserve relevance, ranks and mixed-filter behavior. |
| Comics curated/issue/downloads | series 22,710, canonical_issue 55,598, curated_series 1,134, curated_edition 9,655, download 57,432, download_mirror 398,427. D1 must index browsable/searchable curated, issue, edition and availability keys; R2 detailed per-ID download/mirror arrays. Existing provider/mirror filters cannot disappear into unindexed JSON. |
| Tankoban volumes/series | series 10k, volumes 28,839. D1 identity, search/rank and queried counts/facets; R2 stable ordered volume records with bounded pages and synthesis rules. |
| Biblio, Universes, Tributes, Vault, other tables | Inventory the remaining of all 24 tables and native call sites before declaring parity. Universe/Tribute narrative JSON needs revisioned manifests; Tributes remain separate. Vault and local user state remain authoritative locally. Biblio SQL dependencies unknown, not assumed absent. |

**Gate:** enumerate every native query and each source table, map to endpoint/index/R2 fields; compare source SQLite to projected results on titles, order/ties, AND facets, genre joins, curated gaps, downloads/mirrors, ID/volume synthesis, negatives, paging and locale. Capture actual D1 meta.rows_read/rows_written, stored bytes and R2 PUT/object counts. The ten imported small tables do not establish large-table or feature parity.
## Query capability, geography and cold-cache concerns

D1 counts scanned, not returned rows. A 134,295-row scan spends 5m reads in ~38 misses. Validate ranked AND facets, genre joins, pagination counts and text tokenization with EXPLAIN QUERY PLAN and actual meta.rows_read. LIKE/GLOB patterns max 50 bytes [S3], problematic for long UTF-8 searches. Search postings also raise D1 write/storage costs.

Global Worker execution does not imply local D1 execution: the D1 primary lives in a region. D1 read replication has no extra replica fee [S2], but replica reads require the appropriate Sessions API/bookmarks for consistency; lag or routing to the primary can remain. R2/CDN cold misses add origin fetches; caching effectiveness varies by region, key and freshness. Measure India and other relevant regions separately, p50/p95 cold and warm. Worker CPU 10ms is separate from network wait, yet JSON materialization/verification may exceed CPU; no live benchmark has been run.

## Rich records, Universe/Tribute publishing and local ownership

**Selective API proposal:** GET /v1/search?world=&filters=&sort=&cursor=; /v1/entity/{world}/{id}; /v1/entity/{id}/issues-or-volumes?cursor=; /v1/content/manifest/{universe-or-tribute}; individual hash/version-addressed scene nodes. Use 20-50 item bounded result pages and stable IDs; no table dump, whole catalogue, R2 manifest of every rich record, database download or client-side full scan. External art URLs are acceptable; copy/host third-party assets only when source usage terms permit. MusicBrainz remains selective cached upstream enrichment, not an entire mirror.

**Publication order:** upload schema-validated immutable R2 objects with hashes; validate manifest, renderer version and references; write candidate D1 projection under quota; atomically flip the D1 active-revision pointer **only after** all objects resolve. Retain bounded prior D1/R2 versions for rollback; reject missing objects, verify hashes and garbage-collect orphan keys. **D1 and R2 lack cross-service transactions.** D1 7-day Time Travel [S3] cannot restore deleted R2 objects. Retained copies use billable R2 storage.

Universe and Tribute stories, layout data, labels, node relationships, artwork references and metadata can update via versioned JSON without a native app release **only within already shipped native/QML capabilities**. Include schema and min_renderer_version in manifest, preserve last compatible revision for older clients. New native scenes/interaction mechanics require a new app release. Tributes are distinct styled franchise experiences, not conflated with Universes.

Current synchronous local SQLite cannot be swapped to HTTP calls without application work. Native Qt/C++ adapters must be asynchronous: deterministic cancellation, pagination, bounded read-ahead, retry/backoff, per-record disk LRU, ETag/revision/hash validation, offline last-good fallback, negative-cache expiry and visible partial/unavailable states. The client's local identity, library, history, progress and preferences are always authoritative; server catalogue revisions must never overwrite them. No fallback bulk catalogue download to disguise an outage.

## Security, guest issuance, authorization and caching

**App-only access is unenforceable:** anonymous Qt binaries, headers, issuer and leases can be copied; CORS is not authentication. Treat guest metadata as public; never embed privileged keys. Use server-only publisher secrets, scoped short (staging: 30-minute) HMAC read leases, allowlisted SQL/filter/page inputs, anti-abuse rate limits and monitoring. Leases can be replayed and do not prove a genuine app. Guests require no third-party signup.

Private R2 rich records go through a scoped Worker; intentionally public art/CDN URLs are shareable, with metered R2 origin misses. Cache public search by normalized query/filter/sort/cursor/schema/revision, never private progress, guest tokens or publisher replies. Use immutable hashed content URLs; explicitly configure JSON caching. Worker cache saves origins, **not** Worker calls. Direct static assets bypass Worker quota only if no Worker executes.

## Demand sensitivity (arithmetic, not observed traffic)

Assumptions: 30 dynamic Worker-routed API calls per DAU per day, 20KB compressed returned/call, 30 days/month, **assets excluded**. Let h = backend cache-hit fraction and r = D1 rows read per origin miss. Worker calls/day = 30*DAU regardless h. D1 read rows/day = 30*DAU*(1-h)*r. Illustrative R2 Class B GET/month = Worker calls/day*30*(1-h) if exactly one GET per miss; real endpoints may make zero or several GETs.

| DAU | Worker calls/day (month) | API payload/day (month) | D1 rows/day at r=100: h=0 / 80% / 95% | R2 GET/month at h=0, 1 GET/miss |
|---:|---:|---:|---:|---:|
| 100 | 3,000 (90k) | 60MB (1.8GB) | 300k / 60k / 15k | 90k |
| 1,000 | 30,000 (900k) | 600MB (18GB) | 3m / 600k / 150k | 900k |
| 5,000 | **150,000 (4.5m): Workers Free FAIL** | 3GB (90GB) | 15m / 3m / 750k | 4.5m |

At r=500, multiply D1 columns by 5. Thus 1k DAU, h=0 requires 15m D1 read rows/day (over 5m); 5k DAU, h=80% requires 15m too. The 5k Worker/day failure remains even if h=95% and D1 fits. Origins can be more expensive than returned rows because index scans and joins count. No unlimited-free throughput claim is valid. Budgets are shared with other uses of the same account and guest issuance.

**Assets separately:** example 50 uncached image/art GETs/DAU/day gives 0.15m, 1.5m, 7.5m R2 Class B/month at 100, 1k, 5k DAU. At 5k plus the table's 4.5m API object GETs, 12m Class B/month **exceeds** R2's 10m free allocation; CDN cache hits may reduce origin reads but are unmeasured. 20KB API payload sizes do not bound JSON-at-rest or artwork size. Worker-proxied assets compete for Workers Free; direct static/CDN assets avoid Worker calls but sacrifice claims of client exclusivity. Class A publisher PUTs, HEAD, old revisions and other account activity require separate measurement.

## Failure modes, transition while R2 is missing, and migration effort

Failure cases: Workers 1027/1102, D1 quotas/storage/scan regressions, regional latency, import stalls, guest scraping, R2 overage/suspension, partial D1/R2 publication, stale CDN or missing objects, ID collisions, broken upstream art/mirrors, offline or incompatible native renderer. Fail closed on protected routes; expose explicit unavailable/retry state, stale offline cache and per-route rows_read/rows_written, latency and R2 operation gauges.

**While R2 pending:** retain exactly the current partial protected D1 staging state; 10 complete tables do *not* make the other 14 and rich JSON available. Add a storage abstraction in a future implementation, able to report 'R2 not configured'. Do not pretend all content can be served from 24,208 staging rows, import entire DBs or change the 1.1.9 feature scope to compensate. Once the account owner separately approves/activates R2, stage immutable objects and manifests, verify checksums and D1 projection, then switch a revision pointer. This review did not register R2, deploy or test an application.

Portability: versioned plain JSON, deterministic IDs and hashes, neutral HTTP endpoint contracts, regenerable SQL projection and a replaceable object resolver reduce lock-in. R2 is broadly object/S3 shaped, but alternative blob/CDN storage still requires publication tooling, caches and migration checks. Rebuilding joins/rank behavior for another database is real work; not a URL switch.

**Alternative for strictly zero bill risk:** Workers + minimal D1 search index + **Workers Static Assets** for immutable, selectively addressable JSON page shards/art. Static requests without Worker execution are free/unlimited [S6], subject to 20,000 files/version and 25MiB/file on Free [S1]. **125,429** individual rich roots cannot fit as one file each. Bounded page shards must preserve search/facet parity and must never form bulk content packs. Dynamic searches still hit 100k Worker/day. R2-only posting indexes avoid D1 import but need unproven multi-object facet/ranking logic; not preferred.

**Recommendation: hold/reject full D1 mirror.** For a conditional pilot demand: (1) explicit 24-table native SQL -> index/R2 parity matrix and golden result-order tests; (2) <=420k *observed* billable D1 bootstrap writes for a <=7-day plan at 60k/day, with indexes; (3) bounded D1 reads, per-DB storage and 10ms Worker CPU; (4) actual Indian/geographically diverse cold/warm p50/p95 and backend cache ratio; (5) demonstrated version rollback/offline behavior; (6) R2 payment/overage policy explicitly approved or R2 replaced. 5k DAU dynamic Worker traffic remains a disqualifier on this Free topology.

## Primary official sources (six URLs; checked 2026-10-14)

- [S1] Workers limits, updated 2026-10-12: https://developers.cloudflare.com/workers/platform/limits/
- [S2] D1 pricing and row metrics, updated 2026-04-21: https://developers.cloudflare.com/d1/platform/pricing/
- [S3] D1 limits, updated 2026-04-21: https://developers.cloudflare.com/d1/platform/limits/
- [S4] R2 pricing, updated 2026-10-01: https://developers.cloudflare.com/r2/pricing/
- [S5] Cloudflare billing policy, updated 2026-05-29: https://developers.cloudflare.com/billing/understand/billing-policy/
- [S6] Workers Static Assets billing, updated 2026-04-23: https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/

END: Independent bounded research. No account action, source edit, deployment, cloud call, other worker inspection or performance test.
