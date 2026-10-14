# Supplemental native review: Turso Free + Cloudflare Workers

Research date: **2026-10-14**. This is an independent Codex Sol review, not a sixth Spawn/Preflight result. The attempted sixth Spawn was blocked because all six tabs were assigned; no Spawn repair or relaunch was performed. Only public official vendor material and existing source-derived catalogue arithmetic were used. No signup, import benchmark, cloud mutation, GitHub REST request or YouTube research occurred.

Parent receiving update: the previously unsent sixth task was subsequently dispatched through the newer Spawn host and its separate report was received. See the [comparison ledger](2026-10-14-content-stack-review-ledger.md); this note remains the independent Sol review and does not claim that later dispatch as its own work.

**Judgment:** Turso Free + Workers should outrank a full-copy D1 Free bootstrap as the next candidate to qualify. Its monthly write pool can plausibly absorb this catalogue in one billing period without D1's 53-day daily-budget wait. It should not replace the verified staging baseline before import accounting, query parity, Worker CPU and hard-stop behavior are qualified. R2 is optional later, not a prerequisite assumed here.

## Verified current allowances and stopping behavior

| Turso Free metric | Published allowance |
| --- | ---: |
| Databases | 100 |
| Storage | 5 GB |
| Rows read | 500 million/month |
| Rows written | 10 million/month |
| Embedded sync | 3 GB/month |
| Point-in-time restore | 1 day |

The plan is $0 and requires no credit card. Free cannot consume paid overages without upgrading. Pricing FAQ says exceeding **any** storage/read/write/sync quota blocks databases, even when other metrics have capacity. Paid overages are an opt-in charging path, outside this free-only candidate. The FAQ content was verified in the official page's HTML because text extraction omitted collapsed answers. [Turso pricing](https://turso.tech/pricing)

