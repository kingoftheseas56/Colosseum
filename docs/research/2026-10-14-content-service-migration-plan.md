# Colosseum service migration: portable architecture and rollout

Date: 2026-10-14. Status: migration design and source inventory; provider qualification remains open. This document supersedes the initial infrastructure recommendation, while retaining that document's research evidence. R2 registration is pending. This task creates a plan, not a new deployment or native release.

Benchmark preparation is now verified locally: [evidence and next measurements](2026-10-14-content-benchmark-results.md), [executable runbook](../../server/content-api/benchmark/README.md), and [Tankoban acceptance matrix](2026-10-14-content-benchmark-acceptance.md). Hosted D1/Turso comparison, deployed asset delivery and installed-app receiving remain pending; local fixture timings do not select the backend.

## Decision

Hemanth selected **Workers Free + D1 with a reduced catalogue projection**, superseding the provisional Workers + Turso recommendation. Keep one Colosseum API, portable exports and independent object-storage selection. Existing D1 staging remains the starting point; selection is not production qualification or a claim that D1 won hosted latency tests. Capacity, bootstrap/update accounting, semantic parity and hosted query qualification remain required. External asset URLs and the small owned Static Assets collection can serve the initial scope; R2 remains optional and unregistered.

Read-only Cloudflare connector checks on 2026-10-14 confirmed an existing accessible account and both Colosseum D1 staging databases. No new Cloudflare account is required for this path. Database listing does not confirm billing/free-plan settings, source completeness or service readiness; no resources or account settings were changed.

Release sequencing is **1.1.9 no-account first**, then **1.1.9 with-account** after existing Neon access is migrated from the flagged GitHub account to Hemanth's Gmail and the account integrations are qualified. Neon recovery is outstanding and does not gate catalogue access or local use. Both editions retain canonical local library, history and progress. No-account catalogue access uses app-scoped guest leases without Colosseum login or a Neon dependency; these controls do not prove a genuine client. Account features remain optional in the later edition. Neither edition includes Feria or Vinyl.

Full raw-table replication into D1 is **not the default migration path**. The measured source/index/ledger model is about 3.18 million writes, or 53 daily import windows at the current 60,000-write safety allowance. A narrower projection helps, but neither moving payloads to R2 nor importing SQL files exempts D1 writes. A different free SQL backend may be a better choice if it preserves semantics and substantially improves bootstrap and update costs.

R2 is optional object storage, not the database or API engine. It can improve delivery when immutable objects are cached and images are optimized; it does not automatically accelerate SQL or uncached calls. URLs are valid asset records. Store owned assets only when that adds reliability or publication control.

The user can update supported Universe and Tribute content without an application release after this migration. New layouts, renderer capabilities and application behavior still require an application release. Remote content never becomes executable QML/JavaScript.

## Non-negotiable requirements

- Recurring infrastructure must fit free plans. No paid trial, credit rollover, automatic paid fallback or GitHub REST dependency.
- Applications fetch requested searches, pages, details and scene sections. No full DB, embedded-replica sync, content-pack installation or background crawl reconstructing a complete catalogue.
- Colosseum owns identity, library, history, progress and preferences locally. Existing `ct1:` identities, source IDs, aliases and progress keys survive the migration.
- Initial API consumers are Colosseum apps. Guest use works without third-party signup, with short-lived leases and abuse controls; no administrative/signing secret ships in the binary.
- Core local files, readers, playback and library remain usable offline. Previously viewed content may be served from bounded last-good caches.
- There are five worlds: Tankoban, Biblio, Theatre, Universes and Vault. Tributes are separate styled franchise experiences.
- The already agreed 1.1.9 scope excludes Feria and Vinyl. This migration does not reopen those exclusions or add music UI.

## What exists today

The isolated implementation branch is `codex/content-api-free`, baseline commit `54a53c03`. It contains a protected staging Worker, two D1 databases, source schemas, a resumable publisher and tests. The staging service has 24,208 imported rows in ten complete source tables; fourteen source tables remain unimported. A MusicBrainz artist was fetched, cached and read successfully. Seven Worker and ten publisher tests passed in the preceding implementation task. These are component/staging results, not native application or production qualification. The [staging README](../../server/content-api/README.md) records the exact imported tables, contracts, verification and remaining gaps; `/admin/status` is the live state authority, not this dated snapshot.

The native application is not wired to the new API. Production guest/account token exchange is missing. R2 is not registered. Cloudflare billing/subscription state was not independently confirmed because subscription inspection was denied; a free-only production gate remains. No importer is currently running. Do not continue the expensive full-copy import merely to keep it busy while architecture is reviewed.

