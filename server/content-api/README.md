# Colosseum content API staging foundation

This Worker serves imported catalogue rows and cached MusicBrainz metadata through authenticated HTTP APIs. It does not migrate the native clients, introduce a hosted Vinyl resolver, download databases to clients, or replace Colosseum's existing identity, library, history, progress, or preference owners. Core local use remains independent of third-party accounts.

Staging Worker: https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev

| D1 binding | Database | ID |
| --- | --- | --- |
| `CATALOG` | `colosseum-catalog-staging` | `05a2f058-5df9-4636-a772-34a2672fb655` |
| `METADATA` | `colosseum-metadata-staging` | `9ea859a1-f800-4c24-b24a-476872eec0e2` |

The staging schema and import controls are installed for all 24 manifest tables. The 2026-10-14 bootstrap completed 10 source tables with 24,208 rows: `comics__curated_series` (1,134), `comics__curated_edition` (9,655), `comics__curated_genre` (1,821), `comics__curated_series_coverage` (1,134), `comics__curated_source_issue` (156), `tankoban__series` (10,000), `imdb__meta` (2), `mal__meta` (2), `mal__tag_count` (152), and `mal__classification_count` (152). The import ledger reserved 49,584 write units on UTC 2026-10-13. The other 14 source tables remain unimported; readiness of individual tables does not imply complete native catalogue coverage. `/admin/status` is the authoritative current state. The Beatles artist MBID `b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d` was fetched from MusicBrainz and cached successfully.

## API contracts

Requests use an identifying `User-Agent: ColosseumContentPublisher/1.0 (https://getcolosseum.world)`. The default urllib User-Agent received Cloudflare 403 / error 1010 during staging verification. The publisher uses its own identity, without browser impersonation.

`GET /healthz` is public. `/admin/*` requires `Authorization: Bearer ADMIN_TOKEN`. `/v1/*` requires a signed read lease. Admin credentials belong only in trusted publishing tools, never native client distribution.

| Endpoint | Behavior |
| --- | --- |
| `GET /admin/status` | `{tables:[{name,source_hash,expected_rows,imported_rows,ready}],budget:[{day,reserved_writes}]}`; budget dates are UTC. |
| `POST /admin/import` | JSON `{table,sourceHash,chunkId,rows}`. Rows are arrays in `src/source-schema.json` column order, at most 32 per request and 256 KiB for the entire body. Returns `{ok:true,replayed:boolean}`. A reused chunk ID must have identical table/hash/rows; a conflict fails. |
| `POST /admin/complete` | `{table,sourceHash}`. Sets ready only when ledger and actual table counts both match the manifest. |
| `POST /admin/session` | Staging only: returns `{token,expiresAt}` for a 30-minute read lease. Production account/guest lease issuance is still missing. |
| `GET /v1/catalogues` | Returns catalogue names, source hashes, expected/imported row counts and readiness. |
| `GET /v1/catalogues/:table?after=0&limit=25` | Returns `{items,nextCursor}` using `_cursor` rowid pagination; limits 1–100. Only ready tables are readable; incomplete tables return 503 `CATALOGUE_NOT_READY`. |
| `POST /admin/enrichment` | `{kind,mbid,fetchedAt,sourceUrl,payload}` stores one verified MusicBrainz response. |
| `GET /v1/enrichment/musicbrainz/:kind/:mbid` | Returns cached data and provenance. Cache misses return 404 `ENRICHMENT_NOT_CACHED`; reads never automatically fetch upstream. |

MusicBrainz kinds are `artist`, `release`, `release-group`, `recording`, `work`, and `label`. MBIDs are explicit UUIDs. The service does not automatically map these identities to Colosseum entities. Import quota failures return HTTP 429 `{error:"IMPORT_DAILY_BUDGET"}`; other errors must be investigated rather than skipped.

## Bounded publishing and safe resume

Run commands from this directory with Python 3.11 or later. Keep `.publisher/config.json` local and ignored:

```json
{"baseUrl":"https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev","adminToken":"<local admin token>"}
```

`.publisher/config.json`, `.publisher/secrets.json`, and all checkpoints are ignored. Do not commit secrets, local source databases, or generated runtime artifacts.

```powershell
# Smoke: at most one chunk from the selected table.
python tools/publish.py publish --source-dir "C:\catalogue-snapshot" --tables tankoban__series --max-chunks 1

# Resume in explicit priority order, with a 45,000 estimated-write daily ceiling.
python tools/publish.py publish --source-dir "C:\catalogue-snapshot" --tables comics__curated_series,tankoban__series --max-estimated-writes 45000

# One upstream entity request, then cache it through the API.
python tools/publish.py enrich --kind artist --mbid b10bbbfc-cf9e-42e0-be17-e2c3e1d2600d
```

