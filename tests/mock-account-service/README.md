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

1. `native/account/AccountClient.cpp` / `.h` — the exact routes, HTTP
   methods, and request payload field names the native client sends. This
   is the binding contract.
2. `native/account/AccountController.cpp` — which response fields the
   controller actually reads back out of each reply body, and which error
   codes it treats specially (`session_invalid`, `session_revoked`,
   `challenge_expired`, `challenge_denied`, `challenge_invalid`, etc.).
3. `native/account/AccountHttpTransport.cpp` — the error envelope shape
   (`{"error":{"code":"...","message":"..."}}` for any status >= 400) and
   the fact that redirects are never followed.
4. `qml/account/AccountSecurityPage.qml`, `AccountDevicesPage.qml`,
   `AccountProfilePage.qml` — the exact JSON field names read on the far
   side of the controller's raw pass-through data (device list, approvals
   list, avatar id).
5. The preserved Go reference service under
   `C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\02-profile-account-centre\cpp\reference-account-bundle-8c\bundle-8c-colosseum-account-cumulative\service\internal\{httpserver,account}\`
   — used for status codes, error-code-per-failure mapping, and any shape
   the C++/QML side doesn't pin down (device list fields, approval list
   fields, password/username validation rules).

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
| `GET /v1/profile` | `200 {id, username, protect_new_device_signins, avatar_id}`. |
| `PATCH /v1/profile/username` | Rename with the same validation as create. `200` account body, `409 username_unavailable` on collision. |
| `PUT /v1/profile/avatar/builtin` | `200` account body with the new `avatar_id`. |
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

- **Avatar/profile field name.** `AccountController.cpp` (e.g. lines
  ~1063–1067 in the `GetProfile`/`SetNewDeviceProtection` case, and the
  `RenameUsername`/`SetBuiltinAvatar` cases) reads `reply.body.value("avatar_id")`
  and writes it straight into `m_avatarId`. The preserved Go reference's
  `accountResponse` struct (`account_handlers.go`) instead serializes
  `builtin_avatar_id` and `avatar_url` — no `avatar_id` key at all. Since
  the task's binding contract is the C++ client (it decides what the mock
  must satisfy), and `qml/account/AccountProfilePage.qml` reads
  `controller.avatarId` (populated only from that `avatar_id` key), the
  mock emits `avatar_id` on every account-shaped response (`/v1/profile`,
  rename, set-avatar, set-protection). The Go-only fields were dropped as
  stale/superseded rather than mirrored.
- **Approval item id field.** `AccountSecurityPage.qml` (`approvalChallengeId`,
  lines ~66–71) reads `challenge_id` first, falling back to `challengeId`.
  The Go reference's `listApprovals` handler instead serializes the
  challenge as `id` (`account_handlers.go` lines ~480–489), with no
  `challenge_id` key. `AccountController.cpp`'s `ListApprovals` case
  (line ~1181) only cares that the top-level key is `"approvals"` — it
  passes the array through to QML untouched, so it doesn't disambiguate
  the item shape itself. Resolved in favor of the QML reader: each
  approval item carries both `id` and `challenge_id` set to the same
  value, so it satisfies the QML contract directly while staying
  recognizable against the Go shape.
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
