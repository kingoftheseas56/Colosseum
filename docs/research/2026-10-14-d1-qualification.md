# D1 qualification receipt — 2026-10-14

Status: **Workers Free is owner-confirmed; disposable hosted D1 SQL and actual temporary Worker binding measurements are complete; production guest/native activation remains unqualified.** Existing staging content was preserved. No signup, R2 registration, GitHub REST or production mutation occurred.

## Access and account gates

The connected Cloudflare API can list the target account and D1 resources and read Workers account settings. The configured catalogue and metadata database IDs match the target account's list. `default_usage_model: standard` is a usage model, not Free-plan evidence. Individual subscription and billing-usage GET requests return authentication error 10000. Local Wrangler 4.150.0 `whoami` reports an expired token that cannot refresh in its noninteractive environment; no login or credential extraction was attempted.

The browser inventory contained no existing Cloudflare tab. Opening the account Workers plans page created a dashboard tab but timed out; binding it subsequently failed `Debugger unattached`. No dashboard plan or usage values were observed. The owner subsequently supplied current Free / $0 plan dashboard evidence; independent billing API verification remains unavailable.

D1 list responses report `num_tables:0` and `file_size:8192`. **These are not proof that staging is empty.** The API schema explicitly marks `num_tables` deprecated and inaccurate. Existing README evidence records 24,208 source rows across ten ready tables; current readiness must be reconciled with read-only `_source_tables` and actual counts after the usage gate. The importer must never target these existing staging databases.

Required evidence before hosted qualification: target account Workers Free plan; current UTC-day D1 rows read/written across the account; current storage and database slots; Workers request allowance for the test window. If account is Paid, stop rather than infer that a small test is free. Do not upgrade or enable paid resources. Record UTC review time and reserve headroom before any disposable import or measurement run.

## Current published platform envelope

