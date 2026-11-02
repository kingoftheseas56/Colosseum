# BR-STREMIO-COMMIT — Beta confirms durable secure linking before reporting sign-in success

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Failed/expired/disk-full/reentrant-profile sign-in can report a meaningful failure and preserve last valid credential; no false browser success.

## Alpha behavior
Browser callback acknowledges before complete identity/vault/journal/current-profile marker commit; previous reconnect credential can be lost on failure.

## Beta behavior
Delays browser success,20s validation timeout, current-profile/reentrancy checks and credential readback/rollback.

## Actual owner/caller
AccountRuntime623 createAccountCredentialStore; factory51-56 actual Android default PlatformAccountCredentialStore(Android secret backend), not legacy test injection class.

## Donor evidence
native/stremio/StremioSync.cpp:1959-1985,2030-2098; PlatformAccountCredentialStore.cpp:256-262; AccountRuntime.cpp:695

## Beta evidence
native/stremio/StremioSync.cpp:1984-2014,2026,2072-2143; PlatformAccountCredentialStore.cpp:259-277; AccountRuntime.cpp:695-712; ProfilePreferencesStore.cpp:1072-1086

## Evidence layer
committed-alpha-beta

## Dependencies
N/A: no immediate transfer proposed; preserve existing owner/availability boundary.

## Reuse/adaptation
preserve Beta

## Files and integration points
N/A: preservation/exclusion or unresolved decision; no implementation selected.

## Preserve
Keep current canonical state, saved preferences, security/lifecycle owners and approved desktop presentation.

## Bounded unit
N/A: no implementation before the stated evidence/decision gate.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Preserve newer transactional and lifetime guards; no wholesale credential/auth backport.

## Existing tests
tests/auto/stremio_sync/tst_stremio_sync.cpp; account credential persistence and profile runtime tests; browser/device flow unverified here.

## Required host checks
N/A for preservation/exclusion; unresolved behavior needs a focused current-owner reproduction before change.

## Local device acceptance (pending)
No new device pass claimed. Applicable behavior remains subject to the independent device matrix.

## Recovery
N/A: no product change proposed. Retain app data and existing same-package/signature update path.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
No additional source question recorded; runtime acceptance is separate.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
