# BR-TESTS — Beta provides focused real-owner Android contracts

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Preserves a focused way to detect native lifecycle/source-boundary regressions without building the whole application. Avoids claiming test definitions or prior host results qualify a phone/tablet.

## Alpha behavior
Has focused Nuvio backend host setup and separate test targets such as subtitle parser; does not have Beta AndroidIntegrationTests aggregation/production-without-fixtures contract.

## Beta behavior
Adds focused real-owner DownloadStore/Back/Media3 state/redraw/subtitle host contracts and a separate Nuvio production archive without fixture flags/include path.

## Actual owner/caller
These are host test targets, not APK runtime components: native CMake tests-only branch calls AndroidIntegrationTests; production adapter sources are compiled by the contracts. Definitions do not establish Android JNI/ABI or device operation.

## Donor evidence
native/CMakeLists.txt:19-30 old COLOSSEUM_NUVIO_BACKEND_TESTS_ONLY branch and :1421-1434 separate subtitle_sidecar_parser_test. N/A native/cmake/AndroidIntegrationTests.cmake: Beta-only file.

## Beta evidence
native/cmake/AndroidIntegrationTests.cmake:2-35; native/CMakeLists.txt:28-32

## Evidence layer
committed-alpha-beta

## Dependencies
Qt host Core/Test/Gui/Network/Qml/Quick; production native owner sources; test-mode isolation and explicit Android recovery policy.

## Reuse/adaptation
preserve Beta test composition; selectively adapt non-overlapping Alpha assertions

## Files and integration points
No test execution or product implementation in this audit. Future accepted fixes should extend existing focused real-owner target rather than duplicate algorithms in tests.

## Preserve
Keep production-without-fixtures compile contract, existing AndroidPinnedTorrent host policy selection, isolation and targeted test ownership.

## Bounded unit
N/A to current implementation; relevant future tests travel with the coherent mechanism they verify.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Preserve test integration; inspect definitions only. Nuvio production contract compiles without fixture flag/header search path. Tests not run in audit; neither host test definition nor supplied prior pass qualifies device behavior.

## Existing tests
native/cmake/AndroidIntegrationTests.cmake names tst_android_download_recovery, tst_android_back_contract, android_media3_state_harness, android_surface_redraw_gate_test and subtitle_sidecar_parser_test; source definitions inspected only.

## Required host checks
No new runs. For subsequent authorized implementation use affected focused targets; distinguish compilation, execution and supplied prior receipts.

## Local device acceptance (pending)
Host tests leave JNI/ABI, real WebView/keyboard/input, foreground-service and physical device behavior unqualified; run only separately authorized targeted device acceptance.

## Recovery
No product/test mutation made. Future test-only rollback does not touch user profiles or installed app data.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
No new host tests executed and no device acceptance derived from source definitions.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
