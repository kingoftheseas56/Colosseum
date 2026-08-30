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
- A plain env-configured `Dockerfile` — no host-specific code anywhere in the service.
  It runs unmodified on any platform that can run a container and set env vars.
- CI at `.github/workflows/account-service-ci.yml` (repository root, path-filtered).

## Chosen stack: Cloud Run + Neon

Target is **Google Cloud Run** for compute and **Neon** for PostgreSQL, not Fly. Fly's own
VM pricing is cheap, but Fly Managed Postgres priced out around $1,000/yr — more than this
project should spend before it has real traffic. Cloud Run's free tier and Neon's free
tier are both standing offers (not time-limited trials) and cover this service's load at
early scale for $0/month:

- Cloud Run Always Free: roughly 2M requests, 360,000 GiB-seconds, and 180,000 vCPU-seconds
  per month, scale-to-zero when idle. Confirm current figures on Google's pricing page at
  provisioning time — free-tier terms change.
- Neon free tier: ~0.5 GB storage, one project, compute that auto-suspends after ~5 minutes
  idle and wakes on the next query in roughly a second.

`fly.toml` is left in the tree as a fallback reference only; it is not the active
deployment target. Do not deploy from it without updating this document first.

Tradeoffs to accept knowingly: cold starts (a container cold start plus, if the database
was also idle, a ~1s Neon wake) on the first request after a quiet period, and a lower
data-retention/restore window on Neon's free tier than a paid plan would give. Both are
acceptable for account/session traffic at pre-launch scale; neither is acceptable to
carry silently once the app has real users — see "When to revisit" below.

## Blocking steps

### 1. Provision Neon

- Create a Neon account and a project. Pick a region close to where Cloud Run will run
  (see step 3) — cross-region hops between the two add latency to every request.
- Create a database (e.g. `colosseum_account`).
- Neon gives both a **pooled** (pgbouncer) and a **direct** connection string. Use the
  **pooled** one for `DATABASE_URL`. Cloud Run can run multiple container instances
  concurrently, each opening up to `DATABASE_MAX_CONNECTIONS` connections; the direct
  endpoint's connection cap is much lower and will fall over under that pattern.
- Before trusting it with real accounts, actually exercise Neon's branch/restore feature
  once. An enabled backup you have never restored from is not a tested backup.

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

Avatar upload (`BUCKET_NAME`, `AWS_ENDPOINT_URL_S3`) is still not required at first launch
— the shipped client uses built-in avatar ids, so object storage stays deferred regardless
of hosting target.

### 3. Create the Cloud Run project and service

```bash
gcloud projects create colosseum-account --name="Colosseum Account Service"
gcloud config set project colosseum-account
# Billing must be attached even for Always Free usage; Google requires a card on file.
gcloud services enable run.googleapis.com cloudbuild.googleapis.com \
  secretmanager.googleapis.com artifactregistry.googleapis.com
```

Store the five secrets (`DATABASE_URL` plus the four generated above) in Secret Manager
rather than as plain env vars:

```bash
printf '%s' "$DATABASE_URL" | gcloud secrets create database-url --data-file=-
printf '%s' "$RECOVERY_HMAC_KEY" | gcloud secrets create recovery-hmac-key --data-file=-
printf '%s' "$ABUSE_HMAC_KEY" | gcloud secrets create abuse-hmac-key --data-file=-
printf '%s' "$SESSION_WRAP_KEY" | gcloud secrets create session-wrap-key --data-file=-
printf '%s' "$SYNC_DATA_KEY" | gcloud secrets create sync-data-key --data-file=-
```

Deploy directly from the existing `Dockerfile` (Cloud Build builds it, no separate registry
push step needed):