The usage guide documents `BLOCKED` query errors and counts scanned rows rather than only returned rows. Aggregates/joins/subqueries can touch many rows; aborted transactions can still incur writes. Its introductory plan names are older than the current pricing page: prefer current pricing for allowances and qualify actual current errors/reset behavior. [Usage and billing](https://docs.turso.tech/help/usage-and-billing)

Unknown without an account/test: enforcement timing, current Free reset boundary for every meter, propagation delay, precise recovery procedures and whether blocking applies identically to every API/database engine. Do not depend on a read-only service surviving exhaustion of the write pool. Application-level reservations, low-water stops and cached/offline fallback remain necessary.

## Catalogue bootstrap: computed fit, not measured import

Existing local evidence: `server/content-api/source-manifest.json`, `src/source-schema.json`, and `docs/research/2026-10-14-catalogue-projection-budget.md`. These contain 24 tables, 1,133,337 rows, 164,626,432 source bytes, and 2,823,109 estimated source/index writes. At 32 rows/chunk, the existing ledger model adds 354,250 units: **3,177,359 estimated writes**.

| Comparison | Computed implication |
| --- | --- |
| Current conservative D1 import cap: 60,000/day | `ceil(3,177,359 / 60,000) = 53` capped days. |
| Turso 10,000,000/month | One equivalent import uses about **31.8%**; modeled remainder **6,822,641**. |
| Two equivalent full imports | 6,354,718 writes, about 63.5%. |
| Three equivalent full imports | 9,532,077 writes, about 95.3%; too little practical reserve. |
| Original bytes / published 5 GB | About 3.3% using decimal GB; hosted storage is not measured by source-file size alone. |

Pricing explicitly counts affected index updates as additional writes. Therefore 1,133,337 source rows cannot be treated as the complete SQL-import charge. The existing weighted model is a planning estimate transferable in spirit, not a confirmed Turso bill; system tables, importer transactions, schema work and retry behavior can change usage. [Index-write accounting FAQ](https://turso.tech/pricing)

Prefer a bounded SQL/HTTP importer through Colosseum's trusted admin API with deterministic chunk IDs, atomic ledger/data writes, manifest hash/count checks and retained checkpoints. The current D1-specific publisher/server must be adapted; changing the database URL alone is insufficient. Capture usage before/after a representative indexed import, including failed transactions, before selecting a full-bootstrap ceiling.

Turso documents binary SQLite import and WAL preparation. Authorized operator-to-backend migration remains a candidate; the user restriction concerns post-release bulk database/content-pack downloads to app users. Qualify binary import billing and query parity using a frozen copy, and perform any WAL conversion only on that copy, never the original files. Neither those docs nor this review establishes a row-write billing exemption. No upload is authorized or performed by this read-only review. The current CLI import page's browser extraction was incomplete; the dated official import article describes the mechanism, not proof of today's billing. [Official import article](https://turso.tech/blog/migrating-and-importing-sqlite-to-turso-just-got-easier), [CLI import](https://docs.turso.tech/cli/db/import)

One billing period of capacity is **not** proof of one-day completion, latency, throughput or successful import. Provider rate/transaction limits, network time, Worker handling and concurrent usage remain unmeasured. Thirty-five thousand small import requests could fit the numerical Worker request allowance in isolation, but that does not establish safe daily service capacity.

## HTTP delivery versus embedded sync

The 3 GB meter is database-page transfer between cloud and local embedded databases in either direction. Ordinary SQL-over-HTTP query results are a different path; do not label the sync pool a generic HTTP egress allowance. [Sync definition](https://turso.tech/pricing)

Official SQL-over-HTTP docs support `/v2/pipeline`, bound parameters and explicit typed values. LibSQL uses `@libsql/client/web`; the newer engine uses `@tursodatabase/serverless`. The `/web` client uses HTTP and avoids native dependencies. [HTTP quickstart](https://docs.turso.tech/sdk/http/quickstart), [HTTP reference](https://docs.turso.tech/sdk/http/reference)

**Verified absence:** the current public price table does not list a separate ordinary HTTP query-result egress meter. **Unknown:** an unrestricted contractual HTTP transfer entitlement, undocumented throttles/fair-use constraints, and engine-specific response/request size limits. Do not turn “no listed egress price” into “unlimited proven egress.” Measure response bytes and check current service terms before qualification; no embedded full-database sync to clients is needed for this architecture.

Native clients call Colosseum's leased HTTP API; Turso credentials remain Worker secrets. The Worker controls query allowlists, pagination, source/version readiness and provider provenance. Existing `ct1:` identity, library/history/progress and provider resolution remain with their native owners. Optional R2 can later hold split versioned entity payloads and precomputed standard shelves without changing that authority.

## Workers Free is still a binding constraint

Cloudflare publishes 100,000 account requests/day, 10 ms CPU/request, 128 MB memory, 50 external subrequests/request and six simultaneous connection establishments. Request allowance resets at midnight UTC; exhaustion returns 1027. Network/database waiting is excluded from CPU, while parsing, serialization, lease validation and local ranking execute in the Worker. CPU exhaustion can produce 1102. [Workers limits, updated October 12](https://developers.cloudflare.com/workers/platform/limits/)

Use a small HTTP driver or direct protocol adapter, bounded SQL results and one or a few batched upstream requests. Do not load the entire Comics catalogue into a Worker to port C++ ranking, or assume ORM/SDK overhead plus large JSON fits 10 ms. Move expensive snapshot aggregation into local preparation or equivalent SQL; preserve scoped ranking semantics. Profile representative and worst-case requests rather than relying on vendor examples.

Workers-to-Turso integration is officially documented, including the `/web` import and secret credentials. This establishes a supported architecture, not Colosseum throughput, geographic latency or runtime proof. An additional vendor/network boundary and its availability must be reflected in retry/error handling. [Cloudflare Turso tutorial](https://developers.cloudflare.com/workers/tutorials/connect-to-turso-using-workers/)

Read budgets also need protection: as a sensitivity example, `500,000,000 / 61,585` permits only about 8,118 complete IMDb-title scans/month before any joins or other traffic. A `LIMIT 25` response does not make an unindexed substring scan cost 25. Precomputed summaries and appropriate indexes remain relevant even with the larger read pool.

## Current platform transition and SQL parity risks

Current official docs distinguish the production libSQL SQLite fork from Turso's ground-up rewrite. Both can now run on Turso Cloud; docs favor libSQL for a battle-tested foundation. Select **libSQL explicitly** for an initial native-SQL migration and pin the matching HTTP driver. Do not treat engine names or default provisioning choices as interchangeable. [libSQL overview](https://docs.turso.tech/libsql), [Current introduction](https://docs.turso.tech/introduction)

The April 8 private-beta announcement promised existing SQLite databases would not migrate automatically without user action. It is historical roadmap evidence; current engine availability comes from today's docs. On October 2, Turso announced acquisition by Supabase and said databases/APIs/workflows continue, with deeper integration to come. This adds transition risk, not evidence that Free is withdrawn or that Supabase limits now apply. [April transition announcement](https://turso.tech/blog/turso-cloud-new-generation-private-beta), [October 2 announcement](https://turso.tech/blog/turso-is-joining-supabase)

Cloud limitations include read-only `user_version`/`application_id`, unsupported `busy_timeout` and managed `journal_mode`. Migration bookkeeping belongs in explicit tables. Compatibility marketing is insufficient for native parity. [Cloud limitations](https://docs.turso.tech/cloud/limitations)

Required corpus: exact normalization and escaped `LIKE` semantics; null/collation/date behavior; medium-qualified MAL IDs and intersected facets; Bayesian means over filtered populations; Comics coverage/edition counts and C++ scoped ranking/ties; deterministic pages; preserved IDs/provenance. Test triggers/constraints, transaction atomicity, replay conflicts and source-version activation on the selected engine. SQL-over-HTTP's typed integer strings must not silently lose precision when adapted to JSON client responses.

## Ranking and receiving gate

**First for next qualification:** Workers + Turso Free/libSQL, with optional R2 only when payload measurements justify it. The full-source bootstrap fits a published monthly pool without a mandatory 53-day wait. **Keep as verified baseline:** existing D1 staging and native SQLite; Turso has no Colosseum import/runtime proof yet.

Before adopting, prove: free/no-overages settings and account-wide reserves; sample/full manifest import counts and observed usage; indexed search/relationship parity; CPU/memory and request budgeting; relevant region latency; hard-stop and cached/offline recovery; secret/lease handling; immutable versions and rollback/export strategy. No signup, deployment, database upload, performance test or original SQLite mutation was performed for this review.
