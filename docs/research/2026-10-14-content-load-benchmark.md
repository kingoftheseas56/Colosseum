# Universe and asset benchmark preparation

This is local fixture preparation for the content API comparison. No cloud storage was registered, no URLs were guessed, and no CDN, database, first-paint or Qt renderer performance was measured. R2 remains pending.

Run from the worktree using Python's standard library:

```powershell
python server/content-api/benchmark/test_content_load.py
python server/content-api/benchmark/content_load.py prepare --repo . --assets-root '<explicit source assets/universes directory>' --output server/content-api/.publisher/benchmark/content-load
python server/content-api/benchmark/content_load.py measure --output server/content-api/.publisher/benchmark/content-load --repeats 3
```

The source directory is explicit because the primary checkout can contain assets absent from the isolated worktree. Generated media, JSON bodies and measurements belong under the ignored `.publisher/` directory. The script writes only the requested output directory; it reads source files without executing JavaScript or QML. Output excludes absolute source paths, credentials, databases and private state.

## Fixture contract

`manifest.json` version 1 contains `routes`, `units`, `assets`, and `extraction_gaps`. Body paths are relative to the fixture directory. Each unit has an ID, byte count, SHA-256 and quoted content-derived ETag. Each asset has its source-relative ID, hash-addressed body path, byte count, MIME, header dimensions where available and `variant: original`. SVG dimensions come from viewBox units; these are not necessarily CSS pixels. JPEG/PNG dimensions are header inspection, not decoder qualification. Other formats retain unknown dimensions. No thumbnail or alternate format is fabricated.

Each standard route declares all `available_units`; the initial bounded `requested_units` selects its header and first section only. Further section navigation must request the corresponding available unit explicitly. These are proposed measurement units, not an assertion that installed QML already uses these endpoints. Fixtures do not replace current canonical caches or provider identity owners.

`request-traces.json` version 1 has a `cases` array. A case identifies route, face, cache scenario, resource IDs, expected transport behavior, and `runnable`. `http_mapping` remains null and `deployment_mapping_required` is true. A host runner must explicitly map unit and asset IDs to actual deployed HTTP URLs and map `body_path` to fixture bytes. It must preserve the conditional ETag, revision consistency and failure expectations. No mapping may be inferred from file names. Each JSON unit is limited to 2 MiB and each original asset to 64 MiB.

The six cases are cold start, warm ETag/304, revision change, missing asset/404, unsupported renderer rejection, and last-good recovery after network failure. These are test declarations for a future host/installed-client runner; they do not simulate or claim verification of Colosseum's native caching. Revision-change bodies, actual renderer capability checks and native last-good evidence still require implementation. Empty asset selections explicitly require renderer request capture; there is no evidence-based first-screen asset set yet. Inventorying/copying originals is preparation, not a proposal to download an asset pack.

## Actual preparation evidence

On 2026-10-14, the commands above completed using the primary checkout's explicit assets directory. Five unit checks passed: deterministic preparation/reference integrity, cache declarations, JSON body bound, PNG/SVG header dimensions, and rejection of mutated hashes/path escapes. Preparation found 21 standard roster routes plus four tribute faces; 36 JSON units total 37,617 bytes, with a largest unit of 3,506 bytes. The 137 original assets total 160,918,559 bytes: 113 PNG, 11 JPEG and 13 SVG. Local measurement completed for all 173 resources with three samples each. Timings are saved locally and labelled OS-cache-uncontrolled disk reads and JSON parsing; they cannot select Turso/Workers versus D1 or prove network latency.

The standard JSON fixtures are One Piece, DCAU and Star Wars. Other standard routes remain explicitly blocked on declarative extraction from `Universes.js`. All four tribute faces (One Piece, DCAU, Star Wars and Cosmere) remain extraction gaps with installed source hashes and sizes. No embedded module is mislabeled as an API JSON payload. The existing Universe inventory remains the source of the broader migration boundaries.

First-screen asset capture, deployed payload mappings, authentication/leases, network byte counts/latency, first paint, Qt image decode/render and live installed failure/last-good behavior remain unmeasured. Tankoban chapter mode is outside this fixture slice; parent catalogue query preparation owns Tankoban's other units.