```bash
gcloud run deploy colosseum-account-service \
  --source . \
  --region us-central1 \
  --allow-unauthenticated \
  --port 8080 \
  --min-instances 0 \
  --max-instances 3 \
  --set-env-vars COLOSSEUM_ACCOUNT_ENV=production,HTTP_ADDR=:8080,DATABASE_MAX_CONNECTIONS=8 \
  --set-secrets DATABASE_URL=database-url:latest,RECOVERY_HMAC_KEY=recovery-hmac-key:latest,ABUSE_HMAC_KEY=abuse-hmac-key:latest,SESSION_WRAP_KEY=session-wrap-key:latest,SYNC_DATA_KEY=sync-data-key:latest
```

Notes on the flags:

- `--allow-unauthenticated` is required. Cloud Run defaults to requiring IAM auth on the
  invoker, which would reject every real client request — this service's own session
  tokens are the auth layer, not Cloud Run's.
- `--region` should match the Neon project's region from step 1.
- `--max-instances 3` is a deliberate ceiling, not a Cloud Run default (the default is far
  higher). It caps both worst-case cost and worst-case concurrent connections to Neon
  while traffic is low. Raise it — and reconsider `min-instances` — once real usage
  justifies it; see "When to revisit."

### 4. Domain and TLS

Cloud Run issues a `https://*.run.app` URL with a managed certificate automatically — no
manual TLS work, unlike the Fly custom-domain path. `AccountHttpTransport::isAllowedBaseUrl`
accepts `https://` anywhere, so the default `.run.app` URL is usable as-is; a friendlier
custom domain (e.g. `account.colosseum.app`) is optional polish via Cloud Run domain
mapping and can be added later without changing anything else in this runbook.

### 5. Point the clients at it

Desktop release builds bake the URL in at configure time:

```bash
cmake -S native -B native/build-msvc -DCOLOSSEUM_ACCOUNT_SERVICE_URL="https://<cloud-run-url>"
```

The build refuses a URL that the runtime would reject, so a misconfigured release fails at
configure time instead of shipping. `COLOSSEUM_ACCOUNT_SERVICE_URL` in the environment
still overrides the baked value, so a baked build can be pointed at
`tests/mock-account-service` without reconfiguring.

Then set `RELAY_ACCOUNT_SERVICE_URL` on the deployed watch-party Worker. The relay
validates bearers against this service's `GET /v1/profile`
(`server/watchparty-relay/src/auth.ts`), so this step is what unblocks public signed-in
Watch Party hosting.

### 6. Soft-launch before baking

Deploy first, exercise a real desktop instance against the deployed URL via the
environment variable, and only bake the URL into a public release once it has held up.
The client shipped before the service did, so the service must satisfy the client, not the
reverse — see the contract-drift notes in `tests/mock-account-service/README.md`, where an
`avatar_id`/`challenge_id` field mismatch was already caught and fixed service-side.

## Operating it

This is the project's first always-on service holding user credentials. Before it carries
real accounts:

- a Cloud Monitoring uptime check on `/healthz`, with an alert policy to email on failure;
- a GCP budget alert set well below what a real bill would look like, so a traffic spike or
  a misconfigured `--max-instances` shows up as a notification, not a surprise invoice;
- the Neon restore actually tested once (step 1), not merely enabled;
- a migration discipline for future schema changes (the runner is already advisory-locked
  and idempotent — keep using it, don't hand-edit production schema);
- an answer for "the service is down and every client's sign-in fails" — at minimum, know
  where the uptime alert goes and who acts on it.

## When to revisit paid infrastructure

This stack is free until traffic or storage outgrows the Cloud Run and Neon free
allowances above. That is the intended trigger, not a calendar date: if sustained load
starts hitting `--max-instances`, or Neon's storage/connection limits, that is the signal
the app has become the kind of hit that justifies paying for infrastructure — raise
`--max-instances`, move `min-instances` above 0 to remove cold starts, and/or upgrade the
Neon plan at that point, not before.

## Out of scope at first launch

The Data & privacy policy switches, data export, and the account-deletion flow lack
authoritative wiring on both the client and the service. They remain a known boundary and
should not be advertised as working.
