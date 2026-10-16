# Receiving review: Vinyl foundation Build 0.25

**Decision: ACCEPTED as the Linux functional 0.25 checkpoint. Dots may proceed to Build 0.5 from `5494bab39d4376abca1b87687d557e2d55e0af41`. No production merge, release, final-design approval or complete-Vinyl qualification is implied.**

## Evidence reviewed

- Dots delivery at evidence commit `0031ba8a292436e471272f8cfddb71973b23ad6b`, source delta from pinned app base `3fcdd4be620281d70b59961450d638a77439da91` to candidate `5494bab39d4376abca1b87687d557e2d55e0af41`.
- Focused source review: native local adapter, identity handling, real music-session integration, Main/native/build wiring, Album/Song changes, input/navigation behavior and related contract tests. Imported donor source is not represented as a new exhaustive audit of every line.
- Native extracted-app screenshots inspected for Library, playing Album and Song. JSON/protocol receipts show actual MpvItem PCM audio-track/decoder state, a roughly two-second progressing media clock, stable paused clock, pointer/keyboard seek to approximately 22/27 seconds, resume, page/back preservation and physical Escape to Library.
- Missing-file recovery and malformed 32-byte WAV evidence show truthful native errors and recovery/clean exit. Packaging/source identity and archive roundtrip are Dots-provided receipts; the large Linux archive was not locally reconstructed or launched on Windows for this review.
- Six focused committed CTests pass. Historical component registry remains **211 passed / 8 failed / 6 skipped**, not rewritten as fully green. All eight failures are in three artwork-dependent suites. A separate supplied-photo overlay reports **56 passed / 0 failed / 1 skip**; its three exact test hashes independently match the source commit. This improves component evidence without proving original artwork fidelity or native Artist acceptance.

## Independent receiving check

Compiled the exact source `VinylLocalSource`, existing `VaultIdentity` and provided native harness with **Windows MSVC 19.44 / Qt 6.11.1**, in disposable isolated storage. It completed **20 checks, exit 0**. Checks cover stable local identity, URL/path deduplication, asynchronous completion, newest-request ownership, cancellation, invalid/missing inputs, identity registry restart, rename reconciliation and profile cancellation/clearing. This is a native adapter/identity check, not full Windows app, player or speaker qualification.

## Why the checkpoint passes

The implemented path is an assembled native Colosseum mode: real local input -> Library -> existing Album/Song presentation -> actual existing player. It uses VaultIdentity, CollectionStore and ProgressStore rather than a second identity/library/player system. Browsed and playing identities stay distinct; the local adapter has an injectable request/completion/cancellation seam and no external fetch fallback on a missing local file. External torrent contracts are preserved as deferred source rather than forced into incompatible current StreamServer ownership.

No blocking functional finding was established within this reviewed 0.25 scope. One trailing-whitespace line in imported `tests/qml/tst_vinyl_world_visual.qml:46` makes `git diff --check` return 2; this is a nonblocking formatting item, not a playback failure.

## Limits retained

- `ao=null`: decoder/player behavior is supported; audible speaker output is not verified.
- Windows full native app and the historical intermittent fail-fast root cause remain unqualified. Optional lifetime patch was not applied, and this acceptance does not credit it or claim a cure.
- The minimal local Library entry is temporary milestone UI. It is not the approved final Vinyl design or evidence that Home/Discover/Artist are complete.
- The image warnings/asset limits remain visible. Supplied private artwork is not approved for redistribution by this review.
- Full queue, metadata/detail tools, durability/profile/restart and cross-world lifecycle are still governed by later milestones.

## Build 0.5 instruction

Continue the same single Dots-owned project from the accepted source. Connect the approved Home, Discover/local browse, Library/list, Artist, Album and Song presentation to owned records, complete queue/next/previous/end-of-track and existing shuffle/repeat controls, and retain all 0.25 regressions. Do not turn the temporary Library list into a replacement final frontend. Reuse current shared controls and the app owners in REUSE-MAP.md. Cloud-test the native app before delivery, publish screenshots plus behavioral/test logs, exact source/build identity and honest remaining gaps. Stop at the 0.5 receiving gate. External catalogue/streaming integrations remain deferred.

This document authorizes the next milestone; it does not establish that the instruction has reached Dots or that 0.5 has started.