The four inspected SQLite files contain 1,133,337 rows across 24 tables and total 164,626,432 bytes. The user's approximately 250 MB estimate remains a capacity envelope until other datasets are inventoried; the measured four files are not proof that every future catalogue fits that size.

| Source | Measured bytes | Core inventory |
| --- | ---: | --- |
| IMDb | 21,528,576 | 61,585 titles; 143,615 genre relationships |
| MAL | 41,381,888 | 20,000 anime; 20,000 manga; two 127,417-row relationship representations |
| Comics | 98,865,152 | 22,710 series; 57,432 downloads; 398,427 mirrors; curated editions, claims and coverage |
| Tankoban | 2,850,816 | 10,000 series; 28,839 volumes |

Detailed source evidence: [catalogue projection budget](2026-10-14-catalogue-projection-budget.md), [five-world and Tribute inventory](2026-10-14-universe-content-migration-inventory.md), and [1.1.9 native integration seams](2026-10-14-content-api-119-rollout.md). Earlier [infrastructure research](2026-10-14-content-api-infrastructure-plan.md) retains the Vinyl investigation; it is not repeated here.

## Portability contract

```text
Installed C++/QML app and existing local state owners
    -> HTTPS /v1 semantic API + short-lived read lease
        -> QueryStore: search / facets / detail summaries / version readiness
        -> ContentObjectStore: bounded immutable JSON sections and rich details
        -> Asset descriptors: external URLs or owned immutable objects
        -> Optional enrichment cache: MusicBrainz / Wikidata

Read-only source snapshots + curated content
    -> local deterministic exporter and validator
    -> portable records, relationships, objects and hash manifest
    -> selected database/object publishers
    -> verified candidate revision -> atomic activation pointer
```

The app sees Colosseum's API, stable IDs and semantic results. It never sees a D1 table name, R2 bucket key as authority, SQL dialect or vendor credential. Hostname configuration and signed/server-authenticated revision responses remain under Colosseum control. Content schema version, API major version and publication revision are separate fields.

`QueryStore` is a server boundary for actual operations, not arbitrary SQL passthrough. Implement `search`, `discover`, `facetCounts`, `resolve`, `details`, `volumes` and `sourceDetails` as required by the existing owners. Keep normalization, ranking definitions and stable tie-breakers explicit. Backend implementations may use different SQL or indexes, but must produce equivalent results on shared golden fixtures.

`ContentObjectStore` supports get/put of bounded bytes by immutable key, integrity metadata, existence checks and publication enumeration for the operator. The API never gives clients unrestricted bucket listing. Local fixtures, bounded database JSON rows, Static Assets and R2 can implement the same read contract; their deployment and write paths differ. Supabase Storage can map the same keys without exposing its internal URLs as permanent entity IDs. A SQL-backed document adapter avoids requiring R2 or one Static Asset per title: qualify its row-size/storage/write cost, serve only the requested document, and keep art at external URLs or in the small owned Static Assets collection. For the selected D1 projection, measure document storage and D1 write costs before choosing this adapter.

Portable export format: UTF-8 JSON Lines for typed records and relationships, JSON documents for requested content units, plus manifest schema version, source hashes, counts, UTF-8 sizes, object hashes and reference constraints. Exports are operator artifacts, not client downloads. Preserve nulls, numeric types, Unicode, source namespaces and identity fields explicitly; do not use a SQL dump as the only recovery format. Generate target-specific DDL/indexes/import batches from this export. SQL-engine changes require query parity tests, not a configuration-only promise.

Migration between combinations follows the same runbook: freeze one source revision; export; load candidate backend; validate counts/references/hashes; replay semantic fixtures; run bounded shadow reads; switch server routing for a canary; activate after parity and budget checks; retain the old compatible revision for rollback. Client API/version need not change if semantics remain the same. Never make two backends competing authorities for user progress.

## Combination comparison and selection gates

Official allowances below were checked on 2026-10-14; they are plan inputs, not confirmation of this account's subscriptions or performance. See the linked sources and independent-review ledger. Free limits can change and must be checked again before activation.

