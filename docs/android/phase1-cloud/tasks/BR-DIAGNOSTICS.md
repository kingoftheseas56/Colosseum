# BR-DIAGNOSTICS — Diagnostic fixtures are not production Alpha functionality

Class G; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Avoids exposing fixture/file-trigger control in production or miscounting diagnostic removal as lost playback. Preserves a safer test mechanism and avoids dereferencing a UI target after a click destroys it.

## Alpha behavior
Contains native FixtureAdmission and separately identified Nuvio diagnostic package/file-trigger plumbing; lacks new Collections fixture controller.

## Beta behavior
Moves normalized-identical FixtureAdmission to tests/support, gates fixture APIs with test macro/include path, removes production task watcher; adds isolated build/launch-gated Collections fixture and Lanista click-target lifetime fix.

## Actual owner/caller
No production user route for task diagnostics: fixture APIs are test-macro gated; diagnostic file watcher removed from Android StreamServer. Collections fixture is non-Android build+launch gated. Lanista click lifetime fix belongs optional development tooling, not a missing consumption feature.

## Donor evidence
native/player/NuvioFixtureAdmission.h:1-75; native/player/streamserver_android.cpp:90-291 diagnostic configuration/watcher/fixture path; native/devtools/LanistaServer.cpp:1429-1443 pre-fix click reply. N/A Alpha CollectionsTestProfileController: Beta-only.

## Beta evidence
tests/support/NuvioFixtureAdmission.h:1-75 (normalized-identical Alpha native fixture); native/cmake/NuvioBackendTests.cmake:27-41; native/devtools/CollectionsTestProfileController.h:24-83; native/main.cpp:2426-2469; native/devtools/LanistaServer.cpp:1439-1456

## Evidence layer
committed-alpha-beta

## Dependencies
COLOSSEUM_NUVIO_TEST_FIXTURES, tests/support include path, isolated test profile markers and build/launch guards; optional Lanista tooling.

## Reuse/adaptation
retain test-only isolation; no production donor transfer

## Files and integration points
No production implementation. Any later device fixture scenario must live in an explicitly isolated debug/test target using existing production owner contracts.

## Preserve
Keep test macro containment, private fixture identity, exact metadata pins, synthetic profile containment and Lanista captured reply data.

## Bounded unit
N/A for production; do not recreate task watcher to qualify normal playback.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Keep fixture admission outside production. Collections profile fixture compile+launch containment and Lanista click lifetime fix retained; no production watcher reinstatement.

## Existing tests
Alpha nuvio_fixture_admission_test.cpp and Beta tst_nuvio_stream_backend/production contract; Collections fixture containment definitions. No tests run here.

## Required host checks
For later authorized fixture work, verify production compile has no fixture flag/header path and test target admits only intended pins/profile identities.

## Local device acceptance (pending)
No audit device action; any future isolated fixture device run must avoid real app/profile and cannot substitute for normal production-route acceptance.

## Recovery
No product change. Revert any future isolated diagnostic target independently; do not remove or mutate real profile/media data.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
No new diagnostic/device run performed; fixture definition/source existence is not runtime acceptance.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
