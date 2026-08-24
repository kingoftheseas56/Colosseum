# Colosseum Account Service


This repository is the external identity and cloud-sync service for Colosseum.

**Bundle 7B cumulative** carries the Bundle 2A identity/security vertical, the 6A generic Gate-C sync journal/cursor substrate, and the Slice 14B full-history category activation:

- username/password account creation and sign-in;
- permanent case-insensitive username reservation with preserved display casing;
- Argon2id password hashing and common/weak-password rejection;
- opaque access + rotating refresh sessions;
- refresh retry grace and replay-triggered session revocation;
- one active recovery key stored only as a keyed verifier;
- password recovery and trusted-device recovery;
- password change, recovery-key replacement, and username rename/cooldown;
- private device listing/revocation and log out everywhere;
- optional protected new-device approval;
- built-in/uploaded avatar ownership with private Tigris-backed objects;
- PostgreSQL-backed abuse throttling and security-event records;
- deterministic service-native unit/integration tests against disposable PostgreSQL.

The service still does **not** implement desktop production domain adapters, Recovery-area UI, export, or account deletion. Ordinary schema-v1 records are accepted for the six previously frozen safe categories plus Bundle 7B `full_history`; blocked/secret/local-only categories still fail closed.


## Generic sync contract

Protected endpoints:

```text
POST /v1/sync/push
GET  /v1/sync/pull?after=<server_seq>
```

The account is inferred from the authenticated session. A mutation's `device_id` must equal the authenticated device.

Conflict authority is the lexicographic HLC tuple:

```text
(hlc_physical_ms, hlc_counter, device_id)
```

`server_seq` is replication/catch-up order only and is never the conflict winner.

The service stores:

- append-only per-account mutation journal with unique `(account_id, mutation_id)`;
- one current winner per `(account_id, category, record_key)`;
- first-class tombstones;
- prior winners for 30-day recovery retention;
- application-encrypted ordinary sync payload ciphertext using `SYNC_DATA_KEY`.

Future-clock mutations are rejected per mutation with `clock_skew`, while the response still carries service time and current-winner metadata when one exists.

The server repeats the ordinary-payload exclusion firewall independently of the desktop. Search history, paths, media/blob bytes, session/window/PiP/cast/room state, credentials/secrets, and raw acquisition/transport endpoints are denied.

Bundle 7B intentionally does not compact the journal because cursor-safe compaction policy is not yet approved. It also does not implement account deletion; those are separate lifecycle concerns.

### Bundle 7B full-history activation

Schema-v1 `full_history` is allowed only after the cumulative desktop architecture provides a dedicated profile-owned `HistoryStore`.

The service remains domain-generic. It does not compute watched/read/completed merge semantics. It stores and orders the same encrypted Gate-C record versions under:

```text
category = full_history
record_key = history/<kind>/<media-id>
```

Desktop owner/adapter validation remains responsible for the exact History payload contract; the service still independently rejects forbidden paths, secrets, search/session state, and other ordinary-sync exclusions.


## Toolchain

- Go 1.26.6
- pgx v5.10.0
- PostgreSQL 16.15 in the deterministic CI service

`go.sum` is intentionally generated during adoption from real module resolution; it is not hand-authored in the Preflight draft.

## API convention

Business endpoints begin under:

```text
/v1
```

Business API failures use:

```json
{"error":{"code":"stable_machine_code","message":"safe user-facing message"}}
```

`/healthz` and `/readyz` remain operational endpoints outside `/v1`.

Responses set `Cache-Control: no-store`; credential/session material is never intentionally written to service logs.

## Password contract

Accepted passwords are NFC-normalized, 15–128 Unicode code points, may contain spaces, and have no composition rules. The embedded baseline blocklist prevents an empty denylist in development/test. Production operators should set `PASSWORD_BLOCKLIST_PATH` to a maintained common/breached-password corpus.

Argon2id never goes below the approved floor:

```text
memory: 19 MiB
iterations: 2
parallelism: 1
```

## Session contract

- access token: 256 random bits, 15-minute lifetime, server stores SHA-256 hash only;
- refresh token: 256 random bits, rotated on successful refresh;
- previous refresh token: short retry grace only;
- replay outside grace revokes the device session;
- normal server session has no routine user-visible expiration;
- password change revokes every other session while keeping the current one;
- log out everywhere revokes every session, including current.

## Recovery contract

The service stores only a keyed verifier for the active recovery key. Password recovery or recovery-key use for protected-device fallback consumes that key and issues a replacement. Manually generating a replacement requires current-password re-authentication. There is no support override, security question, email fallback, or phone fallback.

## New-device protection

When enabled, a password-authenticated unknown/revoked installation receives a short-lived challenge rather than a session. A signed-in trusted device may approve/deny it, or the target may use the active recovery key. Approval challenges are scoped, expiring, and one-use.

## Avatar storage

Uploaded avatars are validated as bounded JPEG/PNG images, stored under account-owned object keys, and returned through short-lived presigned URLs. Replaced objects are deleted immediately when possible or placed into the durable cleanup queue.

## Migration policy

Production schema changes are forward-only and follow expand/contract. Do not add destructive automatic down-migrations to the service runtime.

Cumulative schema through Bundle 6 adds:

```text
internal/database/migrations/0002_identity_security.sql
internal/database/migrations/0003_sync_core.sql
```

The Bundle 1 migration runner remains advisory-locked and idempotent.

## Local deterministic verification after adoption

Create a PostgreSQL database whose name ends in `_test`, then set:

```text
TEST_DATABASE_URL=postgres://postgres:postgres@localhost:5432/colosseum_account_test?sslmode=disable
```

Generate real module checksums first:

```text
go mod tidy
```

Then use the repository gate:

```text
git diff --exit-code -- go.mod go.sum
go vet ./...
go test -p 1 -race ./...
go build ./...
docker build --tag colosseum-account-service:test .
```

The destructive integration-test helper refuses a database whose name does not end in `_test`.

Without `TEST_DATABASE_URL` the database-backed suites skip and the run still reports `ok`, so a green result proves nothing unless that variable is set.

The same gate runs in CI from `.github/workflows/account-service-ci.yml` at the repository root, filtered to `server/account-service/**`. GitHub Actions reads workflows only from the repository root, so this directory deliberately holds no `.github/` of its own.

## Runtime configuration

Required:

```text
DATABASE_URL
RECOVERY_HMAC_KEY
ABUSE_HMAC_KEY
SESSION_WRAP_KEY
SYNC_DATA_KEY
```

Production avatar upload additionally requires:

```text
BUCKET_NAME
AWS_ENDPOINT_URL_S3
```

Optional:

```text
COLOSSEUM_ACCOUNT_ENV=development
HTTP_ADDR=:8080
DATABASE_MAX_CONNECTIONS=8
PASSWORD_BLOCKLIST_PATH=
REGISTRATION_GLOBAL_LIMIT_10M=500
SYNC_MAX_FUTURE_SKEW_SECONDS=600
AWS_REGION=auto
```

The security keys are independent deployment secrets. `SESSION_WRAP_KEY` and `SYNC_DATA_KEY` must each decode to exactly 32 bytes; the HMAC keys must decode to at least 32 bytes. The 600-second future-skew default is an operational reference default, not a product promise; operators may override it with a positive value.

## Fly bootstrap

Operational resource names are deliberately not hard-coded. Create/attach the Fly app, Managed Postgres, and Tigris resources outside source control; then supply their real environment/secrets during deployment.
