# Account lifecycle service contract

This document describes the server seams authored for the account lifecycle
slice. Native account-controller and privacy-page wiring remains a later
integration step.

## Canonical export

`GET /v1/account/export` is an authenticated, bounded export projection. The
first request creates a `colosseum.account.export` schema version `1` snapshot;
subsequent requests use the opaque `cursor` returned in the page. Snapshots
expire after 15 minutes and repeated first-page requests reuse the account's
active snapshot during that window. Pages are capped at 100 items.

The snapshot contains account metadata, validated current winners from the
portable sync categories, and validated immutable Activity facts. It does not
contain passwords, recovery material, access or refresh tokens, raw journal
history, ciphertext, local paths, or source transport data. The high-water mark is the maximum committed account sequence
across the sync journal and Activity facts, including delete tombstones; a
tombstone is used for the high-water mark and is not emitted as a current item.

Rows are validated and canonicalized before materialization. Any undecryptable,
invalid, or unsupported account-owned row fails the export with an incomplete
result. A snapshot is bounded to 10,000 items and 64 MiB of encrypted item
materialization. Export snapshots and items are account-owned and cascade with
account deletion. Expired snapshots are removed by bounded maintenance passes;
large child sets are deleted in bounded item batches before the parent row is
removed. The maintenance command passes its `BatchSize` into the lifecycle
prune seam; each pass bounds expired parent snapshots, child items for the
selected snapshot, and deletion receipts independently.

## Account deletion and response-loss retry

`DELETE /v1/account` requires an active authenticated session, the current NFC-
normalized password, a UUID `request_id`, and a client-generated 32-byte
base64url `retry_capability`. The capability is accepted in the request body,
never in a URL or log. The service stores only its SHA-256 verifier in a
30-day receipt with the request id and deleted account id; it retains no
password, session, recovery, or capability secret.

`POST /v1/account/deletion/retry` accepts the same opaque request id and
capability without authentication after ordinary sessions have cascaded away.
Unknown, malformed, expired, or cross-account values return one generic result
class and never disclose whether an account existed. A committed deletion
returns completion metadata only. The receipt's account id intentionally has
no foreign key so the receipt survives the account cascade for its bounded
retention period; maintenance prunes expired receipts in row-limited passes.

Deletion acquires the account sync advisory lock before the account row lock,
rechecks the current password hash and authenticated session after waiting,
creates the deletion receipt and avatar cleanup intent in the same transaction,
retains the existing permanent username reservation, and then cascades account
data. Local cached files, provider backups, and production data erasure are
outside this service slice and are not claimed by these API seams.

## Avatar cleanup

Avatar replacement and account deletion persist cleanup intent before removing
the account's uploaded-object reference. Object deletion checks all live
account references first; an object-store failure or an unavailable reference
check falls back to the durable queue. The maintenance worker retries queued
objects and discards a queue row when another account still references it.
