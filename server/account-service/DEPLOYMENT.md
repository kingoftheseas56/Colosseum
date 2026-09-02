# Account Service — deployment

**Status: not deployed.** The service is adopted and locally verifiable, but database-backed
proof requires `TEST_DATABASE_URL`; nothing is hosted, no URL is baked into any build, and
no shipped Colosseum can sign in.
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
- PostgreSQL persistence with an advisory-locked, idempotent migration runner.
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
- Neon gives both a **pooled** (pgbouncer) and a **direct** connection string. Store the
  pooled URL as `DATABASE_URL` for the service and the direct URL as
  `MIGRATION_DATABASE_URL` for the one-off migration job. Never give the direct URL to
  the service: each of up to three instances may open up to eight pooled connections.
- Before trusting it with real accounts, complete the restore rehearsal in step 7. An
  enabled backup you have never restored from is not a tested backup.

### 2. Generate the runtime and migration secrets

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

Keep the direct migration URL separate from the pooled runtime URL. The migration job
reads `MIGRATION_DATABASE_URL`; the service reads `DATABASE_URL` and never needs the
direct endpoint.

Avatar upload (`BUCKET_NAME`, `AWS_ENDPOINT_URL_S3`) is deferred at first launch via `AVATAR_STORAGE=disabled` in the deploy command below — profiles then serve through avatar.DisabledStore
— the shipped client uses built-in avatar ids, so object storage stays deferred regardless
of hosting target.

### 3. Build one image and migrate before traffic

```bash
gcloud projects create colosseum-account --name="Colosseum Account Service"
gcloud config set project colosseum-account
# Billing must be attached even for Always Free usage; Google requires a card on file.
gcloud services enable run.googleapis.com cloudbuild.googleapis.com \
  secretmanager.googleapis.com artifactregistry.googleapis.com
```

Store the six deployment values in Secret Manager rather than as plain env vars: the
pooled runtime URL, the direct migration URL, and the four runtime cryptographic keys.

```bash
printf '%s' "$DATABASE_URL" | gcloud secrets create database-url --data-file=-
printf '%s' "$MIGRATION_DATABASE_URL" | gcloud secrets create migration-database-url --data-file=-
printf '%s' "$RECOVERY_HMAC_KEY" | gcloud secrets create recovery-hmac-key --data-file=-
printf '%s' "$ABUSE_HMAC_KEY" | gcloud secrets create abuse-hmac-key --data-file=-
printf '%s' "$SESSION_WRAP_KEY" | gcloud secrets create session-wrap-key --data-file=-
printf '%s' "$SYNC_DATA_KEY" | gcloud secrets create sync-data-key --data-file=-
```

Build one image from the existing `Dockerfile`. It contains both the ordinary service and
the dedicated migration binary, so the exact image tested is the image migrated and served:

```bash
export REGION="<cloud-run-region>"
export PROJECT_ID="<gcp-project-id>"
export IMAGE_TAG="<immutable-image-tag>"
export IMAGE="$REGION-docker.pkg.dev/$PROJECT_ID/colosseum/account-service:$IMAGE_TAG"

# Run once per project/region.
gcloud artifacts repositories create colosseum \
  --repository-format=docker \
  --location "$REGION"

gcloud builds submit --tag "$IMAGE" .
```

Run the direct-Neon migration job and wait for it to succeed before creating or updating
the service. If the job fails, stop; do not send traffic to that image.

```bash
gcloud run jobs deploy colosseum-account-migrate \
  --image "$IMAGE" \
  --region "$REGION" \
  --command /colosseum-account-migrate \
  --max-retries 0 \
  --set-env-vars MIGRATION_TIMEOUT_SECONDS=300 \
  --set-secrets MIGRATION_DATABASE_URL=migration-database-url:latest

gcloud run jobs execute colosseum-account-migrate \
  --region "$REGION" \
  --wait
```

Deploy the same image as the public service only after the migration job succeeds:

```bash
gcloud run deploy colosseum-account-service \
  --image "$IMAGE" \
  --region "$REGION" \
  --allow-unauthenticated \
  --port 8080 \
  --min-instances 0 \
  --max-instances 3 \
  --concurrency 16 \
  --set-env-vars COLOSSEUM_ACCOUNT_ENV=production,HTTP_ADDR=:8080,DATABASE_MAX_CONNECTIONS=8,AVATAR_STORAGE=disabled \
  --set-secrets DATABASE_URL=database-url:latest,RECOVERY_HMAC_KEY=recovery-hmac-key:latest,ABUSE_HMAC_KEY=abuse-hmac-key:latest,SESSION_WRAP_KEY=session-wrap-key:latest,SYNC_DATA_KEY=sync-data-key:latest
```

Notes on the flags:

- `--allow-unauthenticated` is required. Cloud Run defaults to requiring IAM auth on the
  invoker, which would reject every real client request — this service's own session
  tokens are the auth layer, not Cloud Run's.
- `--region` should match the Neon project's region from step 1.
- `--min-instances 0`, `--max-instances 3`, and `--concurrency 16` are deliberate rollout
  limits. The service's pgx pool defaults to `MaxConns=8` and `MinConns=0`, so the initial
  Cloud Run ceiling permits at most 24 runtime database connections across instances.
- `COLOSSEUM_ACCOUNT_ENV=production` makes the ordinary service skip substantive startup
  migrations. Only `/colosseum-account-migrate`, using the direct migration URL, runs them.
- Keep the migration job and service on the same immutable image tag. The migration job is
  the migration-before-traffic gate for every schema change.
- Grant the service identity access only to the five runtime secrets and grant the migration
  job identity access only to `migration-database-url`; do not put either URL or any key in
  the image, command line, or tracked files.

### 4. Readiness, domain, and TLS

The existing operational endpoints are sufficient; N-21 adds no second schema probe:

- `GET /healthz` is a liveness check and returns `200` without touching PostgreSQL.
- `GET /readyz` performs a bounded PostgreSQL ping and returns `200` only when the runtime
  can reach the database, otherwise `503` with a safe body.
- Schema readiness is established by the successful migration job before service traffic is
  enabled. `/readyz` deliberately does not replace that migration gate with a version probe.

After deployment, verify both endpoints from the Cloud Run URL before the soft launch:

```bash
curl --fail "https://<cloud-run-url>/healthz"
curl --fail "https://<cloud-run-url>/readyz"
```

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

### 7. Restore rehearsal

Perform this rehearsal against a disposable Neon branch or restored database, never by
overwriting the live production database:

1. Create a branch/restore from the current production backup and record the backup time.
2. Point a temporary copy of `MIGRATION_DATABASE_URL` at the restored direct endpoint and
   execute the migration job. Confirm it completes successfully and is idempotent on a
   second execution.
3. Start a temporary service against the restored branch with its pooled URL. Verify
   `/healthz`, `/readyz`, account sign-in, and a representative sync pull/push flow.
4. Compare the rehearsal results with the expected recovery point and recovery time, then
   record the timestamp, operator, and any gaps outside this repository.
5. Remove the disposable branch only after the results are recorded and the restore path is
   understood.

## Operating it

This is the project's first always-on service holding user credentials. Before it carries
real accounts:

- a Cloud Monitoring uptime check on `/healthz`, with an alert policy to email on failure;
- a GCP budget alert set well below what a real bill would look like, so a traffic spike or
  a misconfigured `--max-instances` shows up as a notification, not a surprise invoice;
- the Neon restore actually tested once (step 7), not merely enabled;
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
