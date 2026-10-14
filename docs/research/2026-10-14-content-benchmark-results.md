# Content benchmark preparation and evidence

Status on 2026-10-14: **local preparation verified; hosted backend decision pending**.
Sol 6.1 agents prepared the shared catalogue fixture and content-load corpus and independently reviewed acceptance and runner correctness. The candidate recommendation remains provisional: test Workers + Turso Free against a reduced D1 projection, with R2 optional. No backend has won this comparison.

## Verified in this preparation

| Evidence | Result | Limit |
| --- | --- | --- |
| Benchmark unit and localhost HTTP checks | 25 passed | Provider protocol responses are fixtures; the real HTTP server is localhost |
| Shared real-source SQL fixture | 967 rows, 11 tables, 21 query cases | Selected population with relationship closure, not full-catalogue load |
| Read-only source oracle versus local projection | All 21 exact ordered comparisons passed | SQL fields only; native postprocessing remains separate |
| Actual local import and runner execution | Import completed; all 21 cases passed across 126 executions (first observation plus five repeats) | SQLite on this workstation, not hosted D1/libSQL or Worker latency |
| Estimated fixture bootstrap | 2,884 writes; 54,356 bytes of compact row JSON | Modeled index/control allowance, not provider billing |
| Universe/tribute roster | 21 standard routes and four tribute faces inventoried | Three existing JSON-backed universes extracted; remaining embedded content explicitly has extraction gaps |
| Content and asset fixture | 36 JSON units / 37,617 bytes; 137 original assets / 160,918,559 bytes | Inventory is not the first-screen transfer set or an asset-pack proposal |
| Local content integrity/read measurement | 173 resources, three samples each | Disk reads/JSON parsing, not Qt image decode, network or visible first paint |

Shared seed digest: `8adb1914b64ee7715c564bba0b53ffd0f2352357ea95b26023e6d79ce48c1fe4`.
Generated source rows, copied artwork, config and raw reports stay in ignored `server/content-api/.publisher/benchmark/`. They are not committed or delivered to app users. The reproducible preparation scripts and compact evidence here are committed.

Actual local commands, from `server/content-api`, after preparing the source fixture and a local config:

```powershell
python -m unittest discover -s benchmark -p 'test_*.py' -v
python benchmark/run.py import --config .publisher/benchmark/local.config.json --seed .publisher/benchmark/seed.json --output .publisher/benchmark/local-import.json --execute
python benchmark/run.py sql --config .publisher/benchmark/local.config.json --seed .publisher/benchmark/seed.json --output .publisher/benchmark/local-sql.json --repetitions 5
```

## What the next measurements must decide

1. Confirm the actual free plans, available allowance and disposable benchmark targets. The prepared adapters need a D1 target/token and Turso URL/token through environment variables. This preparation did not create accounts, provision databases, enable billing or exercise hosted credentials. Existing D1 staging remains untouched.
2. Import this same bounded seed once per empty target; reconcile observed writes against before/after provider usage. Run the identical SQL workload in alternating D1/Turso blocks. A rejected long LIKE pattern, wrong ordering, quota error or incomplete metadata is a finding, never a passing fast response. Stop before escalating scale if parity or cost is uncertain.
3. Qualify full-population query shape and the real bootstrap/update budget in further bounded slices. The tiny sample cannot establish the cost of full catalogue discovery, joins or ranking. Test native Comics ranking and Tankoban volume ordering, not just source SQL.
4. Measure the actual authenticated Worker routes separately. Direct Cloudflare management REST and Turso SQL HTTP cross different gateways; their timings cannot choose the user-facing architecture. Capture request counts, response bytes, p50/p95, observed rows read, errors and Worker CPU where available. Keep unavailable metrics explicitly unknown.
5. Map generated JSON and selected artwork to deployed URLs. Compare the same bytes, authentication and cache policy across the available delivery candidates. R2 can enter this comparison after registration; the fixture is independent of its storage location. ETag/304 transport checks are implemented; revision changes, missing assets, renderer incompatibility and last-good recovery are declared scenarios needing deployed/native evidence.
6. Run the installed-app journey matrix for **all Tankoban except chapter mode**, plus Universe and tribute loads. Capture first usable cards, artwork decode/render, cancellation, warm/cold cache conditions and offline/error recovery. Manga search currently uses AniList; a MAL database benchmark cannot claim to cover that journey. Volume/comic source acquisition stays with its existing resolver owners.

The decision requires correct results, a measured sustainable free budget and acceptable actual app loading. If neither candidate meets those gates, retain the existing behaviour while revising the projection/delivery plan. Do not choose the smaller number from an incomparable timing table. `run.py compare` deliberately produces no automatic winner even when direct SQL reports are comparable.

## Scope and review corrections

This is benchmark tooling and a receiving plan, not the native migration or release activation. The 1.1.9 integration remains subject to the [rollout gates](2026-10-14-content-api-119-rollout.md); Feria and Vinyl remain excluded. Local identity, library, history and progress remain canonical. No whole-database, content-pack or post-release bulk-sync fallback was introduced. No GitHub REST transport was used.

Source review corrected the earlier native IMDb search description: `ImdbCatalog.cpp:233-238` binds `norm + '%'` (prefix), while Comics needs escaped all-word substring matching. The additional IMDb substring case is an exploratory workload. The plan, projection budget and review ledger now distinguish those behaviours.

The runner rejects nonempty import targets, requires explicit remote free-plan/disposable-target attestation, limits requests/bytes, never automatically retries uncertain writes, stops on query/transport failures and preserves observed partial-import metrics. These local guards supplement actual account usage checks; they do not guarantee provider billing or validate an operator's attestation.

Use the [benchmark runbook](../../server/content-api/benchmark/README.md), [Tankoban acceptance matrix](2026-10-14-content-benchmark-acceptance.md), and [Universe/asset fixture contract](2026-10-14-content-load-benchmark.md) for the next run.
