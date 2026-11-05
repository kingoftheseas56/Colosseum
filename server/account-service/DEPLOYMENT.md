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

Each HTTP response carries a generated `X-Request-ID`. Structured request logs
contain only a fixed operation label, status, latency in milliseconds, request
ID, and a stable error class. They never include bearer tokens, passwords,
account payloads, raw PostgreSQL errors, connection strings, object keys, or
provider credentials. The API error body remains the stable user-safe error
contract.

Keep provider logs, migration-job status, `/healthz` and `/readyz` checks, and
database backup/restore evidence together for the exact image and schema receipt
under review. None of those receipts are present in this source tree today.

## Anonymous Collections web API preflight

The static website calls the account-service origin directly. Enable browser
reads only with exact origins in the runtime setting:

```text
COLLECTIONS_ALLOWED_WEB_ORIGINS=https://getcolosseum.world,https://www.getcolosseum.world
```

The default is an empty allowlist. Entries must be HTTP(S) scheme/host/port
origins with no path (including a trailing slash), credentials, query, fragment,
or wildcard. The anonymous shared-collection GET and passphrase-open POST APIs
support CORS with Content-Type only; authenticated APIs do not. CORS never
grants credentialed access. Successful unprotected GETs are cacheable for
60 seconds and support ETag revalidation. Other responses use no-store. The
service's own viewer and native clients continue to work without Origin.

`TRUSTED_PROXY_CIDRS` is an optional comma-separated list of operator-verified
IPv4/IPv6 proxy networks. It defaults to empty and rejects catch-all networks.
For anonymous collection abuse limits, forwarded chains are used only when the
socket peer is trusted. Parsing is bounded to 4096 bytes and 32 addresses;
trusted hops are removed from the right, and the first untrusted public address
identifies the client. IPv6 addresses share their /64 bucket. A missing,
malformed or ambiguous trusted chain returns `503 source_unavailable` before
opening a collection. Untrusted public peers cannot assert identity through
forwarded headers. Environment labels and Fly/Cloudflare client-IP headers do
not establish trust.

`COLLECTIONS_EDGE_SECRET` is the recommended production path. It sidesteps the
host's undocumented ingress proxy by trusting only our own Cloudflare zone.
When set (at least 32 characters, no surrounding whitespace), anonymous
collection opens ignore the socket peer, `X-Forwarded-For` and
`TRUSTED_PROXY_CIDRS` entirely. A request is accepted only when it carries
exactly one `X-Colosseum-Edge` header equal to the secret (compared in constant
time) and exactly one public `CF-Connecting-IP` address. Cloudflare overwrites
`CF-Connecting-IP` with the address that connected to it. Every other request
returns `503 source_unavailable`, including direct requests to the host's own
hostname. A `CF-Connecting-IP` inside Cloudflare's published ranges also
returns 503, because it means a second Cloudflare hop replaced the visitor; the
list is embedded in `collection_edge.go` and must be refreshed if Cloudflare
changes it. Account login/registration limits are not affected.

Enablement order (each production step needs Hemanth's approval):
1. Create a proxied `api.getcolosseum.world` record on our Cloudflare zone
   pointing at the account-service origin.
2. Add a Cloudflare Transform Rule (Modify Request Header, Set static) for that
   hostname: `X-Colosseum-Edge` = the secret. Store the secret in the host's
   secret settings and in the password manager with the other service keys.
3. Ensure native clients and the website open shared collections through
   `https://api.getcolosseum.world`, not the host's hostname. Direct opens are
   refused once the secret is set. Legacy viewer links then redirect to
   `https://getcolosseum.world/c/{share_id}`. For isolated native runs, set
   `COLOSSEUM_COLLECTIONS_API_URL` for anonymous opens alongside
   `COLOSSEUM_ACCOUNT_SERVICE_URL` for login, sync and publication management.
4. Rehearse on a non-production service first. A forged `CF-Connecting-IP`
   sent straight to the host must return 503. Opens through the edge from two
   different networks must produce two different rate-limit buckets. When the
   origin itself sits behind another Cloudflare zone (Render), confirm that the
   visitor address, not a Cloudflare address, arrives.

Without the secret, the trusted-proxy rules below still apply.

Before production enablement, the operator must record the actual public and
private ingress topology, exact trusted CIDRs, and how each hop overwrites or
appends X-Forwarded-For. Include direct/private bypass paths and topology-change
behavior in that verification. The static website adds no proxy hop. Missing
verified ingress information blocks rollout; do not substitute broad CIDRs or
unconditional forwarding-header trust.

Use the separately authorized deployment checkpoint to obtain an isolated clone
of the verified production baseline. Record its deployed migration lineage,
checksums and physical-schema metadata before applying migration 0011 with the
exact migration binary built from the reviewed service commit. Compare all
previous receipts, preexisting schema metadata and representative state after
migration; rerun the command to prove idempotence. A synthetic local 0010
fixture or an empty-database test is not that production-clone rehearsal. Retain
backup/rollback and runtime-role schema-gate receipts, then deploy only through
the approved release sequence above. No production migration or setting change
is authorized merely by this document.
