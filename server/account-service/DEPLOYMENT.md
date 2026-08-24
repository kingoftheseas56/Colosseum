# Account Service — deployment

**Status: not deployed.** The service is adopted and verified in this tree — `go vet`,
`gofmt`, and `go test -p 1 -race -count=1 ./...` all green against real PostgreSQL — but
nothing is hosted, no URL is baked into any build, and no shipped Colosseum can sign in.
Every account request in a released install still fails with `transport_configuration` /
"The account service configuration is invalid."

This file is the runbook for closing that gap. The steps below need account ownership,
payment, and secret custody, so they are Hemanth's to execute; an agent must not create
the accounts or spend the money.

## What already exists

- Full identity surface: accounts, sessions, devices, device-approval challenges,
  recovery keys, password change, profile, avatar, new-device protection.
- Generic sync: `POST /v1/sync/push`, `GET /v1/sync/pull?after=` — the endpoints the
  desktop `SyncEngine` adapters (collection, progress, history, preferences) call.
- PostgreSQL persistence with three advisory-locked, idempotent migrations.
- argon2 password hashing, HMAC-keyed recovery, wrapped sessions, encrypted sync
  payloads, rate limiting, and a password blocklist.
- `Dockerfile` and `fly.toml` targeting Fly.io.
- CI at `.github/workflows/account-service-ci.yml` (repository root, path-filtered).

## Blocking steps

### 1. Provision

Create, outside source control, on Hemanth's own accounts:

- a Fly.io app (`fly.toml` is the intended target),
- a managed PostgreSQL instance, with a **tested** restore, not merely enabled backups,
- optionally a Tigris/S3 bucket, only if avatar upload is wanted — the shipped client
  uses built-in avatar ids, so this can be deferred at first launch.

### 2. Generate the four secrets

Generate independently, once, and store them as host secrets — never in this repository.
`SESSION_WRAP_KEY` and `SYNC_DATA_KEY` must each decode to exactly 32 bytes; the HMAC keys
must decode to at least 32 bytes.

```bash
for k in RECOVERY_HMAC_KEY ABUSE_HMAC_KEY SESSION_WRAP_KEY SYNC_DATA_KEY; do
  echo "$k=$(openssl rand -base64 32)"
done
```

Losing `RECOVERY_HMAC_KEY` invalidates every issued recovery key; losing `SESSION_WRAP_KEY`
signs every device out; losing `SYNC_DATA_KEY` makes stored sync payloads unreadable.
Treat all four as unrecoverable-on-loss.

The dev values in the preserved `_local_run.env` under Preflight-Architect were
deliberately **not** adopted into this tree. Do not promote them to production.

### 3. Domain and TLS

`AccountHttpTransport::isAllowedBaseUrl` accepts `https://` anywhere but `http://` only on
loopback. A production endpoint must therefore be `https://<host>` with a real
certificate — a plain-HTTP host fails every request exactly like an unconfigured build.

### 4. Point the clients at it

Desktop release builds bake the URL in at configure time:

```bash
cmake -S native -B native/build-msvc -DCOLOSSEUM_ACCOUNT_SERVICE_URL="https://<host>"
```

The build refuses a URL that the runtime would reject, so a misconfigured release fails at
configure time instead of shipping. `COLOSSEUM_ACCOUNT_SERVICE_URL` in the environment
still overrides the baked value, so a baked build can be pointed at
`tests/mock-account-service` without reconfiguring.

Then set `RELAY_ACCOUNT_SERVICE_URL` on the deployed watch-party Worker. The relay
validates bearers against this service's `GET /v1/profile`
(`server/watchparty-relay/src/auth.ts`), so this step is what unblocks public signed-in
Watch Party hosting.

### 5. Soft-launch before baking

Deploy first, exercise a real desktop instance against the deployed URL via the
environment variable, and only bake the URL into a public release once it has held up.
The client shipped before the service did, so the service must satisfy the client, not the
reverse — see the contract-drift notes in `tests/mock-account-service/README.md`, where an
`avatar_id`/`challenge_id` field mismatch was already caught and fixed service-side.

## Operating it

This is the project's first always-on service holding user credentials. Before it carries
real accounts: monitoring and an alert path, a restore actually tested once, a migration
discipline for future schema changes, and an answer for "the service is down and every
client's sign-in fails."

## Out of scope at first launch

The Data & privacy policy switches, data export, and the account-deletion flow lack
authoritative wiring on both the client and the service. They remain a known boundary and
should not be advertised as working.
