# Account System Hardening Design

**Date:** 2026-08-28

**Status:** Approved audit-derived design for implementation planning.

## Objective

Harden Colosseum's existing account subsystem before a production account service is deployed. This work does not deploy the backend. It fixes correctness, security, resilience, recovery UX, and regression coverage defects found by a hostile audit of current `origin/master`.

## Ground truth

The account happy path works against `tests/mock-account-service/server.mjs`, but the mock currently mirrors some client assumptions rather than the real Go service. Therefore a green mock journey is not sufficient proof of production compatibility.

The production account implementation spans:

- desktop transport/client/controller: `native/account/AccountTransport.h`, `AccountHttpTransport.*`, `AccountClient.*`, `AccountController.*`
- desktop UI: `qml/account/*.qml`
- production service: `server/account-service/internal/{account,httpserver}`
- native Qt tests: `tests/auto/account*`
- runtime mock: `tests/mock-account-service`
- Lanista runtime scenarios: `tests/lanista_scenarios`

## Defects to close

1. **Avatar response contract drift.** The desktop controller reads `avatar_id`; the Go service serializes `builtin_avatar_id`. The mock emits `avatar_id`, masking the mismatch.
2. **Offline account state is presented as signed out.** `TopBar.qml`, `AccountFlyout.qml`, and `AccountCenter.qml` only treat `mode === "signedIn"` as account-present. Native `Mode::Offline` still has a remembered account and supports logout, but the UI shows "Not signed in" and a dead Sign in action.
3. **Username enumeration timing leak.** Unknown usernames return before Argon2 verification; known usernames with a wrong password pay the password verification cost.
4. **Desktop account requests have no bounded transfer timeout.** A stalled server can leave create/sign-in or other operations busy indefinitely.
5. **Password policy drift.** Create/change/server accept 8-128 Unicode code points; `AccountRecovery.qml` requires 15-128.
6. **Malformed approval payloads fail open as an empty list.** `AccountController` validates `devices` as an array but calls `.toArray()` on `approvals` without a type check.
7. **First recovery key can be dismissed without explicit save acknowledgement.** Manual replacement already requires "I saved it"; account creation does not.
8. **No committed real-QML create/sign-in Lanista regression.** Native controller coverage is broad, but there is no permanent runtime journey that clicks the production onboarding screens, and the mock can drift from the Go service.

## Contract decisions

### Account avatar field

`builtin_avatar_id` is the canonical service field because it is what the production Go API already serializes. The desktop controller will read `builtin_avatar_id` first and may temporarily accept legacy `avatar_id` as a compatibility fallback. The mock must emit the canonical field and its self-test must fail if the client-facing shape regresses.

### Account-present UI state

Define account presence separately from online mutability:

- `accountPresent = mode === "signedIn" || mode === "offline"`
- network-changing controls continue to require `mode === "signedIn"`
- `offline` top-bar/flyout/centre identity shows the remembered account; the flyout/centre offer Sign out, not Sign in
- `locked`, `signedOut`, and `localOnly` are not account-present for this UI purpose

### Request deadlines

Add an explicit per-request timeout to `AccountTransportRequest`:

- ordinary account requests: 15,000 ms
- approval long-poll requests: `(waitSeconds * 1000) + 10,000 ms`, bounded to 10,000-35,000 ms

`AccountHttpTransport` must convert transfer timeout to the existing network-error reply shape so the controller's normal retry/error state machine remains authoritative.

### Sign-in timing hardening

The service must perform one password-hash verification for syntactically valid username sign-in attempts regardless of whether the account exists. `NewService` will create one dummy Argon2 hash at startup; the unknown-account path verifies the presented password against that dummy hash before returning `ErrInvalidCredentials`. Rate limiting remains unchanged.

### Password policy

The desktop recovery UI will use the production service rule: 8-128 Unicode code points. The service remains authoritative for blocklist/context rejection.

### Approval payload validation

HTTP 2xx with a non-array `approvals` member is a protocol failure. Preserve the previously displayed approval list, set `ErrorCategory::Protocol`, emit the appropriate failure/error signal, and do not silently replace the UI with an empty list.

### Recovery-key acknowledgement

All one-time recovery-key presentations that invalidate or establish account recovery authority require an explicit saved acknowledgement before dismissal. Reuse the existing `I saved it` interaction instead of inventing a second pattern. Copying the key is helpful but is not itself proof that the user saved it.

## Global constraints

- Accounts remain optional; local-only mode must keep working.
- Do not deploy or bake a production account-service URL in this project.
- Do not weaken secure-store fail-closed behavior.
- Do not persist plaintext passwords, recovery keys, access tokens, or refresh tokens in QML-visible ordinary state.
- Preserve stale-request generation guards in `AccountController`.
- Preserve profile isolation and account A -> B -> A sealing guarantees.
- Production Go service, desktop client, and mock must agree on response field names after this work.
- Every defect closes RED -> GREEN with a targeted regression before implementation.
- Every implementation task ends in a focused commit; final integration runs the full account regression matrix plus `git diff --check`.

## Verification bar

The hardening program is complete only when:

- targeted Qt tests pass for transport/controller/profile behavior
- `go test ./...` passes under `server/account-service`
- `node tests/mock-account-service/server.mjs --selftest` passes
- committed Lanista create-account and fresh-profile sign-in journeys pass against the mock service
- the mock uses the same canonical account/profile fields as the Go service
- the account journey has negative checks for invalid credentials, malformed protocol data, and timeout behavior
- `git diff --check` is clean
