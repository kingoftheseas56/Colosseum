# BR-DESKTOP-PRESERVE — Preserve desktop Vinyl/Vault improvements without importing them into Android scope

Class F; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Prevents an Android transfer from regressing desktop download-derived library state or newer Vinyl behavior. This finding does not claim those desktop features work on Android or certify their algorithms.

## Alpha behavior
Desktop Vault already projects downloaded media, but lacks Beta live synthetic-root refresh, improved loose-episode grouping and enrichment retry; older Vinyl APIs lack newer lyrics/radio/video/home-selection additions.

## Beta behavior
Improves desktop Vault download projection/refresh/grouping and stale-enrichment handling and extends desktop Vinyl recording/lyrics/radio/video/home state. Android still excludes those worlds/backends.

## Actual owner/caller
Explicitly excluded: native/CMakeLists desktop block and main !Q_OS_ANDROID guards own Vault/Vinyl registration; android.cmake filters their QML. VaultDoor/VaultHomeWidget are presentation exceptions, not backend activation. No active Android Alpha donor route established.

## Donor evidence
native/engine/VaultDownloadsRoot.cpp:55-70,231-245; VaultLibrary.cpp:1571-1580 setDownloadsRoot; VaultKit.cpp:372-411 episode parsing; native/main.cpp:1038-1077 cover enrichment; native/CMakeLists.txt:3174-3187 older Vinyl target; native/main.cpp:1778-1787 old Vinyl registration. New-only service files have N/A Alpha lines because absent from tracked tree.

## Beta evidence
native/CMakeLists.txt:867-997,1204-3359; native/main.cpp:928-1197,1662-1763,1865-1875; native/cmake/android.cmake:93-99

## Evidence layer
committed-alpha-beta

## Dependencies
Existing desktop Vault index/download owners, synthetic roots/enrichment generation; desktop Vinyl identity/profile/backend; platform build and staging gates.

## Reuse/adaptation
preserve Beta desktop organs; intentionally exclude Android transplant

## Files and integration points
No Android implementation or future-world design authorized. Preserve existing guarded desktop targets/registrations while editing common code.

## Preserve
Keep canonical download owner grouping, synthetic-root change signals, stale-write barriers, local identity and newer Vinyl profile state; do not replace desktop subsystem files with Alpha.

## Bounded unit
No implementation unit; preservation constraint only. Desktop-only Vinyl deeper algorithms received structural triage, not full audit.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Vault newer synthetic download-root/grouping/refresh and Vinyl recording/lyrics/radio/video/home selection are outside active Android app graph. No donor gap; API/source-hunk structural triage only for Vinyl, not deep desktop algorithm certification.

## Existing tests
Vault download-root and native projection tests plus Vinyl test estate are present; source/owner triage only, no executions.

## Required host checks
No additional audit run. Any later common-code change must retain desktop target gates and run focused affected Vault/Vinyl owner tests under separate implementation authority.

## Local device acceptance (pending)
N/A for this excluded Android route. No phone/tablet Vault/Vinyl acceptance or future feature design is inferred.

## Recovery
No change made. Future accepted common-code rollback must preserve canonical stores/download payloads and revert code without uninstall or profile cleanup.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
Deeper desktop Vinyl algorithms unexamined beyond API/change-hunk/owner triage; no desktop correctness certification.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
