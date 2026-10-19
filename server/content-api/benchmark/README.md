# Shared catalogue benchmark fixture

This slice prepares identical real-source projection rows, indexes and SQL cases for D1 and Turso/libSQL. It makes no hosted calls. Run from `server/content-api` with Python 3.11+:

```powershell
python benchmark/prepare.py --source-dir "<source checkout>/data"
python -m unittest discover -s benchmark -p test_prepare.py -v
```

Default outputs are ignored `.publisher/benchmark/schema.sql`, `seed.json` and `queries.json`. `--output-dir` changes this directory; keep generated data local/ignored. The four source files open through `mode=ro` attached connections with `query_only=ON`; hashes must match the current manifest before preparation and remain unchanged afterward. Source files must stay frozen during preparation. No source WAL edits or database-file transmission occurs.

The current real-source fixture has **967 rows across 11 projection tables**, **2,884 conservatively estimated writes** including index writes and ten ledger units per 32-row chunk, **36 estimated chunks**, and **54,356 compact UTF-8 row-JSON bytes**. The default ceiling is 9,000 estimated writes and any ceiling at/above 10,000 is rejected. These are preparation estimates, not actual provider metering. The runner must independently reserve existing account/day/month usage and refuse imports without verified Free-plan authority.

Roots are spread deterministically through ordered IMDb/MAL IDs, with extra real Unicode/prefix cases, eight small-download comics series, and twelve Tankoban counts between 2 and 12. All related genres/classifications/downloads/mirrors/volume facts for these sampled IDs are included. Shared numeric IDs across MAL media remain medium-qualified. This is a small semantic/control workload; its latency does not establish full-scale performance.

## Runner contract

`seed.json` has `version:1`, `sourceHashes`, `schemaStatements` (table and index DDL strings), `tables` (name/columns/rows/indexCount/rowCount/selectedKeys/estimatedWrites), and `queries`. Table rows are positional arrays; source mirror rowid is preserved as `source_rowid`. Do not reinterpret array order or source identity. `seedContentDigest` hashes the schema/table payload independent of timing; `workloadDigest` hashes query IDs/SQL/binds/expected-result hashes.

Each query has `id`, `sql`, `args`, `expectedColumns`, ordered `expectedRows` arrays, `expectedDigest`, source/projection query plans and local elapsed milliseconds. The source oracle queries the original databases using CTEs restricted to the same selected roots/relationship closure; it does not compare an unrestricted full population with a sample. Exact array equality is enforced locally before artifacts are written.

Canonical digest: recursively normalize integral finite numbers to integers, retain nulls/string bytes/row order, then SHA-256 over compact UTF-8 JSON (`ensure_ascii=False`, separators `(',',':')`, `allow_nan=False`). Floating nonintegral values retain their numeric value; reject nonfinite values. A JS runner can use `JSON.stringify` on scalar arrays after finite-number validation. Rows must follow the SQL's explicit ordering. Do not hash provider-specific result envelopes or unordered maps.

Plan fields contain `details`, `scanPlanNodes`, `searchPlanNodes`, and `actualRowsScanned:null`. `EXPLAIN QUERY PLAN` identifies planned scan/search operations; it does not measure billed rows or execution counts. Source and local projection times are single observations for diagnostics, not hosted latency or a statistical speed comparison. Provider execution plans/metered rows/CPU/wall time belong in the runner report.

## Coverage and boundaries

| Implemented case | Exact proof scope |
| --- | --- |
| IMDb exact normalized lookup, current native prefix search, requested substring | Ordered SQL fields over the shared sample; substring is an additional requested workload, since live `ImdbCatalog.cpp:233` binds `norm + '%'`. |
| IMDb real Unicode display-title query, null and genre membership | Unicode display facts are real; baked normalized names may be ASCII. This sample's null query can legitimately return zero. |
| MAL manga AND genre/demographic facets and explicit gate | Case-insensitive intersection over the same sample; both facet predicates and root medium are retained. |
| MAL scoped mean and Bayesian ranking | Confidence floor 3000 and mean over the same sampled facets; not the entire source's catalogue mean. |
| MAL anime tag membership | Medium-qualified classification membership; not full native anime-filter parity. |
| Comics all-word search and escaped literal `%`/`_` | Native-inspired order plus deterministic GCD-ID tie-break, explicitly additional where native ties are unspecified. |
| Comics series downloads/mirrors | Complete closure for selected series, with mirror source ordering preserved. |
| Tankoban count/provenance and synthesized-volume overlay | Counts, exact number-string overlays, generated holes and outside-count extras; native numeric-aware final sorting still needs parity proof. |
| ASCII and multibyte LIKE patterns: 49/50/51 UTF-8 bytes | Actual pattern bytes include `%`. Original SQLite expected rows are recorded. Remote pattern rejection is a compatibility failure, never a successful empty result or fast passing query. |