| Combination | Strongest reason to consider it | Decisive qualification issue |
| --- | --- | --- |
| Workers + D1 + R2 | One provider, SQL queries plus independently published documents | D1 import/update writes and query scans; R2 has billable overages |
| Workers + D1 + Static Assets | No R2 registration; asset hosting has a free path | Same D1 bootstrap problem; per-version file cap and content deployment coupling |
| Workers + Turso, R2 optional | SQLite-family queries; larger monthly write allowance; bounded JSON in the DB with URL/Static Assets artwork | Verify import/accounting, hard-stop settings, HTTP transfer terms and actual remote-query cost |
| Workers + Neon + R2 | PostgreSQL queries and SQL ingestion without D1's daily write model | Free compute/transfer budgets, cold starts and SQLite-to-Postgres semantic conversion |
| Supabase Free DB + Storage + Functions | Integrated Postgres, storage and APIs | Database/egress/function allowances, inactive-project pause and dialect migration |
| Workers + R2 precomputed indexes | Avoids live SQL writes and serves immutable content well | Arbitrary AND facets, scoped ranking and search may exceed Worker CPU/object-read budgets |

D1's free limits include 5 million rows read/day, 100,000 rows written/day and 5 GB total storage; each free database is capped at 500 MB. Index maintenance counts toward writes. D1 also limits LIKE/GLOB patterns to 50 bytes: longer or multibyte title queries need a parity-preserving implementation, not silent truncation. Workers Free permits 100,000 requests/day and 10 ms CPU per invocation. Cache hits inside an authenticated Worker still consume Worker requests. Sources: [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/), [D1 limits](https://developers.cloudflare.com/d1/platform/limits/), [Workers limits](https://developers.cloudflare.com/workers/platform/limits/).

