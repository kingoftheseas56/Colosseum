# BR-READER-RETAINED — Both snapshots share guarded Android WebView reader; no whole-reader feature gap established

Class D; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Native Android EPUB publication script-disabled content, notes/bookmarks/search/progress routes retained; read-along explicitly unsupported by Android host in both versions, not a Beta regression.

## Alpha behavior
AndroidPaper/AndroidEbookRenderer/AndroidEbookHost WebView with trusted asset/publication routes, generation checks and font path allowlist.

## Beta behavior
Equivalent current guards and same native comic core/input; reader Java diff is comment-only.

## Actual owner/caller
Main->ReaderShell->ReaderPaper Android selector->AndroidPaper->JNI foreign WebView. Readium-labelled notes do not describe this active source engine.

## Donor evidence
native/platform/android/src/org/colosseum/reader/AndroidEbookHost.java:95-148; qml/reader2/ReaderPaper.qml:49; resources/reader2/android_boot.js:1-43

## Beta evidence
same paths/functions/line ranges; native/reader2/AndroidEbookRenderer.cpp:120-163; main.cpp:905-906; native/cmake/android.cmake:15-16

## Evidence layer
committed-alpha-beta

## Dependencies
N/A: no immediate transfer proposed; preserve existing owner/availability boundary.

## Reuse/adaptation
retain equivalent owner

## Files and integration points
N/A: preservation/exclusion or unresolved decision; no implementation selected.

## Preserve
Keep current canonical state, saved preferences, security/lifecycle owners and approved desktop presentation.

## Bounded unit
N/A: no implementation before the stated evidence/decision gate.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Preserve security and generation/font guards regardless of provenance. Do not claim every Android book feature accepted from opening a book.

## Existing tests
tests/android/ReaderPublicationSessionTest.java; tests/android_reader_fonts_test.mjs; test_android_reader_boundary.py; reader2_profile_privacy_harness.cpp.

## Required host checks
N/A for preservation/exclusion; unresolved behavior needs a focused current-owner reproduction before change.

## Local device acceptance (pending)
No new device pass claimed. Applicable behavior remains subject to the independent device matrix.

## Recovery
N/A: no product change proposed. Retain app data and existing same-package/signature update path.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
No comprehensive device reader feature acceptance; exact installed source provenance delegated platform overlay/build packet.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
