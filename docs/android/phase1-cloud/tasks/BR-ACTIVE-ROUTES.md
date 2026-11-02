# BR-ACTIVE-ROUTES — Native graph keeps readers/platform backend active while desktop organs remain excluded

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Avoids mistaking desktop code presence for an Android capability or labelling retained reader/platform machinery missing. A broad Alpha import could activate unsupported desktop dependencies or remove newer common state.

## Alpha behavior
Android compiles AndroidEbookRenderer/Media3/Android StreamServer and selects AndroidPaper; desktop WebEngine/Vault/Vinyl organs are excluded by platform graph. Native Back/fontScale APIs exist.

## Beta behavior
Retains that Android reader/player/backend separation, adds common profile-owned Collections/AniList code and explicit systemBackAvailable exposure; desktop-only organs remain excluded.

## Actual owner/caller
native/CMakeLists includes android.cmake -> Android renderer/Media3/streamserver_android target sources; main Reader2Bridge/installAndroidEngineBindings and ComicReaderCore registrations -> Main ReaderShell -> ReaderPaper -> AndroidPaper. PlatformRuntime remains the native Back/font-scale owner. Desktop exclusions are compile/staging gates, not missing user routes.

## Donor evidence
native/cmake/android.cmake:4-57 target_sources and optional Nuvio branch; qml/reader2/ReaderPaper.qml:49 Android selector (matching committed blob); native/platform/PlatformRuntime.cpp JNI nativeFontScale/nativeBackGesture (matching blob). N/A for Alpha NamedCollections/AniList additions: absent from its tracked tree.

## Beta evidence
native/CMakeLists.txt:867-1000,1204-3359; native/cmake/android.cmake:4-57,89-99; native/main.cpp:903-920,1865-1875

## Evidence layer
committed-alpha-beta

## Dependencies
Android CMake target/staging filters; main context registration; QML platform selector; optional stamp-verified Nuvio artifact; platform capability owner.

## Reuse/adaptation
preserve active platform ownership; no donor implementation

## Files and integration points
No implementation requested. Future accepted reader/backend changes belong the existing Android source/registration/selector owners, not a second engine.

## Preserve
Keep native Android reader/JNI, Media3 queued initialization, optional Nuvio availability gates, common canonical stores and desktop-only compile/staging exclusions.

## Bounded unit
No implementation unit for this preservation record; use as acceptance constraint on separately approved fixes.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Keep Android reader/JNI and production backend route; native Back/fontScale retained, QML consumption gaps belong PF/worlds findings. Source graph alone does not prove packaged optional engine ABI.

## Existing tests
Existing Android paper/boot/boundary/font contracts and Beta native AndroidIntegrationTests definitions; source inspected only.

## Required host checks
For any later accepted change, verify intended Android target sources/context names/QML selector remain wired and unavailable dependencies remain excluded. No checks executed by this audit.

## Local device acceptance (pending)
No new device action for this record; later reader/player work must qualify its actual Android route and installed artifact separately.

## Recovery
No product change or migration to roll back. Any future route change must revert its wiring as one unit through data-preserving same-package upgrade; never clear profile data.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
Source graph does not establish installed optional engine ABI/version or physical Android acceptance; build/artifact provenance belongs platform receipt.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
