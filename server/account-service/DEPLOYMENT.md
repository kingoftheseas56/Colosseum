# Colosseum Account Service operations

This document describes the checked-in release and operations contract. It does
not claim that a provider account, database, bucket, scheduler, or deployed
image exists.

## Current evidence boundary

The desktop source currently falls back to:

```text
https://colosseum-account-service.onrender.com
```

That URL is a client fallback observed in source and during the account review.
The repository contains no provider control-plane configuration, deployed image
SHA, database/backup receipt, secret inventory, or scheduler receipt. Any future
HTTP observation for `/healthz` or `/readyz` must be retained with its timestamp,
URL, image, and schema receipts before it can support an availability claim;
none is treated as a current deployment receipt here. No rollout is performed
by these instructions.

`fly.toml` remains historical reference material. This repository does not name
Cloud Run, Render, Fly, Neon, or another provider as a proven active hosting
target. Use the selected provider's current official documentation for its
container, secret, database, TLS, job, and monitoring setup.

## Release sequence

1. Provision PostgreSQL and, when uploaded avatars are enabled, an S3-compatible
   private bucket. Create a runtime database role without `CREATE`, `ALTER`, or
   `DROP` privileges and a separate migration role with the minimum DDL rights
   required for the forward-only migrations.
2. Store `DATABASE_URL` as the runtime connection string. Use the provider's
   pooled endpoint when the provider supplies one. Store the provider's direct
   endpoint separately as `MIGRATION_DATABASE_URL`; never pass that privileged
   value to the long-running service.
3. Store all service and storage credentials in provider secret storage. Do not
   put them in this repository, image layers, command logs, or diagnostic
   attributes.
4. Run the explicit migration job and wait for it to finish:

   ```text
   MIGRATION_DATABASE_URL=<direct privileged URL> /colosseum-account-migrate
   ```

   The command opens one direct PostgreSQL connection, holds the transactional
   advisory lock, and records canonical SHA-256 checksums. Runtime startup does
   not run DDL. Existing name-only migration rows receive a documented legacy
   baseline only after the physical schema contract passes; that baseline cannot
   prove the old SQL text.
5. Start the service with the runtime role and runtime secrets only:

   ```text
   COLOSSEUM_ACCOUNT_ENV=production
   DATABASE_URL=<pooled runtime URL>
   RECOVERY_HMAC_KEY=<provider secret>
   ABUSE_HMAC_KEY=<provider secret>
   SESSION_WRAP_KEY=<provider secret>
   SYNC_DATA_KEY=<provider secret>
   BUCKET_NAME=<private avatar bucket>
   AWS_ENDPOINT_URL_S3=<provider S3 endpoint>
   AWS_REGION=<provider region>
   /colosseum-account-service
   ```

   Production avatar storage is mandatory in the service configuration. The
   bucket, endpoint, region, and provider credentials belong in secret or
   managed environment configuration; built-in avatars do not remove this
   startup contract.
6. Bake or override the client endpoint only after the selected provider has
   supplied a deployment receipt and the service has passed the acceptance
   journey against that exact URL. The source fallback above remains a separate
   fact until then.

## Schema and readiness contract

`/healthz` reports process health and does not query PostgreSQL. `/readyz` uses a
bounded database context and checks both connectivity and the exact supported
schema: every embedded migration must be present, no future migration may be
recorded, every checksum must match, and the required columns, types, validated
keys, foreign-key delete actions, and checks must exist. A missing, future,
drifted, or still-baseline-only schema returns HTTP 503.

Monitor both endpoints with the provider's current HTTP monitor and a 20-second
client timeout. Alert on readiness failure. The 20-second monitor timeout is an
operator setting; the service keeps a shorter internal database deadline so a
slow provider cannot tie up request workers.

Schema changes are forward-only and use expand/contract. Run the migration job
before changing the service image. Keep the advisory lock and inspect the job's
exit status. A failed transaction must leave no partial migration receipt.
Rollback means deploying a schema-compatible image or completing a forward
compatibility migration; the service must not perform automatic down-migrations.

## Scale-to-zero maintenance

The service keeps a small in-process cleanup ticker as a best-effort fallback;
it calls the same bounded maintenance pass as the standalone command. It is
not a durable schedule when instances scale to zero. Configure the chosen
provider's external job/scheduler to invoke the bounded maintenance binary:

```text
DATABASE_URL=<runtime URL> /colosseum-account-maintenance \
  --timeout=20s --avatar-limit=25 --batch-size=100
```

`MAINTENANCE_DATABASE_URL` can point at a provider-approved maintenance endpoint
when it should differ from `DATABASE_URL`. The command requires no session,
recovery, abuse, or sync encryption secrets. One invocation attempts due avatar
cleanup, expired authentication-rate event pruning, security challenge/retry
maintenance, up to 100 rows per table of old sync-version pruning, and bounded
export-snapshot item/parent plus deletion-receipt pruning under the supplied
deadline after the same read-only schema compatibility gate used by the service.
The `--batch-size` value bounds each retention pass, including lifecycle child
rows before their snapshot parent. A non-zero exit is an actionable scheduler
failure; this repository does not pretend that a scheduler is already
configured.

Activity retention, compaction, and native Activity scale remain a later
protocol/native slice. The maintenance command does not delete immutable
Activity facts or claim to solve that work.

## Operational diagnostics

Each HTTP response carries a generated `X-Request-ID`. Structured request logs
contain only a fixed operation label, status, latency in milliseconds, request
ID, and a stable error class. They never include bearer tokens, passwords,
account payloads, raw PostgreSQL errors, connection strings, object keys, or
provider credentials. The API error body remains the stable user-safe error
contract.

Keep provider logs, migration-job status, `/healthz` and `/readyz` checks, and
database backup/restore evidence together for the exact image and schema receipt
under review. None of those receipts are present in this source tree today.