Workers Static Assets has a 20,000-file free per-version limit and 25 MiB per-file limit. The proposed 125,429 root documents already exceed the file count if each is a separate asset. Packing, supported sharding or a different object store requires explicit design; a giant bundle returned to clients is prohibited. An authenticated `run_worker_first` path remains subject to Worker request limits. Sources: [Workers limits](https://developers.cloudflare.com/workers/platform/limits/), [Static Assets billing](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/).

R2 Standard includes 10 GB-month storage, 1 million Class A and 10 million Class B operations monthly, with no egress charge. Usage beyond allowances is billable; this is not an unlimited free service or an automatic spending cap. Budget alerts do not stop usage. Sources: [R2 pricing](https://developers.cloudflare.com/r2/pricing/), [budget alerts](https://developers.cloudflare.com/billing/manage/budget-alerts/).

Neon currently lists 1 GB storage/project, 100 CU-hours/project and 5 GB included public transfer on Free; check all applicable account/project limits and scale-to-zero behavior. Supabase Free lists 500 MB database, 1 GB object storage, 5 GB egress plus 5 GB cached egress and 500,000 Edge Function invocations/month, and pauses inactive projects after a week. Turso currently lists 5 GB storage, 500 million monthly rows read and 10 million monthly rows written on Free, without a credit card. These figures are not interchangeable measures of request capacity. Sources: [Neon pricing](https://neon.com/pricing), [Supabase pricing](https://supabase.com/pricing), [Turso pricing](https://turso.tech/pricing).

Neon's 100 CU-hours supports 400 active hours at 0.25 CU; an always-awake 30-day month needs 180 CU-hours even at that size. Caching must therefore reduce both transfer and compute uptime. Supabase's cached Storage allowance is not extra uncached API egress. These make both lower-priority alternatives for this continuously available catalogue. [Neon plan details](https://neon.com/docs/introduction/plans), [Supabase pricing](https://supabase.com/pricing).

Turso's Free quota exhaustion can block databases, including reads when the write/storage allowance is exhausted. Its 3 GB embedded-sync allowance is not the ordinary HTTP query-result meter; clients will not use embedded sync. This remains historical alternative-backend evidence; Turso is not the selected implementation target. HTTP egress entitlement and live quota enforcement remain qualification questions. The [supplemental Sol review](2026-10-14-turso-free-stack-review.md) records sources, platform-transition considerations and unverified points; its import-budget fit is an estimate, not measured speed.

Reject any candidate that needs paid service to satisfy the measured source, silently removes existing filtering, requires full client sync, cannot give bounded selective responses, or needs privileged client secrets. Prefer the candidate that passes parity with the least ongoing operation, fits a measured free envelope and can bootstrap in an acceptable window. Production acceptance of the selected D1 path must include recorded import measurements, cold/warm query results and free-plan settings. An independent recommendation alone is not a benchmark.

## Data ownership across the product

| Surface | Hosted content | Existing local responsibility |
| --- | --- | --- |
| Tankoban | MAL/Comics discovery, series, editions, volume metadata and permitted source references | Readers, files, library/progress, canonical identity and selected provider behavior |
| Biblio | Bounded book discovery, facets, details and provenance | Extend `BiblioCatalog`/`BiblioCatalogStore` caching; reader and library remain local |
| Theatre | IMDb/MAL anime discovery and title facts | Playback, history, library and provider choice |
| Universes | Universe directory, curated order, sections, work references, text and artwork descriptors | Standard structured Universe renderer, navigation and personal progress |
| Vault | Selected-title enrichment and artwork lookup | File scanning, paths, inventory, identity, organization and private local state |
| Tributes | Franchise-specific text, eras/arcs, portals, pinned works and supported scene parameters | Installed One Piece/DCAU/Star Wars/Cosmere renderers and interactions |

Vault is not uploaded as a remote catalogue. No private file path or complete library inventory is needed to fetch metadata for a selected known title. Provider IDs such as MBID/QID/MAL/IMDb remain qualified aliases; they never silently replace `ct1:` or merge ambiguous records.

## Catalogue projection and bootstrap

Start with the same frozen four-source snapshot and deterministic export. Retain searchable/sortable scalar fields, required memberships and aggregate summaries in the query store. Move large per-entity details, mirror groups, editorial scene sections and artwork descriptors to object documents only where queries do not require scanning those documents.

The source has 135,429 root rows before unifying Tankoban attachments. All 10,000 Tankoban IDs match MAL manga IDs, so the object layout can attach Tankoban metadata to manga and produce 125,429 root documents while retaining source provenance. This is evidence for one join, not permission to merge unrelated comics identities.

MAL `tag` and flattened `classification` memberships matched in both SQL difference directions for this snapshot. A single axis-bearing 127,417-row relationship projection can potentially serve both APIs. Validate multiplicity, normalization and future-source changes before deleting either representation from the projected contract.

| Bootstrap design | Current-model estimated writes | Import windows at 60,000/day |
| --- | ---: | ---: |
| Existing full 24-table D1 copy | 3,177,359 | 53 |
| Existing-weight root tables only | 772,257 | 13 |
| Roots + necessary memberships, deduplicated MAL representation | 1,403,233 | 24 |
| Illustrative new roots, no through three secondary-index charges | 177,769–584,056 | 3–10 |

The last row excludes full facet needs and is a sensitivity model, not the chosen schema or measured billing. FTS/index costs, control writes and retries must be measured. A no-index root table may be cheap to import and too expensive to query. Other backends require their own accounting model; D1's 53-day estimate does not transfer to Turso/Postgres.

Use one compact query result with card summaries for a search page; do not fetch one object per result. Fetch rich detail when opened. The measured largest mirror array exceeds 466,000 JSON characters before surrounding details; split by stable post/edition/entity boundaries. Target about 128 KiB UTF-8 per JSON part and enforce an encoded-byte limit. Keep the normal list-page response much smaller. A proposed maximum of 50 cards or 20 requested IDs is an initial bound to verify against bytes and CPU, not a right to return unbounded nested data.

API readiness is per operation and source revision. A populated `series` table does not make Comics discovery or coverage ready. Activation requires all dependency relationships, ranking summaries and object references for that capability. Preserve null behavior, native IMDb prefix LIKE matching (ImdbCatalog.cpp:233-238 binds norm plus %), Comics escaped all-word substring matching, explicit-content rules, language/type exclusions, AND facets, scoped Bayesian means, native Comics ranking, stable ties and Tankoban count-based volume synthesis. Test Unicode case/collation differences, literal LIKE wildcard escaping and null ordering explicitly across SQLite/libSQL/Postgres. Where existing SQL leaves ties unspecified, record the accepted deterministic tie rule before using cursor pagination; do not claim exact parity for an undefined order. Use the native harness contracts listed in the release plan as the baseline.

No source DB is overwritten. Export outputs stay outside original data directories. Operator-side COPY or binary SQLite import may be qualified on a compatible backend using a frozen copy; the prohibition concerns bulk delivery to app users, not trusted server migration. Such imports have no presumed quota exemption. Changes after the initial snapshot are keyed upserts/tombstones with new revision metadata; avoid full indexed reimport for each content edit. Estimate two retained compatible revisions, indexes, temporary import space and backups when checking storage capacity.

### Disposition of all 24 source tables

Counts come from the committed source schema/manifest. Placement below is the proposed logical mapping, not a completed migration. `Query` means queryable fields/relationships or a proved equivalent; `detail` means bounded selectively requested documents; `build` means publisher inputs retained for reproducibility. No row family is discarded merely because it leaves live SQL.

| Source table | Rows | Required disposition |
| --- | ---: | --- |
| `imdb.title` | 61,585 | Query/search/sort projection plus title detail |
| `imdb.genre` | 143,615 | Query membership, inclusion/exclusion and facet behavior |
| `imdb.meta` | 2 | Source revision/build provenance |
| `mal.anime` | 20,000 | Medium-qualified query projection and Jikan-shaped detail |
| `mal.manga` | 20,000 | Query projection/detail; Tankoban attachment target |
| `mal.tag` | 127,417 | Query membership; derive only after classification parity proof |
| `mal.classification` | 127,417 | Axis-bearing query membership and AND facets |
| `mal.tag_count` | 152 | Baseline count input; scoped counts still computed for actual filters |
| `mal.classification_count` | 152 | Facet metadata/count input; preserve normalization and scope |
| `mal.meta` | 2 | Source revision/build provenance |
| `comics.series` | 22,710 | GCD-qualified query projection and series detail |
| `comics.series_stats` | 22,710 | Search/rank/availability summaries, verified against source |
| `comics.download` | 57,432 | Bounded per-series post details plus queried availability summaries |
| `comics.download_mirror` | 398,427 | Bounded per-post source references with order/host/label preserved |
| `comics.canonical_issue` | 55,598 | Build/reference input to coverage and issue reconciliation; preserve qualified IDs |
| `comics.curated_series` | 1,134 | Separate curated identity/query projection and detail |
| `comics.curated_edition` | 9,655 | Query format/year/availability summaries and edition detail |
| `comics.curated_genre` | 1,821 | Query memberships and scoped series counts |
| `comics.curated_source` | 12,895 | Source/availability summaries plus bounded edition-source details |
| `comics.curated_source_claim` | 10,484 | Preserve claim provenance and reconciliation/detail evidence |
| `comics.curated_source_issue` | 156 | Preserve source-to-issue mappings and related evidence |
| `comics.curated_series_coverage` | 1,134 | Query/detail coverage summaries with reproducible build inputs |
| `tankoban.series` | 10,000 | MAL-qualified count/provenance attachment, not a new identity |
| `tankoban.volumes` | 28,839 | Sparse detail overlays preserving count synthesis and numeric ordering |

The four files do not contain all Biblio/provider content or the Universe/Tribute declarations. Their extraction is a separate stage using the existing owners listed above. Optional future music enrichment has its own source-qualified cache and is not part of this 24-table completeness claim.

## Content extraction and independent updates

Extract declarative content from `Universes.js`, the three current Universe JSON payloads, One Piece arc/atlas data, DCAU data, Star Wars era data and Cosmere assembly/configuration. Some declarations mix content with calculations or animation parameters; extract only supported values and keep behavior in installed C++/QML/JS. Preserve current extension IDs, aliases, work IDs, section order and progress references. Route by stable IDs rather than display names so a title rename does not detach progress.

Suggested document families:

```text
content/<revision>/universes/<universe-id>/header.json
content/<revision>/universes/<universe-id>/sections/<section-id>.json
content/<revision>/tributes/<tribute-id>/sections/<section-id>.json
content/<revision>/titles/<source>/<medium>/<source-id>/<part>.json
assets/<sha256>/<variant>.<extension>
```

This is a logical key layout. Content-addressed objects may be reused across revisions through the publication manifest to avoid duplicating unchanged bytes. Do not send the full server object inventory to the client. A small app manifest advertises compatible catalogue/Universe/Tribute revisions and capabilities; each response references only its requested follow-up units.

Each document carries schema version, stable ID, revision, source/provenance and supported renderer/capability requirements. An asset descriptor carries HTTPS URL or service path, media type, optional byte size/hash, width/height, variant and attribution/rights reference. A hash is mandatory for owned published objects; external mutable URLs may lack a reliable hash and must be marked accordingly. Do not persist expiring media URLs as permanent catalogue facts.

Optimize owned artwork locally into appropriate card/banner/detail sizes and supported formats. Avoid making every card load the observed 8.8 MB original PNG. Keep rights/provenance separate from metadata availability; a Wikidata fact does not grant rights to linked artwork. Public owned art may use a direct static/CDN URL if intended to be public. Protected JSON uses the authenticated API. No paid image transformation service is assumed.

Example: changing a One Piece arc description or artwork updates one validated content revision. On the next manifest revalidation, the app requests that section and retains all local progress. Adding a new interactive map renderer requires an app release first; unsupported content stays on the last compatible revision.

## API and client behavior

The proposed service routes are semantic contracts; current raw-table staging routes do not implement them yet:

- `/v1/content-manifest`: small compatible revision/capability index, with ETag.
- `/v1/catalogues/{medium}/search`, `/discover`, `/facets`: bounded results with validated filters and deterministic ordering.
- `/v1/titles/{qualified-id}` and bounded batch lookup: requested detail and source facts.
- Per-series volume, edition and source-detail routes: paged where needed, preserving existing behavior.
- `/v1/universes`, `/v1/universes/{id}`, `/sections/{section-id}` and equivalent Tribute routes.
- Internal/operator-only publish, validation, activation and rollback routes; never exposed by a read lease.

Disable or restrict the current raw-table pagination routes to operator diagnostics before production. The application read lease must expose only the semantic contract; raw pagination is not a back door for app-side database reconstruction.

Responses distinguish success with no matches from source-not-ready, unavailable, auth-expired and unsupported-schema states. Include revision/freshness/provenance; opaque cursors bind normalized query and revision. If a revision expires mid-pagination, return a defined restart-required response and let the client refresh without clearing existing visible results.

Extend the four native catalogue owners with a shared asynchronous transport helper. Existing synchronous accessors may return valid local/cached results immediately, but must not block on HTTP or spin nested event loops. Add request IDs, completion/error signals and generation checks; discard late responses after navigation/filter changes. Preserve row shapes used by existing QML views. Coalesce duplicate work and cancel obsolete requests.

Extend the existing Biblio snapshot/cache owner for bounded response units rather than adding a second book store. Inspect and reuse suitable existing cache infrastructure for other owners; new storage needs an explicit gap, not a new authority. Use byte/age eviction, atomic writes and last-good values. Cache requested pages/sections/details only. Existing valid local catalogue files can remain read-only caches; no migration downloads them or silently reconstructs them.

Unknown required schema/capabilities, failed integrity checks or malformed content cannot replace last-good data. A removed record gets a tombstone/alias as appropriate, preserving local history. On network failure, show cached stale data or honest unavailable enrichment; do not invent successful empty lists.

## Access and caching

Implement app-scoped guest issuance independently of Neon and Colosseum login for the no-account edition; reuse suitable existing service ownership without making the account database a guest-access dependency. Integrate optional account exchange with the existing account-service boundary for the later edition. Its current opaque tokens are authenticated through hashed server-side sessions, not by pretending they are JWTs. Add a bounded exchange/guest issuer for short-lived content-read leases with audience, expiry and minimal scope. Validate leases at the edge without a central account DB lookup for every content request. Admin and publisher credentials remain operator-only. Rotation and expiry behavior require live tests.

“Only Colosseum apps” is an initial access policy, not a guarantee that an anonymous native binary cannot be imitated. A bundled API key, CORS header or User-Agent does not establish trusted app identity. Use guest issuance limits, request/query bounds and abuse controls; stronger attestation is a separate platform decision. Do not force an external account to preserve the local-first product contract.

Authenticate before reading any protected cached response. Shared non-personal cache keys include content revision, normalized query, medium, locale, explicit policy and relevant capabilities; they do not use bearer tokens as public cache keys. Never mix private account/library results into the shared catalogue cache. Do not publicly cache lease responses or authentication failures.

Immutable objects receive long cache lifetimes; active manifests receive short revalidation lifetimes and ETags. The app uses conditional requests and coalesces refreshes. Content publication changes revision keys, so a cache purge is not the only correctness mechanism. Private R2 access uses Worker bindings and bounded requests, not public bucket listing. `r2.dev` is a development endpoint, not the intended production CDN. See [R2 caching](https://developers.cloudflare.com/r2/examples/cache-api/) and [public bucket behavior](https://developers.cloudflare.com/r2/buckets/public-buckets/).

## MusicBrainz, Wikidata and future Vinyl

Do not host a MusicBrainz mirror. Keep a provider-qualified enrichment cache keyed primarily by known MBID and request type, with fetched/expiry times, field provenance and confidence. Provider results enrich Colosseum facts without replacing user edits or canonical identity. Search-based matches require ambiguity handling; titles alone are not sufficient to merge records.

Fetch enrichment through one controlled publisher/queue path with deduplication, retry/backoff and a global provider rate limit. Do not run a live upstream request for every app card or cache miss. Initial operation can use a single serialized publisher; moving to distributed workers requires a genuinely shared limiter, not one timer per isolate. UI uses cached facts and remains usable when enrichment is absent.

MusicBrainz requires a meaningful User-Agent and no more than one request/second per application; its public API is free for non-commercial use, with commercial eligibility handled separately. Confirm Colosseum's applicable eligibility before enabling routine production fetching; no paid fallback is authorized. [MusicBrainz API](https://musicbrainz.org/doc/MusicBrainz_API).

Use Wikidata primarily for known QID/entity lookup and selected cross-media facts. Avoid making public SPARQL a runtime dependency for every search/card. Preserve CC0 fact attribution/provenance where useful and evaluate linked media separately; respect endpoint-specific limits and backoff. [Wikidata data access](https://www.wikidata.org/wiki/Wikidata:Data_access).

Vinyl's catalogue/resolver work remains in the referenced Vinyl audit conversation. This plan provides a future medium/provider extension boundary, not a new YouTube implementation or repeated feasibility search. The 1.1.9 release includes neither Vinyl nor Feria. MusicBrainz cache infrastructure is not permission to add their product UI to that release.

## Free operating envelope

Planning example, not measured traffic: 30 dynamic API requests per daily active user and 20 KB compressed per response, over a 30-day month. Artwork, retries, issuer calls and other account services are additional.

| DAU | Dynamic requests/day | Response bytes/month | DB rows/day at 100 rows per uncached query | Rows/day at 90% query-cache hits |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 3,000 | 1.8 GB | 300,000 | 30,000 |
| 1,000 | 30,000 | 18 GB | 3,000,000 | 300,000 |
| 5,000 | 150,000 | 90 GB | 15,000,000 | 1,500,000 |

At 5,000 DAU this request shape exceeds Workers Free even with perfect edge query caching. A target of 80,000 daily requests leaves about 16 calls/user at that scale before accounting for other services. Client caching, fewer round trips and workload limits matter; do not promise unlimited free growth. Database row reads depend on scanned rows and indexes, not merely returned cards. Remote Postgres/Turso transfer includes cache-fill traffic; do not assume a 90% hit rate without measurement.

For R2, free-only operation requires more than an alert. Verify the account stays on Workers Free, keep protected buckets private, bound each dynamic read to at most two R2 Class B operations and exclude unbounded list/fan-out routes. At the account's 100,000-request/day limit, 31 days times two operations is 6.2 million Class B operations; reserve headroom for all other readers and operator work. Search cards come from the SQL projection, not twenty object reads. HEAD, GET, retries, manifest/auth/cache misses and internal subrequests must be included in the bound. Scheduled and operator work are separate budgeted paths; tests must prove no trigger or direct credential bypasses the accounting. Returning two objects is not proof of two billed operations. This is a proposed enforceable architecture constraint that needs tests, not a deployed control.

Before publishing, enforce total owned-object storage below an internal threshold such as 8 GB and monthly Class A operations below 800,000, including retries, multipart work, retained revisions and other account use. Count actual object operations in a durable publisher ledger and reconcile with provider usage. Backups and revision retention consume storage too. Never allow clients arbitrary uploads. No per-read D1 counter is added merely for metering: that would itself consume the daily write budget.

These controls reduce bill risk but R2 has no provider-enforced zero-dollar guarantee established here. Other credentials, public access, a paid-plan change or an unbounded code path can invalidate the envelope. If the user requires a hard provider stop under all traffic, qualify a hard-capped object alternative or retain external URLs/Static Assets for the initial scope. Do not silently interpret a budget alert as a cap.

Measure cold/warm p50/p95 latency, Worker CPU, row-read amplification, object operations, compressed bytes, cache hit rate and failure behavior from India and at least one other region. Decide index/read-replication placement from evidence, not the assumption that an edge Worker makes every database query local. Metrics must avoid a D1 write for each read request; use available aggregate telemetry and bounded operator samples.

## Publication and rollback runbook

1. Pin source files and content edits to a source revision. Read originals only; record hashes and extraction version.
2. Validate schema, stable IDs, references, asset descriptors, ordering and supported renderer capabilities. Reject dangling section/work references and unexpected executable fields.
3. Export portable records/relationships and bounded immutable objects. Measure counts, bytes, worst objects and projected free-budget cost before upload.
4. Publish a candidate query revision and referenced objects with idempotency keys/checksums. Resume verified chunks; retry within the budget. A network acknowledgement alone is not completeness proof.
5. Verify counts, reference closure and hashes; execute parity probes and selected full logical journeys against the candidate. Mark individual capabilities ready only when dependencies pass.
6. Activate one small revision pointer with compare-and-swap/transaction semantics in the selected control store. SQL and object uploads are not a cross-store transaction: all parts are prepared before this single publication commit.
7. Canary compatible clients, monitor budgets/errors, then widen. Clients revalidate the manifest and fetch requested changed units; no content pack is installed.
8. Roll back the active pointer to the previous compatible revision if needed. Retain referenced objects/cache keys until in-flight cursors and compatibility windows expire. Clean unreferenced objects only after verified retention rules.

A backend outage disables remote enrichment/queries and preserves local core use, valid response caches and progress. A rollback never restores GitHub REST catalogue downloads. Authentication errors trigger bounded renewal; 429 honors Retry-After; not-ready is distinct from an empty catalogue; upstream enrichment failure leaves existing facts intact. External asset failures use an existing variant/placeholder and refresh metadata when appropriate.

Keep local source backups and periodic operator exports of curated content/enrichment/provenance. Verify restoration into a disposable local environment. Git CLI stores code/configuration and small schema fixtures, not credentials or full local DBs. No GitHub REST operation is part of publication, update discovery or recovery.

## Execution stages and acceptance

| Stage | Work and deliverable | Acceptance / dependency |
| --- | --- | --- |
| A: while R2 is unregistered | Record the D1 decision; freeze portable API/export/schema and source inventory | Same feature contract for every candidate; pricing/account unknowns explicit |
| B: local preparation | Deterministic exporter, Universe/Tribute extraction, bounded objects, query projections and fixture corpus | Hash/count/reference checks; every current native query family represented; no source mutations |
| C: free-backend proof | Import a representative slice, measure accounting and run parity/load probes on the reduced D1 projection | Free plan verified; no paid resources; extrapolated full bootstrap and update costs acceptable |
| D: service implementation | Semantic routes, Neon-independent guest issuance, cache policy, publisher activation/rollback | Auth, query bounds, mixed-version rejection, provider failures and rollback tested |
| E: R2 activation if selected | After user registration, verify billing/allowances; private bucket/binding; publish canary objects | Hash/integrity, auth-before-cache, operation bounds and retention verified; no public dev endpoint |
| F: complete publication | Load required projection/data, objects and compatible capabilities under measured budgets | Required source coverage and parity complete; source readiness differs from HTTP health |
| G: native integration | Existing C++ owners become async/cache-aware; supported Universe/Tribute content resolves through API | Clean profile, upgraded cache, offline, cancellation, expiry and malformed content journeys pass |
| H: scoped 1.1.9 receiving | Integrate only approved changes; package production config; remove prohibited catalogue transport | Final diff excludes Feria/Vinyl; installed network proof and existing release gates pass |

Stages B and native contract preparation can proceed before R2 signup; stage E cannot. If a different backend wins, replace E with its object-store qualification, preserving the same exports/routes/tests. No registration is needed to finish this plan or perform local extraction/projection work. Account setup for a new alternative is a later concrete decision, not an action hidden in this planning task.

For 1.1.9, preserve the reviewed candidate and work in isolated slices. Remove both automatic and callable catalogue GitHub REST/bulk-download paths; change packaging's first-launch assumptions. A timer-only disable is insufficient. Preserve existing DB readers as optional old-cache support, and test a fresh installation with no development `data/` directory. The content service must cover the already approved features before API-only cutover; a partial catalogue cannot be labeled ready to meet a date. Full native integration details and harness anchors remain in the linked 1.1.9 rollout note.

Release gates: semantic/native parity; production guest lease issuance and renewal; full required publication; measured free envelope; clean installed first launch and cache upgrade; offline core use; no catalogue GitHub REST/DB transfers; supported independent Universe content update and rollback; final scoped receiving review. Existing installer/signature/update gates still apply. Source review, tests, builds, runtime proof, commit/push, deployment and release approval remain separate states.

## Independent reviews and outstanding decisions

The exact dispatch, all six received reports, hashes and receiving corrections live in [the comparison review ledger](2026-10-14-content-stack-review-ledger.md). A sent prompt is not an accepted review. The provisional Turso recommendation is retained as review history and superseded by Hemanth's D1 decision; D1 production acceptance still requires measured import, query, auth and free-plan evidence.

Open decisions are narrow: whether the selected reduced D1 projection passes parity/bootstrap/zero-cost gates; whether owned-object volume and publication cadence justify R2; how Neon-independent guest issuance meets its free-capacity and abuse-control gates; the measured projection/index design; and MusicBrainz production-use eligibility. None requires changing the app's canonical state ownership or installing content packs.

Completion of this planning task means a reviewable design, measured source inventory, independent comparison evidence/status and executable acceptance gates. It does not mean R2 is enabled, all data migrated, native clients updated, or 1.1.9 qualified.
