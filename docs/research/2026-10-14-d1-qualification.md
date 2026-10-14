# D1 qualification receipt — 2026-10-14

Status: **account Free plan and current allowance unverified; hosted catalogue performance unmeasured; no operation approved for live activation by this receipt.** Local capacity inspection is complete. No cloud provisioning, import, deletion, deployment, signup, R2 registration, GitHub REST or production mutation was performed in this lane.

## Access and account gates

The connected Cloudflare API can list the target account and D1 resources and read Workers account settings. The configured catalogue and metadata database IDs match the target account's list. `default_usage_model: standard` is a usage model, not Free-plan evidence. Individual subscription and billing-usage GET requests return authentication error 10000. Local Wrangler 4.150.0 `whoami` reports an expired token that cannot refresh in its noninteractive environment; no login or credential extraction was attempted.

The browser inventory contained no existing Cloudflare tab. Opening the account Workers plans page created a dashboard tab but timed out; binding it subsequently failed `Debugger unattached`. No dashboard plan or usage values were observed. The account owner's plan/usage answer remains pending.

D1 list responses report `num_tables:0` and `file_size:8192`. **These are not proof that staging is empty.** The API schema explicitly marks `num_tables` deprecated and inaccurate. Existing README evidence records 24,208 source rows across ten ready tables; current readiness must be reconciled with read-only `_source_tables` and actual counts after the usage gate. The importer must never target these existing staging databases.

Required evidence before hosted qualification: target account Workers Free plan; current UTC-day D1 rows read/written across the account; current storage and database slots; Workers request allowance for the test window. If account is Paid, stop rather than infer that a small test is free. Do not upgrade or enable paid resources. Record UTC review time and reserve headroom before any disposable import or measurement run.

## Current published platform envelope

