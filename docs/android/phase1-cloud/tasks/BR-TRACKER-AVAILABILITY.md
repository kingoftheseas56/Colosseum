# BR-TRACKER-AVAILABILITY — Compiled AniList support does not establish Android tracker sign-in availability

Class D; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Do not count Beta-only AniList files as working Android sync, nor claim an Alpha donor Android tracker feature absent Beta. Shared platform limit needs separate explicit scoped product work if requested.

## Alpha behavior
Native existing tracker connections depend on WindowsTrackerCredentialVault; Android returns unavailable.

## Beta behavior
Adds AniList protocol/API/loopback/auth/sync/outbox but still vault-gated unavailable on Android. Canonical facts remain local authoritative.

## Actual owner/caller
ProfileStoreRuntime constructs router, SyncCenter connection availability first checks Windows vault; controllers require available vault.

## Donor evidence
native/trackers/TrackerCredentialVault.cpp:isAvailable; account/ProfileStoreRuntime.cpp:846-866

## Beta evidence
native/trackers/TrackerCredentialVault.cpp:244-250; account/ProfileStoreRuntime.cpp:911-931; AniListConnectionController.cpp:127-134

## Evidence layer
committed-alpha-beta

## Dependencies
N/A: no immediate transfer proposed; preserve existing owner/availability boundary.

## Reuse/adaptation
retain; future separate platform work

## Files and integration points
N/A: preservation/exclusion or unresolved decision; no implementation selected.

## Preserve
Keep current canonical state, saved preferences, security/lifecycle owners and approved desktop presentation.

## Bounded unit
N/A: no implementation before the stated evidence/decision gate.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Preserve Beta provider/canonical outbox additions; document readiness boundary, no Alpha backport.

## Existing tests
No test adoption proposed by this preservation/decision record; relevant packet names retained checks.

## Required host checks
N/A for preservation/exclusion; unresolved behavior needs a focused current-owner reproduction before change.

## Local device acceptance (pending)
No new device pass claimed. Applicable behavior remains subject to the independent device matrix.

## Recovery
N/A: no product change proposed. Retain app data and existing same-package/signature update path.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
No external tracker authentication/provider testing performed; account/Stremio credential path is separate and does have Android backend.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
