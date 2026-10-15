# Mock Colosseum Account Service (test infrastructure only)

`server.mjs` is a dependency-free, in-memory Node.js mock of the Colosseum
account service. It exists so a **real desktop Colosseum instance** can be
pointed at it for runtime exercise of the account/session/device/approval
surface without a live Go service.

**This is NOT the production account service.** No TLS, no persistence
(state lives in a single process and resets on restart), no rate limiting,
no argon2 (passwords are hashed with Node's built-in `scrypt` purely so
plaintext never sits in memory longer than needed — this is a convenience,
not a security posture). Do not point a real account at it and do not use
it as a reference for production behavior beyond the response shapes it
mirrors.

## Ground truth this mock satisfies

Read in this order if you need to change it:

1. `server/account-service/internal/httpserver/account_handlers.go` - the
   canonical production account/profile response shape, status codes, and
   endpoint-specific response fields.
2. `native/account/AccountClient.cpp` / `.h` - the exact routes, HTTP
   methods, and request payload field names the desktop client sends.
3. `native/account/AccountController.cpp` - how canonical response fields
   are translated into safe desktop state, plus the error codes the
   controller treats specially (`session_invalid`, `session_revoked`,
   `challenge_expired`, `challenge_denied`, `challenge_invalid`, etc.).
4. `native/account/AccountHttpTransport.cpp` - the error envelope shape
   (`{"error":{"code":"...","message":"..."}}` for any status >= 400) and
   the fact that redirects are never followed.
5. `qml/account/*.qml` - downstream UI behavior for the controller's safe
   properties and raw device/approval arrays.

## Run

```
node tests/mock-account-service/server.mjs --port 18080
```

Then point a real Colosseum instance at it:

```
COLOSSEUM_ACCOUNT_SERVICE_URL=http://127.0.0.1:18080
```

One line is logged per request to stdout (`METHOD path -> status Nms`).

## Self-test

```
node tests/mock-account-service/server.mjs --selftest
```

Starts its own instance on an ephemeral port, runs the full happy path
against itself over `fetch`, prints a pass/fail line per step, and exits
`0` on success / `1` on any failure. No external server needs to be
running first.

## Endpoints

### Public (no bearer token)

| Route | Behavior |
| --- | --- |
| `GET /healthz` | `200 {"status":"ok"}` |
| `POST /v1/accounts` | Create account + device + session. `201 {session, recovery_key}`. `409 username_unavailable` if taken, `400 invalid_username`/`invalid_password` on validation failure. |
| `POST /v1/sessions` | Sign in. Known device or protection disabled → `200 {status:"signed_in", session}`. Unknown device + `protect_new_device_signins` on → `202 {status:"approval_required", challenge_token, challenge_expires_at}`. Bad credentials → `401 invalid_credentials`. |
| `POST /v1/sessions/refresh` | Rotates access + refresh token pair. `200 {session}`. Unknown token → `401 session_invalid`. Revoked token → `401 session_revoked`. |
| `POST /v1/sessions/revoke-refresh` | Best-effort revoke of a refresh token (idempotent no-op on unknown token). Always `204`. |
| `POST /v1/password/recover` | Self-service reset via recovery key. `200 {recovery_key}` (rotated) on success. Bad key/account → `401 invalid_credentials` (matches the Go mapping: `ErrRecoveryKeyInvalid` shares the `invalid_credentials` code, it does **not** get its own code). Revokes all sessions on success. |
| `POST /v1/password/trusted-recovery` | Starts a recovery challenge another signed-in device must approve. `202 {status:"pending", challenge_token, challenge_expires_at}`. |
| `POST /v1/password/trusted-recovery/poll` | `200 {status:"pending"}` until decided; `200 {status:"recovered", recovery_key}` on approval; `403 challenge_denied` / `410 challenge_expired` otherwise. |
| `POST /v1/challenges/device/poll` | Same pending/approved/denied/expired shape as trusted-recovery poll, but approval yields `{status:"signed_in", session}`. |
| `POST /v1/challenges/device/recovery-key` | Skip approval by presenting the recovery key directly. `200 {session, recovery_key}` (rotated) on match, `401 invalid_credentials` otherwise. |

### Protected (`Authorization: Bearer <access_token>`)

