# Collections publications (candidate, not deployed)

The existing account service owns publication authorization and PostgreSQL storage. Native personal collections remain private, profile-owned and offline-capable. There is no new login or vendor. Deploying this candidate requires operator review and the ordinary explicit migration command; no production migration or rollout was performed by this assignment.

## Routes

Existing Bearer sessions protect:

- `POST /v1/collections/publications`: `{operation_id,kind,snapshot,passphrase}`; kind is `collection` or `gift`.
- `GET /v1/collections/publications`: `{publications:[{share_id,kind,revision,name,revoked,protected,created_at,updated_at}]}`. Only the authenticated account's metadata is listed, never every cover.
- `PUT /v1/collections/publications/{share_id}`: `{operation_id,expected_revision,snapshot,passphrase}`. Ordinary publications only. This explicitly replaces the publication and its passphrase (empty removes protection).
- `DELETE /v1/collections/publications/{share_id}`: `{operation_id,expected_revision}`. Revocation is terminal; edition revision does not change.

Anonymous receiving:

- `GET /collections/s/{share_id}` serves the self-contained browser viewer.
- `GET /v1/collections/shared/{share_id}` opens an unprotected publication.
- `POST /v1/collections/shared/{share_id}/open` with `{passphrase}` opens protected or unprotected content. Never put a passphrase in the URL.

Create, update, revoke and open return `{share_id,kind,revision,snapshot,revoked,protected,created_at,updated_at}`. Gift `revision` is always 1. The retained edition key is the pair `(share_id, revision)`. An ordinary edit changes no public content until PUT succeeds. A later gift always gets a new independent share ID. Revocation cannot recall downloaded/kept copies.

Mutations require a fresh UUID operation ID, retained unchanged across retries of that exact request. Account-scoped receipts and the mutation commit in one transaction. Reusing the ID with a different operation, destination or payload returns 409. Retrying an already-applied operation returns the publication's **current** envelope (possibly updated/revoked), without replaying the mutation. Account row locks serialize concurrent mutations and quota checks. `expected_revision` rejects stale editors. A gift update returns 409 even to its owner. Missing, foreign-owned and revoked public links do not expose content.

## Versioned projection

Snapshot v1 has exactly `schema_version`, `name`, `description`, `presentation`, `members`.

Presentation has `preset` (`midnight`, `paper`, `sea`), `dedication`, `signature`, `cover_data`.

Each member has `world`, `kind`, `source`, `source_id`, `edition_id`, `canonical_id`, `canonical_source_id`, `album_source_id`, `album_browse_id`, `comic_post_ids`, `title`, `creator`, `year`, `note`.

The optional canonical title ID is `ct1:` plus a lowercase UUID. `canonical_source_id` may carry only a theatre IMDb `tt` ID. Album source/browse IDs apply only to Vinyl. Comic post IDs are at most 500 distinct positive decimal IDs for GetComics/GCD only. These are explicit canonical navigation facts, not arbitrary reopen payloads. Client exports must not include routes, raw provider payloads, path-shaped IDs, credentials, entitlements, cover URLs or media.

Unknown JSON keys are rejected recursively. IDs are bounded portable ASCII identifiers; URI prefixes, slashes, backslashes and Windows drive-relative forms are rejected. Display strings are plain text, not HTML. Duplicate source identity/edition tuples or canonical title/edition pairs are rejected. Unavailable recipient providers must not cause title replacement or loss.

Bounds, in UTF-16 code units (matching QString and JavaScript length): name 100, description 300, dedication 2000, signature 100, title 300, creator 200, year 32, note 500; 500 members. Body maximum 768 KiB. Custom cover: base64 PNG/JPEG only, at most 256 KiB decoded, dimensions at most 2048×2048. Server fully decodes and re-encodes pixels to strip EXIF/ancillary metadata; sanitized output must still fit 256 KiB. Recipients preserve this sanitized permitted asset, not an external URL. Clients should accept responses up to 2 MiB because encoding/sanitization may increase their size.

## Storage, abuse and operations

Migration `0011_collection_publications.sql` adds two account-owned, cascading tables. The explicit publication snapshot is stored as JSONB, separate from encrypted private sync. Passphrases use the existing Argon2id verifier. Idempotency digests use the existing keyed abuse secret, so receipts do not expose a cheap offline passphrase verifier. Rotating that secret changes retry-digest compatibility; finish pending operations or plan receipt handling first.

Limits: 200 publications per account including revoked editions; 5000 operation receipts per account; 100 new create/update operations per hour/account. Revocation remains available after those quotas are reached; repeating revocation of an already revoked link allocates no new receipt. Successful repeated operations do not consume new mutation quota. Public opening is bounded to 120/minute/network prefix; protected verification to 10 attempts/15 minutes/share/network prefix. Limits use the existing database-backed limiter and trusted client-address handling. Rate limits do not establish named-recipient identity. Link IDs contain 256 random bits. No CORS openness, external image fetch or media proxy is introduced. API/browser responses are noncacheable; the viewer sets restrictive CSP and renders text safely.

The bounded quotas are deliberate operational limits, not automatic billable scaling. Historical receipts/editions are not silently purged or recycled. Review quota growth, retention and costs before production adoption; any paid infrastructure change requires separate authority. Current server-side account deletion cascades publications/receipts, blocking future links. Previously retained local gifts remain independent.

## Reproduction

Use the existing account-service configuration with synthetic accounts and an explicitly disposable `_test` database. Install the Go version from `go.mod`; apply migrations through the existing migration CLI, then run the existing server CLI. Native service endpoint configuration must target that same development origin. Serving viewer and API on one origin avoids introducing a CORS dependency.

Focused commands:

```
go test -p 1 -race ./internal/account ./internal/httpserver ./internal/database -run 'TestCollection|TestRunMigrationsFromEmptyDatabase' -count=1
go test ./internal/httpserver/collectionviewer -count=1
```

Names must be NFC-normalized with trimmed, single-space whitespace.

Set `TEST_DATABASE_URL` to the disposable PostgreSQL connection. Without it, database tests explicitly skip and are not evidence of lifecycle qualification. Run database test packages serially: their existing fixtures reset the test schema. The service tests cover owner isolation, ordinary updates, fixed gift editions, optimistic conflicts, passphrases/rate limits, revocation, duplicate concurrent requests and transactional rollback/retry. HTTP tests exercise unknown nested fields, malicious provider payloads and oversized/trailing bodies. Native/browser runtime qualification remains a separate assembled journey, not implied by these tests.
