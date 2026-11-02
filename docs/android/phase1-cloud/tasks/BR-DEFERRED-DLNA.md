# BR-DEFERRED-DLNA — Desktop-only optional Beta subsystem remains deferred

Class F; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
No Alpha Android capability is missing because these Beta-only files exist. Preserve the desktop subsystem while avoiding unsupported Android activation or claims about sharing acceptance.

## Alpha behavior
No tracked native/dlna subsystem corresponding to Beta optional server/projection/resources.

## Beta behavior
Adds optional desktop pupnp server plus Vault/audiobook snapshot projection and resources, gated out of Android.

## Actual owner/caller
Explicitly inactive on Android: optional DLNA target and main registration are within desktop/platform and COLOSSEUM_HAVE_DLNA gates. DLNA internal audit is explicitly deferred by current task.

## Donor evidence
N/A: native/dlna/* are Beta-only in reconciled committed inventory; Alpha has no corresponding subsystem to cite. This is absence evidence, not invented source lines.

## Beta evidence
native/CMakeLists.txt:954-997; native/main.cpp:1222-1283

## Evidence layer
committed-alpha-beta

## Dependencies
Desktop Vault/audiobook snapshot owners, optional pupnp dependency, COLOSSEUM_HAVE_DLNA and non-Android guards.

## Reuse/adaptation
intentionally excluded/deferred; no transfer

## Files and integration points
None for this audit; do not enable or redesign DLNA.

## Preserve
Leave Beta optional dependency, immutable snapshot boundary, desktop registration and resources intact.

## Bounded unit
N/A: explicitly deferred, no implementation authorized.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Explicit task defer; optional pupnp server and static resources are not active Android source path. Internal algorithm/icon audit excluded; retain Beta files untouched.

## Existing tests
N/A to a new Android adoption recommendation; internal DLNA test/algorithm audit is outside the deferred scope.

## Required host checks
No new check authorized/required by this audit; source gates already inspected. Separate authorized DLNA work would define focused tests.

## Local device acceptance (pending)
N/A: no Android DLNA route or new device sharing qualification claimed.

## Recovery
No product changes and no rollback action. Do not alter media/library data or deployment state for this deferred topic.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
DLNA algorithms/icons and operational sharing behavior remain unexamined; structural classification only.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