Official [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/) and [limits](https://developers.cloudflare.com/d1/platform/limits/) checked live on 2026-10-14 specify Free: 5 million rows read/day, 100,000 written/day, 5 GB account storage, 500 MB/database, ten databases/account and 50 queries/Worker invocation. Free reads/writes reset at 00:00 UTC and quota exhaustion returns errors. The actual account plan is owner-confirmed Free, independently of these published limits.

`LIKE`/`GLOB` patterns are limited to 50 UTF-8 bytes, bound parameters to 100/query, and row/string/BLOB size to 2 MB. The fixture's 49/50/51-byte ASCII and multibyte cases therefore remain mandatory compatibility checks; remote rejection of 51 bytes cannot count as an empty successful result. No truncation workaround is accepted.

The benchmark's **less-than-10,000 estimated-write ceiling is deliberately a per-run safety cap**, not the platform's daily quota. Preserve it.

Workers [pricing](https://developers.cloudflare.com/workers/platform/pricing/) and [limits](https://developers.cloudflare.com/workers/platform/limits/) separately list Free 100,000 requests/day and 10 ms CPU/invocation. The [Rate Limiting binding reference](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/) documents namespace/limit/period configuration, local counters and eventual consistency, but these three primary pages do not explicitly establish this binding's Workers Free eligibility or separate charge status. Its examples labelled free/paid describe application customers, not Cloudflare plans. Subsequent binding deployment and actual `limit()` execution established account-level runtime availability on owner-confirmed Free without an upgrade. Independent separate billing/no additional charge verification remains unavailable; threshold enforcement/429 remains unobserved. A per-IP limiter also shares allowance among users behind NAT and is not an accurate global quota-accounting system; deployment needs a unique namespace and the recorded account/runtime evidence.

## Read-only capacity measurement

Inspected only the four manifest sources through SQLite `mode=ro` with `query_only=ON`. All four SHA-256 hashes matched the frozen source manifest before measurement and remained unchanged afterward. Streamed every row using the benchmark projection's existing column definitions and index weights. Ignored local artifact: `server/content-api/.publisher/benchmark/qualification-capacity.json`.

| Model | Rows | Compact row JSON bytes | Estimated writes including modelled ledger |
| --- | ---: | ---: | ---: |
| Entire 11-table benchmark column projection | 912,735 | 92,507,506 | 2,419,874 |
| Tankoban series (10,000 rows) | 10,000 | 178,647 | 23,130 |
| Tankoban volumes (28,839 rows) | 28,839 | 2,420,695 | 66,698 |
| Tankoban series plus volumes | 38,839 | 2,599,342 | 89,828 |
| Full original 24-table manifest | 1,133,337 | Not measured here | 3,177,359 |

Projection JSON bytes are transport-size measurements, **not D1 database storage**: SQLite pages, indexes, schema and controls are excluded. A separate generated local SQLite copy then materialized all 912,735 rows using the fixture's identical table declarations and indexes: **28,341 pages × 4,096 bytes = 116,084,736 bytes**, matching the closed database file size (about 110.7 MiB). Construction took 20.32 seconds locally. This demonstrates local indexed projection size below the published 500 MB/database limit, not actual D1 storage or account headroom; import controls, hosted layout and future revisions still need allowance. Ignored artifacts: `.publisher/benchmark/qualification-full-projection.sqlite` and `qualification-storage.json`. These full projection columns also omit facts needed for full native catalogue parity. No full hosted population was imported; the subsequently imported disposable fixture contains only 967 rows.

The 24-table model reconciles exactly: 2,823,109 weighted data/index writes + 35,425 modelled 32-row chunks × 10 control units = 3,177,359 writes. At the existing deliberate 60,000/day reserve, that is 53 days. This is a capacity model, not a schedule or measured billing. The 11-table projection's equivalent bound is 41 days at 60,000/day. Tankoban alone models two days at that reserve; all missing volumes exceed the fixture's single-run 10,000 ceiling and require a separately reviewed staged importer. Readiness of existing series does not qualify volumes.

Full local Tankoban population bounds: maximum series count 285 (zero counts above proposed 2,000 cap), maximum baked rows per series 285, maximum volume-number UTF-8 bytes five, and maximum summed number/cover/name bytes per series 22,731. Proposed 2,000-row/512 KiB semantic caps do not exclude the existing population on these raw bounds; final encoded JSON and future revisions still need their own check.

## Performance and integration decision

Previously generated local SQLite report has 21 passing cases over 967 sampled rows; it is local evidence only. Hosted sample SQL meters and temporary Worker route timing are recorded below. Worker CPU, cold-cache/geographic coverage and native loading performance remain unmeasured. On the full local projection, `EXPLAIN QUERY PLAN` confirms Tankoban count searches the MAL-ID index; overlay searches the MAL-ID/number index while using recursive/generated rows and temporary ordering. IMDb substring scans the title table. These are strategies, not billed row counts. Full scan risk matters: one whole 912,735-row projection scan would consume about 18% of a hypothetical unused Free daily read quota; actual per-operation meters must replace this illustrative calculation.

The live service currently provides raw ready-table pagination, catalogue readiness and a staging admin-only read-lease issuer. It does not yet provide guest issuance or semantic Tankoban operations. Raw table paging is not acceptance of native Tankoban/IMDb/MAL/Comics behavior. Parallel implementation may author a disabled guest/semantic operation, but activation still requires receiving review and account/hosted gates.

Recommended first qualification target: a series-by-MAL-ID operation with exact source hash/count reconciliation, indexed lookup and native count/provenance parity; add volumes only when their source table is ready and exact number-string overlay plus native numeric-aware sorting are proven. Keep native local fallback and canonical state ownership. Do not activate full catalogue search/facets/ranking from 21 sample SQL cases or enable Universe/media routes without their separate body/hash/ETag/runtime evidence.

Next, qualify the actual guest/semantic/native path after staging volumes is complete and receiving gates pass. The temporary Worker fixture, compact recovery and separate boundary diagnostics are complete. Preserve ordered results/digests, errors, per-query `rows_read`, `rows_written`, duration, first-observed and repeated p50/p95. Connector execution is management API diagnostics; distinguish its transport overhead from the deployed Worker route. Separately exercise the actual guest → semantic Worker → native path after review, and retain unknown CPU/cold-cache fields as unknown. No Turso comparison is needed for the user's D1 choice.

## Live account, import and compatibility evidence

The account owner supplied current Workers plans dashboard evidence showing Free / $0. This is owner-confirmed, not independently verified through billing APIs. UTC was verified with the clock tool. Account-wide GraphQL with no database-ID filter succeeded: Oct 13 catalogue usage was 57,622 reads / 54,859 writes and metadata three reads / five writes. Oct 14 initially returned no groups, then 24 reads despite subsequent observed work, demonstrating ingestion lag. The later account-wide refresh reconciled staging 10,025 reads / zero writes and fixture 13,127 reads / 2,563 writes. No other database IDs appeared for that day. Reported headroom does not guarantee absence of concurrent traffic or lag.

Read-only staging reconciliation proved ten ready tables / 24,208 imported rows. Tankoban series had the expected manifest hash and 10,000 expected/imported rows; actual count returned 10,000 and metered 10,000 reads. Volumes had zero imported of 28,839, ready=0; actual count was zero. Catalogue control query metered 24 reads; import-budget lookup one read and showed 49,584 reserved writes for Oct 13. Four query plans metered zero reads/writes. Actual catalogue query metadata reported 3,780,608 bytes, contradicting the stale 8,192-byte database-list metadata. MAL-ID series lookup uses its integer primary key; volume lookup uses the MAL-ID index and temporary number ordering.

After receiving acceptance of reported headroom with explicit lag uncertainty, created only the disposable database colosseum-d1-benchmark-20261014-0129, ID `c33a4181-f311-4410-a228-49485712f380`. Its seed digest is `8adb1914b64ee7715c564bba0b53ffd0f2352357ea95b26023e6d79ce48c1fe4`; its actual seed `workloadDigest` is `c25bae85e6acf17dcf5e4a0ae872bc32f3d8a69b2362ef9d340c5018c9abbab9`. The current seed, D1 receipt and deployed Worker HTTP artifact agree on that workload digest.

The older local SQLite report has `queryDigest:76174167d6ddb57885a54a8a53a6dc25e4960ed132b1630598b5052369ad2637`. This is a different hash field/canonical serialization: `run.py` hashes arrays of ID, SQL, compact-JSON argument string and expected digest; `prepare.py` hashes objects containing ID, SQL, argument array and expected digest. Different values do not establish changed SQL or binds. Do not compare these two differently serialized hash fields as if they were identical identity checks.

Actual import metered **2,563 writes, 1,013 reads, 88 management requests**, final size **233,472 bytes**. All eleven table counts matched the 967-row seed. Success/meters and next-query write reservations were checked, with a 9,000-write ceiling and no retries. This connector adaptation uses identical fixture schema/indexes/rows but no publisher control ledger, so its actual writes do not replace the original 2,884 conservative estimate for a different importer.

The original 21-case SQL run checked first observation plus five repeats for seventeen cases, enforcing exact ordered-array parity inside execution. It stopped immediately at ASCII 51-byte LIKE error 7500, LIKE or GLOB pattern too complex / SQLITE_ERROR. SQL metered 9,852 reads / zero writes across 102 successful management requests. Returned connector serialization truncated 25 nested arrays, so full five-repeat distributions cannot be reconstructed from that output. Receipt retained in ignored `.publisher/benchmark/d1-qualification-receipt.json`.

Receiving review authorized explicit compact recovery: seventeen single rechecks plus separately labelled Unicode boundary diagnostics. All seventeen rechecks and Unicode 49/50 matched ordered rows; Unicode 51 failed error 7500. Therefore **19/21 sampled SQL cases pass; ASCII and Unicode 51-byte cases fail**. Recovery added 2,262 metered reads / zero writes. Complete untruncated evidence: `.publisher/benchmark/d1-compact-recovery.json`.

Two separate Tankoban point-repeat diagnostics retained six full compact observations each. Count metered two reads per returned row, repeat SQL p50/p95 0.1933/0.2131 ms. Overlay metered 76 reads for eight rows, repeat SQL p50/p95 0.8266/1.3763 ms. Management repeat p50/p95 was 347/353 ms and 357/383 ms respectively. These are management API diagnostics, not Worker/native performance.

**Budget accounting correction:** the fixture's original 20,000-read guard excluded 10,025 prior staging reads; total lane observations reached 23,152, exceeding the original lane ceiling by 3,152. Receiving review then authorized a revised **50,000 total-read ceiling**, including staging and conservative next-query reservations. The write ceiling remained 9,000. Both rejected patterns lacked individual meta meters, so their read cost is unknown rather than asserted zero. Subsequent GraphQL reconciled observed totals; no automatic replay occurred.

## Actual temporary Worker binding measurements

Receiving review approved a temporary Worker bound only to the disposable fixture, with static SQL/fixed binds for Tankoban count and overlay, a 512 KiB response cap and independent rate-limit namespace. It exposed no arbitrary SQL, admin/session credentials or production guest configuration. Server lane owned deployment; this lane owned exactly **12 HTTPS GETs**, six per case, with the truthful publisher User-Agent. All returned 200 and matched case ID, columns, seed/workload digests, exact ordered rows and canonical result digests. Complete local ignored artifact: `.publisher/benchmark/d1-worker-fixture.json`.

| Sampled operation | First client RTT | Repeat client p50 / p95 | Repeat D1 SQL p50 / p95 | Body bytes | Reads/query |
| --- | ---: | ---: | ---: | ---: | ---: |
| Tankoban count | 981.2 ms | 380.1 / 401.7 ms | 0.2978 / 0.3203 ms | 559 | 2 |
| Tankoban overlay | 365.1 ms | 385.2 / 450.2 ms | 0.7053 / 0.8111 ms | 1,089-1,091 | 76 |

Repeat summaries are nearest-rank over five observations. Edge colo was SIN; D1 colo HKG. Each Python urllib observation includes transport/TLS and Worker overhead. Worker CPU, production compression, native decode/paint and cold-cache state remain unproved. This is actual Worker binding performance for sampled SQL, not the actual guest/semantic/native route or full population. Overlay read amplification was 9.5 reads per result row.

These requests added 468 metered reads / zero writes: final lane **23,620 observed reads / 2,563 writes**, below revised 50,000/9,000 ceilings. The server lane also deployed and actually executed the Rate Limiting binding on owner-confirmed Free without an upgrade. Four probe requests returned 200; runtime availability is established, but 429 threshold enforcement was not observed and independent separate-charge verification remains unavailable.

Decision: sampled Tankoban count and SQL overlay independently pass hosted equality with measured bounded costs. Both 51-byte LIKE patterns fail, so unrestricted catalogue search cannot be admitted unchanged. Existing staging volumes is absent; current native activation remains blocked pending complete source readiness, actual lease/semantic envelopes, numeric-aware sorting reconciliation, guest/semantic Worker measurements and native acceptance. Universe/media and remaining catalogue operations retain their separate gaps.

Cleanup: both lanes confirmed all planned artifacts were saved. Server lane deleted its owned temporary Worker and verified absence. This lane checked the disposable database's exact ID/name, deleted only `c33a4181-f311-4410-a228-49485712f380`, and verified absence via D1 listing; only the two original staging databases remained. Local ignored fixture/source/metadata/measurement artifacts are retained. No existing staging table changed.