Official [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/) and [limits](https://developers.cloudflare.com/d1/platform/limits/) checked live on 2026-10-14 specify Free: 5 million rows read/day, 100,000 written/day, 5 GB account storage, 500 MB/database, ten databases/account and 50 queries/Worker invocation. Free reads/writes reset at 00:00 UTC and quota exhaustion returns errors. Account plan remains a separate unverified fact.

`LIKE`/`GLOB` patterns are limited to 50 UTF-8 bytes, bound parameters to 100/query, and row/string/BLOB size to 2 MB. The fixture's 49/50/51-byte ASCII and multibyte cases therefore remain mandatory compatibility checks; remote rejection of 51 bytes cannot count as an empty successful result. No truncation workaround is accepted.

The benchmark's **less-than-10,000 estimated-write ceiling is deliberately a per-run safety cap**, not the platform's daily quota. Preserve it.

Workers [pricing](https://developers.cloudflare.com/workers/platform/pricing/) and [limits](https://developers.cloudflare.com/workers/platform/limits/) separately list Free 100,000 requests/day and 10 ms CPU/invocation. The [Rate Limiting binding reference](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/) documents namespace/limit/period configuration, local counters and eventual consistency, but these three primary pages do not explicitly establish this binding's Workers Free eligibility or separate charge status. Its examples labelled free/paid describe application customers, not Cloudflare plans. **Eligibility remains unknown, not established as unavailable.** Do not upgrade to resolve uncertainty. A per-IP limiter also shares allowance among users behind NAT and is not an accurate global quota-accounting system; deployment needs a unique namespace and actual account/provider eligibility evidence.

## Read-only capacity measurement

Inspected only the four manifest sources through SQLite `mode=ro` with `query_only=ON`. All four SHA-256 hashes matched the frozen source manifest before measurement and remained unchanged afterward. Streamed every row using the benchmark projection's existing column definitions and index weights. Ignored local artifact: `server/content-api/.publisher/benchmark/qualification-capacity.json`.

| Model | Rows | Compact row JSON bytes | Estimated writes including modelled ledger |
| --- | ---: | ---: | ---: |
| Entire 11-table benchmark column projection | 912,735 | 92,507,506 | 2,419,874 |
| Tankoban series (10,000 rows) | 10,000 | 178,647 | 23,130 |
| Tankoban volumes (28,839 rows) | 28,839 | 2,420,695 | 66,698 |
| Tankoban series plus volumes | 38,839 | 2,599,342 | 89,828 |
| Full original 24-table manifest | 1,133,337 | Not measured here | 3,177,359 |

Projection JSON bytes are transport-size measurements, **not D1 database storage**: SQLite pages, indexes, schema and controls are excluded. A separate generated local SQLite copy then materialized all 912,735 rows using the fixture's identical table declarations and indexes: **28,341 pages × 4,096 bytes = 116,084,736 bytes**, matching the closed database file size (about 110.7 MiB). Construction took 20.32 seconds locally. This demonstrates local indexed projection size below the published 500 MB/database limit, not actual D1 storage or account headroom; import controls, hosted layout and future revisions still need allowance. Ignored artifacts: `.publisher/benchmark/qualification-full-projection.sqlite` and `qualification-storage.json`. These full projection columns also omit facts needed for full native catalogue parity. No hosted population was imported.

The 24-table model reconciles exactly: 2,823,109 weighted data/index writes + 35,425 modelled 32-row chunks × 10 control units = 3,177,359 writes. At the existing deliberate 60,000/day reserve, that is 53 days. This is a capacity model, not a schedule or measured billing. The 11-table projection's equivalent bound is 41 days at 60,000/day. Tankoban alone models two days at that reserve; all missing volumes exceed the fixture's single-run 10,000 ceiling and require a separately reviewed staged importer. Readiness of existing series does not qualify volumes.

Full local Tankoban population bounds: maximum series count 285 (zero counts above proposed 2,000 cap), maximum baked rows per series 285, maximum volume-number UTF-8 bytes five, and maximum summed number/cover/name bytes per series 22,731. Proposed 2,000-row/512 KiB semantic caps do not exclude the existing population on these raw bounds; final encoded JSON and future revisions still need their own check.

## Performance and integration decision

Previously generated local SQLite report has 21 passing cases over 967 sampled rows; it is local evidence only. No D1 rows-read meters, hosted query durations, Worker CPU, route latency, geographic/cold-cache behavior or native loading performance have been measured in this lane. On the full local projection, `EXPLAIN QUERY PLAN` confirms Tankoban count searches the MAL-ID index; overlay searches the MAL-ID/number index while using recursive/generated rows and temporary ordering. IMDb substring scans the title table. These are strategies, not billed row counts. Full scan risk matters: one whole 912,735-row projection scan would consume about 18% of a hypothetical unused Free daily read quota; actual per-operation meters must replace this illustrative calculation.

The live service currently provides raw ready-table pagination, catalogue readiness and a staging admin-only read-lease issuer. It does not yet provide guest issuance or semantic Tankoban operations. Raw table paging is not acceptance of native Tankoban/IMDb/MAL/Comics behavior. Parallel implementation may author a disabled guest/semantic operation, but activation still requires receiving review and account/hosted gates.

Recommended first qualification target: a series-by-MAL-ID operation with exact source hash/count reconciliation, indexed lookup and native count/provenance parity; add volumes only when their source table is ready and exact number-string overlay plus native numeric-aware sorting are proven. Keep native local fallback and canonical state ownership. Do not activate full catalogue search/facets/ranking from 21 sample SQL cases or enable Universe/media routes without their separate body/hash/ETag/runtime evidence.

Once the account answer arrives, reserve usage, reconcile staging read-only, and measure the same exact sampled SQL on an authorized disposable target (at most 2,884 estimated seed writes plus reviewed controls and bounded query read allowance). Preserve ordered results/digests, errors, per-query `rows_read`, `rows_written`, duration, first-observed and repeated p50/p95. Connector execution is management API diagnostics; distinguish its transport overhead from the deployed Worker route. Separately exercise the actual guest → semantic Worker → native path after review, and retain unknown CPU/cold-cache fields as unknown. No Turso comparison is needed for the user's D1 choice.