The current generated workload contains **21 cases**. Negative long/special inputs query real tables rather than substituting constant expressions. Any semantic workaround for D1's pattern limit must be implemented and separately proven before claiming this feature compatible. No automatic truncation or skipping is permitted.

Live source anchors: `native/engine/ImdbCatalog.cpp:186,233`; `MalCatalog.cpp:154,199,552,614,639`; `ComicsCatalog.cpp:126,165,187`; `TankobanCatalog.cpp:110,136,172`. The synthetic unit fixtures test nulls, deterministic closure, preserved source hashes, failed budget/hash gates, Bayesian ranking and synthesized holes/out-of-range volume facts; they supplement the real-source run.

Critical remaining coverage: Universe page assets/media delivery is not exercised by SQL fixtures. Entire Tankoban excluding chapter mode still requires manga/detail/edition/discovery adapters, every catalogue/facet/date/ranking case, asset routing, caches/offline behavior and native GUI receiving tests. Curated Comics C++ ranking/coverage/source-claim identity, complex numeric-aware volume sorting, provider enrichment, full `ct1:` reconciliation and full-population performance remain unproved. Native owners keep canonical identity/library/history/progress; this fixture is not a migration or product acceptance claim.

## Runner runbook

Save reviewed configurations under ignored `.publisher/benchmark/`; environment variables hold tokens and their values must never appear in these files or reports. The following four examples are templates, not provisioned targets. Replace placeholders deliberately. No new account, database or deployment is created by these commands.

Local SQLite configuration, `.publisher/benchmark/sqlite.json`:

```json
{"backend":"sqlite","databasePath":".publisher/benchmark/local.db"}
```

The local destination must be new/empty; it is generated output, never an original catalogue. Import requires the explicit execution flag even locally. Reusing an already seeded destination for another import fails rather than overwriting it; subsequent `sql` runs are read-only.

```powershell
python benchmark/run.py import --config .publisher/benchmark/sqlite.json --execute --output .publisher/benchmark/local-import.json
python benchmark/run.py sql --config .publisher/benchmark/sqlite.json --repetitions 5 --output .publisher/benchmark/local-sql.json
```

D1 configuration, `.publisher/benchmark/d1.json`:

```json
{
  "backend":"d1",
  "accountId":"00000000000000000000000000000000",
  "databaseId":"00000000-0000-0000-0000-000000000000",
  "tokenEnv":"COLOSSEUM_BENCH_D1_TOKEN",
  "disposableDatabase":true,
  "freePlanReviewed":false,
  "usageReviewUtc":null
}
```

Turso/libSQL configuration, `.publisher/benchmark/turso.json`:

```json
{
  "backend":"turso",
  "url":"https://reviewed-disposable-target.example.turso.io",
  "tokenEnv":"COLOSSEUM_BENCH_TURSO_TOKEN",
  "disposableDatabase":true,
  "freePlanReviewed":false,
  "usageReviewUtc":null
}
```

**Remote imports are conditional instructions:** use only authorized disposable, empty targets after checking the actual Free plan, disabled paid overages where applicable, account-wide current usage, available day/month allowance and target ownership. Then record a real UTC `usageReviewUtc` and set `freePlanReviewed:true`. Do not copy a review flag from another account/period. Never point this importer at the existing D1 staging databases or an application database. Sample configurations intentionally leave review false/null and are not executable approvals.

