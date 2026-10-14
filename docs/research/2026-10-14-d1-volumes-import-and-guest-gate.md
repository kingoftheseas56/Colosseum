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

## Review-findings addendum (2026-10-14, second pass)

**ADMIN_TOKEN rotation — prepared, NOT executed; incident NOT resolved.**
Exact blocker: every authorized Cloudflare route from this machine is
closed. `npx wrangler whoami` reports the OAuth token expired and
non-refreshable non-interactively; no `CLOUDFLARE_API_TOKEN` exists in the
User or Machine environment; the historical house pattern (watchparty-relay
DEPLOYMENT.md) is a one-time interactive `wrangler login` by the account
owner. Rotation requires rewriting the `ADMIN_TOKEN` Worker secret, which
only Cloudflare-side authorization can do. What is ready:
`tools/rotate-admin-token.py --apply` performs the whole rotation in one
authorized shell — generates the new secret (never printed, piped straight
to `wrangler secret put`), derives the publisher bearer exactly like the
Worker's `admin()` (derivation cross-proven against node crypto on a fixed
vector: identical standard-base64 HMAC), atomically rewrites
`.publisher/config.json`, then verifies the OLD token is rejected (401) and
the NEW token accepted (200) against `/admin/status`, printing only status
codes. `--verify-only` re-checks the current token. The exposed credential
remains valid until this runs; treat the staging admin surface as exposed
until then. SESSION_SECRET is untouched — no independent reason to rotate
it (it has not appeared in any transcript).

**Rate-limit namespace fixed and the real config validated.** The limiter
`namespace_id` is now the account-unique positive integer string `"1001"`
(matching the test fixture; no other Worker on the account declares a
rate-limit namespace). Validation ran against the ACTUAL deployment config,
not a fixture: `npx wrangler deploy --dry-run --outdir dist-verify` exits 0
and prints the resolved bindings — `env.CATALOG`/`env.METADATA` (D1),
`env.GUEST_RATE_LIMITER (30 requests/60s)` (Rate Limit),
`env.ENVIRONMENT ("staging")` + `env.TANKOBAN_GUEST_ENABLED ("true")`. The
signing secret requirement stands: without `SESSION_SECRET` (≥32 chars) the
guest lane still fails closed by code. Limiter semantics, corrected: the
Workers rate-limiting binding is approximate and enforced at individual
Cloudflare edge locations — it blunts abuse, it is not a strict
account-wide quota. Staging deployment itself remains blocked on the
wrangler-auth blocker above; release traffic is not enabled (the candidate
now compiles no endpoint by default — finding 3).

**Budget accounting, corrected.** 2026-10-14 closes at 58,978 RESERVED
ESTIMATED writes in the server's `_import_budget` ledger (yesterday
49,584). The ledger is an estimate trail, not actual D1 `rows_written`;
actuals need Cloudflare analytics (GraphQL), which the same auth blocker
prevents, so no free-plan headroom figure is claimed. Remaining volumes:
3,335 rows ≈ 7,720 estimated writes including chunk overhead — gated until
the 2026-10-15 UTC budget day; the resumable publisher completes it in one
run and flips `ready=1` via its row-count-checked `/admin/complete`.
