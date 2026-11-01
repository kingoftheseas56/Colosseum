# Colosseum Account Service operations

This document describes the checked-in release and operations contract. Dated
read-only observations are distinguished from rollout instructions; they do not
qualify production configuration, backups, or unobserved infrastructure.

## Current evidence boundary

The desktop source currently falls back to:

```text
https://colosseum-account-service.onrender.com
```

That URL is a client fallback observed in source and during the account review.
The read-only provider/deployed-commit observation for the 2026-11-02
authentication investigation is recorded below. There is still no checked-in
provider ingress configuration, database/backup receipt, secret inventory, or
scheduler receipt. Any future HTTP observation for `/healthz` or `/readyz` must
be retained with its timestamp,
URL, image, and schema receipts before it can support an availability claim;
no health observation alone is treated as a deployment receipt. No rollout is
performed by these instructions.

`fly.toml` remains historical reference material. The observation below
identifies Render as the active account-service host on that date, without
establishing its ingress trust ranges. Use the selected provider's current
official documentation for its
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

   Production deployments using only built-in avatars may explicitly set
   `AVATAR_STORAGE=disabled` and omit the avatar bucket and endpoint. Otherwise,
   avatar storage is required; the bucket, endpoint, region, and provider
   credentials belong in secret or managed environment configuration. Missing
   or misspelled `AVATAR_STORAGE` values do not waive the storage requirement.
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

### Authentication source attribution

Read-only Render control-plane inspection on 2026-11-02 IST identified
`colosseum-account-service` in Singapore, rooted at `server/account-service`,
with commit `69e31a79b9a7e70017ce1fca5ed48fe0982b9176` live and automatic
deployment from `master` enabled. That deployed revision uses the socket peer
for non-Fly authentication limits. Render documents that its load balancers
hide the client socket address and forward `X-Forwarded-For` instead:
https://render.com/articles/how-render-handles-ddos-attacks
https://render.com/docs/web-services

The source repair adds optional `TRUSTED_PROXY_CIDRS` (comma-separated explicit
CIDRs). Empty means direct socket attribution; no platform environment variable
or forwarded header grants trust. Only a socket peer in an explicitly configured
network can supply `X-Forwarded-For`. Walk the complete chain from the right,
removing only configured trusted hops, and use the first untrusted public
address. IPv4 remains per-address; IPv6 remains grouped at /64. Malformed peers,
missing/malformed trusted-proxy headers, all-trusted chains, and ambiguous
private hops return temporary `source_unavailable` (503) before consuming any
authentication budget. Headers on untrusted public connections are ignored.
With a proxy policy enabled, unexpected private/loopback peers also fail closed.
The former ambient `FLY_APP_NAME` trust shortcut is removed; Fly deployments
must explicitly configure and verify their forwarding boundary too.

Before a separately authorized rollout, obtain the actual ingress socket and
forwarded-chain contract from provider-controlled evidence, verify each trusted
hop and direct/private-network reachability, and pin only those proxy CIDRs.
Do not guess a private subnet, use a public catch-all, assume a hop count,
or copy Render outbound ranges as ingress trust. The read-only investigation
did not obtain those ingress ranges or a tester-correlated 429 bucket. Log
retrieval initially returned 503 and subsequent filtered queries returned no
matching records; neither result proves that no 429 occurred. Accordingly no
Render-specific ranges are baked into this repair and no production config,
rate rows, credentials, or deployment were changed. Merging/pushing this fix to
`master` would trigger a rollout and requires that separate authorization.

Keep the previous image and proxy configuration for rollback; both must match
the verified topology. Do not clear rate tables or rotate the abuse HMAC key to
reset limits. Existing attempt, username, success, and global limits are unchanged.
Anonymous limiter logs now include only scope and rounded-up retry seconds,
allowing operators to distinguish the registration breaker from source limits.
Public 429 bodies do not disclose the internal bucket.

For regression tests use an isolated PostgreSQL database with an `_test` suffix:
`TEST_DATABASE_URL=<dedicated local test URL> go test -p 1 ./... -count=1`.
Packages reset that database's schema, so serialize them with `-p 1` and never
point the test URL at live data. The optional native
`localServiceRegistrationAndLogin` test uses
`COLOSSEUM_ACCOUNT_429_TEST_SERVICE=http://127.0.0.1:<local-port>` and a separate
disposable database-backed service. It creates a disposable account and signs
in through the real native transport/controller with in-memory credential
storage; it must never target production.

Each HTTP response carries a generated `X-Request-ID`. Structured request logs
contain only a fixed operation label, status, latency in milliseconds, request
ID, and a stable error class. They never include bearer tokens, passwords,
account payloads, raw PostgreSQL errors, connection strings, object keys, or
provider credentials. The API error body remains the stable user-safe error
contract.

Keep provider logs, migration-job status, `/healthz` and `/readyz` checks, and
database backup/restore evidence together for the exact image and schema receipt
under review. None of those receipts are present in this source tree today.
