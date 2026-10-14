# D1 volumes import + guest gate preparation — receipts and runbook

Date: 2026-10-14. Branch: `codex/content-api-free`. Scope: finish the Tankoban
volumes import on the existing staging D1, prepare the guest gate
configuration, and record exact hosted evidence. No production resource was
created or changed; everything targets the existing staging Worker + D1 pair.

## Volumes import (hosted, real)

- Tool: `tools/publish.py` (unchanged) against
  `https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev`
  admin API, source
  `C:\Users\Suprabha\AppData\Roaming\Brotherhood\Colosseum\catalog-vault\tankoban_catalog.db`
  (sha256 `04d485c6a559fdc7…` — matches `source-manifest.json` and the
  server-side `_source_tables` manifest; the app's own downloaded database is
  the canonical source).
- Invocation:
  `python tools/publish.py publish --source-dir <catalog-vault> --tables tankoban__volumes --max-estimated-writes 59000`
- Result: `{"stopped": "estimated-write-limit", "chunks": 797, "estimatedWrites": 58978}`.
- Server state after: `tankoban__volumes imported_rows=25504 / 28839, ready=0`;
  `_import_budget` day `2026-10-14` = `58978` reserved writes (previous day
  2026-10-13 = 49584). The self-imposed 60,000/day server trigger is the
  stopping bound; the plan's hard free limit is 100,000 rows written/day, so
  today closes with ~41K of plan headroom.
- Remaining: 3,335 rows (~6,700 estimated writes at weight 2). Resume on the
  next UTC day with the same command (checkpoint `lastRowid` is durable in
  `.publisher/progress.json`; chunk replay is idempotent by `chunkId`):
  `python tools/publish.py publish --source-dir <catalog-vault> --tables tankoban__volumes --max-estimated-writes 45000`
  followed by the publisher's automatic `/admin/complete`, which flips
  `ready=1` after its row-count check.

## Hosted verification receipts (2026-10-14)

- `GET /healthz` → `200 {"ok":true,"environment":"staging"}`.
- `GET /v1/catalogues/tankoban__series?limit=3` with staging broad lease →
  `200`, keyset rows `{mal_id:1,volume_count:8,count_basis:"bookwalker"}`,
  `{mal_id:2,volume_count:0,count_basis:"mal"}`,
  `{mal_id:3,volume_count:22,count_basis:"mal"}`, `nextCursor:3` — the read
  lane and imported data are live and correct.
- `GET /v1/catalogues/tankoban__volumes?limit=1` → `503 CATALOGUE_NOT_READY`
  — honest not-ready until the remaining rows complete.
- `POST /v1/guest/session` (correct body, no token) → `401 UNAUTHORIZED`,
  and `GET /v1/tankoban/series/13` with the broad lease → `404 NOT_FOUND`:
  the currently **deployed** staging Worker predates the guest/semantic
  routes (the request falls through to the generic bearer check). The source
  in this branch has the routes and its 11 Miniflare tests pass, including
  guest-disabled fail-closed and limiter fail-closed — but a deploy is
  required before any client can use them.

## Guest gate configuration (this branch, undeployed)

`wrangler.jsonc` now carries `TANKOBAN_GUEST_ENABLED: "true"` and a
`GUEST_RATE_LIMITER` rate-limiting binding (`simple: 30 requests / 60 s`,
per connecting IP, free tier). Worker tests: **11 pass / 0 fail** with the
config in place. `SESSION_SECRET` is deliberately NOT in the file.

## Deploy runbook (BLOCKED: wrangler auth expired)

Exact blocker: `wrangler whoami` reports "Not logged in. Your auth token has
expired and could not be refreshed, and the environment is non-interactive."
No `CLOUDFLARE_API_TOKEN` is present in the environment. Requires Hemanth
(or an authorized operator) to run, in `server/content-api`:

1. `npx wrangler login` (interactive browser OAuth), or export
   `CLOUDFLARE_API_TOKEN` with Workers Scripts (Edit) + D1 (Edit) scope.
2. Generate a secret without printing it:
   `python -c "import secrets;print(secrets.token_urlsafe(48))" | npx wrangler secret put SESSION_SECRET --name colosseum-content-api-staging`
3. `npx wrangler deploy` (ships current source + vars + rate-limit binding).
4. Verify, in order: `GET /healthz` → 200; `POST /v1/guest/session` before
   volumes complete → `503 CATALOGUE_NOT_READY` (gate open, data honest);
   after tomorrow's resume completes → `200 {token,expiresAt}`; then
   `GET /v1/tankoban/series/13` with that guest lease → a `ready:true`
   series payload.
5. Then the native end-to-end hook can run on the candidate:
   `tst_tankoban_catalog actual_worker_contract` with
   `COLOSSEUM_CONTENT_TEST_URL=https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev`,
   `COLOSSEUM_CONTENT_TEST_MAL_ID=13`, and the expected volume count read
   from the local source DB.

Rollback: `npx wrangler secret delete SESSION_SECRET` (or remove the var and
redeploy) returns the guest lane to fail-closed 503; the imported rows are
additive and inert until `ready=1`.