| Route | Behavior |
| --- | --- |
| `DELETE /v1/sessions/current` | Revokes this device's session only. `204`. |
| `POST /v1/sessions/logout-all` | Revokes every session for the account. `204`. |
| `POST /v1/password/change` | Validates current password, 8–128 code points on the new one. `204` on success, `401 invalid_credentials` / `400 invalid_password` otherwise. |
| `POST /v1/recovery-key/replace` | Validates current password, rotates recovery key. `200 {recovery_key}`. |
| `GET /v1/profile` | `200 {id, username, protect_new_device_signins, builtin_avatar_id}`. |
| `PATCH /v1/profile/username` | Rename with the same validation as create. `200` account body, `409 username_unavailable` on collision. |
| `PUT /v1/profile/avatar/builtin` | Request body uses `avatar_id`; response uses canonical `builtin_avatar_id`. |
| `GET /v1/devices` | `200 {devices:[{id, install_id, label, platform, trusted, last_seen_at}, ...]}`. |
| `DELETE /v1/devices/{deviceID}` | Revokes that device's tokens and removes it. `204` whether it's the current device or a remote one — the native client (not the server) decides what "revoked my own device" means locally. `404 device_not_found` if unknown. |
| `PUT /v1/security/new-device-protection` | `200` account body with the new `protect_new_device_signins`. |
| `GET /v1/approvals?wait_seconds=0..25` | `200 {approvals:[{id, challenge_id, kind, device_label, platform, expires_at}, ...]}`. Honors `wait_seconds` as a short poll instead of returning empty immediately. |
| `POST /v1/approvals/{kind}/{challengeID}` | Body `{"decision":"approve"|"deny"}`. `204` on success, `401 challenge_invalid` / `410 challenge_expired` otherwise. |

### Mock seed control (`/_mock/`, non-production, no auth)

| Route | Behavior |
| --- | --- |
| `POST /_mock/seed-devices` | Body `{account_id?, username?, count?}` (defaults to the most recently active account, count 3). Adds N fake trusted devices with randomized labels/platforms/last-seen times. `200 {account_id, devices:[...]}`. |
| `POST /_mock/seed-approval` | Body `{account_id?, username?, kind?, device_label?, platform?}` (`kind` defaults to `"device"`). Injects one pending approval (and, for `kind:"device"`, an untrusted device row) so the Security page's approval list and decide flow have something real to exercise. `200 {account_id, approval}`. |
| `POST /_mock/reset` | Wipes all in-memory state back to empty. `200 {status:"reset"}`. |

## Self-test results (last run)

26/26 steps passed: reset; create account; duplicate-username rejection;
wrong-password sign-in rejection; refresh-token rotation; profile read;
username rename; builtin avatar set; device list; seed 4 devices; device
list reflects seed; revoke a remote device; device list reflects revoke;
new-device-protection toggle; seed a pending approval; approvals list
shows it; approve it; approvals list empties; full second-device sign-in
→ approval-required → poll pending → list → decide approve → poll
signed-in; password change; wrong-current-password rejection; recovery
key replace/rotate; unauthenticated request → `session_invalid`; logout
current → subsequent request on that token → `session_revoked`; sign back
in and logout-all → subsequent request → `session_revoked`.

## Contract ambiguities resolved

- **Avatar/profile response field name.** The production Go
  `accountResponse` serializes `builtin_avatar_id`, so that is the canonical
  wire field emitted by the mock on every account-shaped response. The
  desktop controller maps it into the existing `avatarId` QML property and
  temporarily accepts legacy `avatar_id` on read for compatibility. The
  builtin-avatar request body still uses `avatar_id`, matching the production
  Go request contract; the request and response field names are intentionally
  different.
- **Approval item id field (resolved 2026-08-20, service-side).** `AccountSecurityPage.qml`
  (`approvalChallengeId`, lines ~66–71) reads `challenge_id` first, falling
  back to `challengeId`. The Go reference's `listApprovals` handler used to
  serialize the challenge as `id` only (`account_handlers.go` lines
  ~480–489), with no `challenge_id` key — against the real service the QML
  reader would derive an empty challenge id and approval decisions could
  not be submitted. Hemanth ratified the service-side fix (the client
  shipped publicly, the service had not deployed): the preserved Go
  service now emits both `id` and `challenge_id` (same value) from
  `listApprovals`, additively, alongside the pre-existing `id`. The mock's
  dual-emission of `id`/`challenge_id` was already correct against the QML
  contract; it now also matches the Go service instead of merely masking
  the drift. `AccountController.cpp`'s `ListApprovals` case (line ~1181)
  still passes the array through to QML untouched and does not
  disambiguate the item shape itself.
- **Revoking the current device.** The task brief asked to check "whatever
  shape the real service uses" for self-revoke via `DELETE /v1/devices/{id}`.
  Per `account_handlers.go`'s `revokeDevice` handler, the endpoint is
  unconditionally `204 No Content` regardless of whether the target is the
  caller's own device. `AccountController::handleCompleted`'s `RevokeDevice`
  case (lines ~1151–1175) confirms this: it compares the revoked device id
  against `m_deviceId` **client-side** after a plain success reply and
  only then calls `finishLocalSignOut(true)`. No special server response
  shape exists to resolve — the mock intentionally does not special-case
  self-revoke.
- **Recovery-key failures share `invalid_credentials`.** `api.go`'s
  `writeServiceError` maps both `ErrInvalidCredentials` and
  `ErrRecoveryKeyInvalid` to the same `401 invalid_credentials` response,
  not a distinct `recovery_key_invalid` code. `AccountController::categoryForReply`
  has no separate bucket for a recovery-key-specific code either, so the
  mock follows the Go mapping exactly for `/v1/password/recover` and
  `/v1/challenges/device/recovery-key`.