```powershell
# Only after the operator checks above and explicit authorization for these targets:
python benchmark/run.py import --config .publisher/benchmark/d1.json --execute --output .publisher/benchmark/d1-import.json
python benchmark/run.py import --config .publisher/benchmark/turso.json --execute --output .publisher/benchmark/turso-import.json

# Read diagnostics against the successfully imported identical seed:
python benchmark/run.py sql --config .publisher/benchmark/d1.json --repetitions 5 --output .publisher/benchmark/d1-sql.json
python benchmark/run.py sql --config .publisher/benchmark/turso.json --repetitions 5 --output .publisher/benchmark/turso-sql.json
python benchmark/run.py compare --left .publisher/benchmark/d1-sql.json --right .publisher/benchmark/turso-sql.json --output .publisher/benchmark/comparison.json
```

The seed remains 21 cases / 967 rows / 2,884 estimated writes; SQL batches may be smaller than 32 rows to remain within 100 parameters. The runner also accounts for manifest/control work. It verifies emptiness and marks loading/ready state, checks imported counts, and stops on provider errors, quota, malformed responses, parity failure or network uncertainty. Partial import evidence is retained; inspect target state and usage before any recovery. There is no automatic destructive reset, blind retry or full-source upload.

Direct D1 REST and Turso SQL-over-HTTP measurements are diagnostic and include their management/data API paths; they do not measure deployed Worker bindings, signed client routes or Qt loading. Protocol references: [Cloudflare D1 query API](https://developers.cloudflare.com/api/resources/d1/subresources/database/methods/query/), [Turso SQL-over-HTTP reference](https://docs.turso.tech/sdk/http/reference). Neither path proves application acceptance.

For paired runs, use the same seed/query digests, repetition count, sampling and request path category. Alternate D1→Turso and Turso→D1 order across repeated pairs; retain each report rather than overwriting history. Record provider account/database usage immediately before and after imports and query batches. Missing per-query meters remain unknown until reconciliation. Report first-observed and repeat p50/p95 separately; no cache flush is performed and cold-cache status remains unproved. `compare` validates comparable hosted diagnostics but always leaves winner selection to receiving review; a faster small sample never automatically selects the stack.

HTTP configuration, `.publisher/benchmark/http.json`, maps only explicitly deployed JSON/asset routes:

```json
{
  "label":"reviewed-worker-path",
  "tokenEnv":"COLOSSEUM_BENCH_READ_LEASE",
  "allowLocalhost":false,
  "routes":[
    {"id":"tankoban-json","url":"https://reviewed-worker.example/explicit/versioned/tankoban-route","kind":"json","authenticated":true,"sha256":"0000000000000000000000000000000000000000000000000000000000000000","requireEtag":true},
    {"id":"universe-asset","url":"https://reviewed-worker.example/explicit/versioned/universe-asset","kind":"asset","authenticated":true,"sha256":"0000000000000000000000000000000000000000000000000000000000000000","requireEtag":true}
  ]
}
```

Replace each URL with an authorized deployed route and each zero hash with the independently computed expected body SHA-256; do not derive the expected hash from the same unverified response being tested. Keep authentication explicit and provide the valid lease through its named environment variable. ETag revalidation must return an empty 304. A route without its required hash/authentication/ETag contract is not qualified; no route discovery or speculative asset fetching occurs.

```powershell
python benchmark/run.py http --config .publisher/benchmark/http.json --repetitions 5 --output .publisher/benchmark/worker-http.json
```

HTTP runs request identity encoding, bound the run to 200 requests, 64 MiB received bodies in total and 10 MiB per response, and stop on failure. At most 20 mapped routes are accepted; select route/repetition counts that fit the allowance, including first requests and conditional revalidation. This measures transport bytes/timing, body hashes and revalidation; it does not measure production compression, native image decode, first paint, cold cache, geographic behavior or the complete Universe/Tankoban acceptance journeys.

These preparation runs performed no signup, provisioning, native edits, GitHub REST calls, R2 changes or Feria/Vinyl work. Hosted measurements require configured, reviewed free targets. See the [preparation evidence and next measurements](../../../docs/research/2026-10-14-content-benchmark-results.md) and [complete Tankoban receiving matrix](../../../docs/research/2026-10-14-content-benchmark-acceptance.md) before selecting a stack.
