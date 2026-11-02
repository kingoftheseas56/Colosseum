# BR-BETA-STATE — Beta adds profile-safe Collections and Manga language/resume state

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Protects newer persistent user collections, language selections and read progress from broad Alpha file replacement or destructive rollback.

## Alpha behavior
No NamedCollections owner/adoption bytes, per-series manga language or numbered cross-translation cache.

## Beta behavior
ProfileStoreRuntime binds one NamedCollections owner; canonical identity validates exact reopen references; adoption protects opaque local bytes and digest-authenticated rollback; per-series language and manga numbered resumes added.

## Actual owner/caller
Common Android native graph CMake387, native main1973 CollectionsIdentity/ProfileScope; profile activation owns runtime; QML adapters use exact identity/reopen owner.

## Donor evidence
native/account/ProfileStoreRuntime.cpp:506-519; native/engine/ColosseumTitleIdentityRegistry.cpp:70; ProgressStore.h:885

## Beta evidence
native/NamedCollectionsStore.cpp:515-630; account/ProfileStoreRuntime.cpp:519,571,653,985; ProfileAdoption.cpp:679-704; ColosseumTitleIdentityRegistry.cpp:73-347; ProfilePreferencesStore.cpp:673-731

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
Preserve Beta state ownership/schema/adoption and collection identity; Alpha dirty Manga library is separate hash-labelled worlds packet.

## Existing tests
NamedCollections native store/adoption/profile fixtures; manga language/profile/reader resume tests from shared test inventory.

## Required host checks
N/A for preservation/exclusion; unresolved behavior needs a focused current-owner reproduction before change.

## Local device acceptance (pending)
No new device pass claimed. Applicable behavior remains subject to the independent device matrix.

## Recovery
N/A: no product change proposed. Retain app data and existing same-package/signature update path.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
Account/provider/live device behavior not newly run; Alpha dirty code not proven to be shipped APK.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