Omit `--tables` to select all manifest tables. Comma-separated names retain the specified priority. `--config` and `--state-dir` are global options placed before the subcommand. The default allowance is 45,000 estimated writes; `--max-estimated-writes` cannot exceed the server's 60,000 import cap.

The publisher checks the source dataset SHA-256 against both manifests, opens SQLite read-only, and selects deterministic rowid chunks. Source files must remain unchanged throughout publication: hash verification does not create an immutable snapshot. Body size may reduce a chunk below 32 rows; an oversized single row stops publication.

`.publisher/progress.json` is an atomic checkpoint. Pending chunk bodies are saved before the network request, so an uncertain acknowledgement can be replayed identically. A replay advances the local cursor once and does not charge the local estimate again. Preserve this checkpoint for the entire import; deleting it or changing the state directory can invalidate safe continuation. The publisher also uses an exclusive `.publisher/publisher.lock`. A crash may leave the lock behind; remove it only after confirming no publisher is running. Do not run simultaneous publishers from separate state directories.

Each invocation starts from the server's current UTC budget, so restarting does not reset the allowance. Quota or configured-budget stops are graceful and resumable. Other failures stop with a nonzero exit and retain pending intent. Resume with the same source, configuration and checkpoint after investigating the error or after the daily budget resets.

MusicBrainz requests share the publisher lock and persist request timing, with at least 1.1 seconds between requests. `enrich` performs one request for the known ID and has no blind retry for 429 or other upstream errors.

## Free-tier envelope and operations

The source snapshot contains 24 tables, 1,133,337 rows and 164,626,432 source bytes. The manifest estimates 2,823,109 source/index row writes before import ledger overhead. A full import therefore takes weeks within the conservative 60,000 estimated-write daily cap. SQLite source bytes are not a measurement of resulting D1 storage.

The configured free-tier planning envelope is 5 million rows read/day, 100,000 rows written/day, 5 GB account storage and 500 MB per database. The lower import cap reserves headroom; import estimates are conservative planning units, while Cloudflare's actual usage remains authoritative. Free limits are shared with other resources on the account. This setup uses no R2, GitHub REST requests or database-download delivery route. No subscription upgrade was requested. The connector could not read billing subscriptions, so the account's subscription state was not independently verified.

### Why this free stack

D1 retains SQLite query semantics and includes data transfer without egress charges. Its daily limits make it a good starting point for indexed catalogue reads, but a slow choice for a large initial import. Workers also has a request/CPU allowance; production capacity needs measured request and row-read budgets, not a claim of unlimited free users. [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/), [Workers pricing](https://developers.cloudflare.com/workers/platform/pricing/).

Supabase Free currently includes a 500 MB database and 5 GB egress, with inactive projects paused after a week. Neon Free currently lists 1 GB per project, 100 CU-hours and 5 GB included public transfer. Those PostgreSQL options are worth reconsidering if import speed or richer SQL becomes the dominant requirement; neither is an unconditional free-capacity upgrade for this read-heavy SQLite catalogue. This is an architectural choice, not a latency benchmark. [Supabase](https://supabase.com/pricing), [Neon](https://neon.com/pricing).

The staging resources were created and deployed through the connected Cloudflare MCP because local Wrangler authentication had expired. Conventional commands below are available once an authorized Wrangler session is restored; restoring login is a separate operational step.

```powershell
npx wrangler d1 migrations apply colosseum-catalog-staging --remote
npx wrangler d1 migrations apply colosseum-metadata-staging --remote
npx wrangler secret put ADMIN_TOKEN
npx wrangler secret put SESSION_SECRET
npx wrangler deploy
```

`tools/prepare_sources.py` inventories read-only SQLite sources and generates the schema, source migration and manifest for a fresh snapshot. It is not an upgrade mechanism for already imported data. Separate source-version migrations, replacement/reset handling and production guest/account lease integration remain unimplemented. Do not regenerate manifests against changed data and resume an existing import as though it were the same snapshot.

## Verification

```powershell
npm run types
npm run check
npm test
python -m unittest discover -s tests -p test_publisher.py -v
```

Publisher tests cover identical replay after uncertain acknowledgement, keyset resume, server budget carryover, daily quota stops, source hash mismatch, malformed HTTP, request-size bounds, exclusive locking, identifying headers and MusicBrainz request spacing. Worker tests and local type checks establish component behavior; staging API checks establish live service behavior. Neither proves full client migration or production readiness.

Verified in this setup: 10 publisher tests, 7 Miniflare/D1 tests, TypeScript check, Worker dry-run build, zero npm audit findings, exact source-manifest regeneration, and live protected reads/MusicBrainz caching. Source-version mismatches fail closed for publication and reads. The non-Feria/non-Vinyl release cutover is specified in [the 1.1.9 rollout plan](../../docs/research/2026-10-14-content-api-119-rollout.md); native integration and release qualification remain outstanding.
