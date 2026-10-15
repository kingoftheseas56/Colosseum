# W4-H Seal Repair Handoff

TASK: T-8 / W4-H seal repair
DATE: 2026-10-07
STATE: COMPLETE

Every Claude seal finding F1-F6 is closed, hygiene is closed, the four screen needs are recorded as named DEFER/exclusion inputs, and the required fresh Release superbuild/CTest sweep is green. W4-I / Slice 9 was not started and no contract hashes were recorded.

## Findings

**F1 — COMPLETE.** `w4-end-to-end-prototype/real_cue_journey.cpp` now runs a real-libmpv failure/fallback case before the retained CUE-pair proof. BB5 first resolves a deliberately nonexistent local path directly to mpv, with no file-existence pre-check. libmpv emits END_FILE error, `RealMpvEngine` routes it to `QueueSessionCoordinator::engineFailed`, BA4 records Failure, BB5 `playbackFailed()` selects the second candidate, BA4 records SourceChange immediately after Failure, the existing fixture loads, and playback reaches Playing. The BA1-active / BA2-bound occurrence invariant is checked after each material transition. Proving test: `vinyl_w4_h_real_cue_journey`.

**F2 — COMPLETE.** The `race-quit-restore` case in `fake_play_journey.cpp` now restores the pending input into a fresh harness, starts a fresh BB5 resolve, delivers the pre-quit request late and proves `IgnoredStale / StaleRequest` without consuming the fresh request, then accepts the fresh result and reaches Playing. The active-occurrence invariant is checked through restore, stale delivery, restart and Playing. Proving test: `vinyl_w4_h_fake_journey`.

**F3 — COMPLETE.** Added `ba3-session-persistence-prototype/fixtures/ba3-session-v1.json`, a real schema-v1 Wave 3 snapshot fixture. The BA3 contract test restores it with the current v2 reader and checks queue order/active occurrence, 42.5-second -> 42500-ms migration, and safe current defaults. The existing synthesized-v1 compatibility case remains. Proving test: `vinyl_w4_ba3_contract`.

**F4 — COMPLETE.** BA5 now has `PersonalDisplaySnapshot { title, artistCredit }` on Library entries and Playlist items as non-authoritative fallback presentation. Durable writer version is v3; the reader accepts v1-v3, restores v1/v2 without inventing snapshots, round-trips v3 snapshots, and rejects malformed/truncated snapshot data with typed `MalformedSnapshot`. H6-A now proves Library and Playlist rows retain readable snapshots during catalogue `Unavailable`, while restored stale display text never overwrites the authoritative BG1 result after recovery. `BA5-LIBRARY-PLAYLISTS-CONTRACT.md` was amended in place. Proving tests: `vinyl_w4_ba5_persistence_contract` and H6-A inside `vinyl_w4_h_fake_journey`.

**F5 — COMPLETE.** Contract ownership wording was repaired: BA1 occurrence-ID generation is an explicit DEFER owned by the Production Colosseum Vinyl queue-adoption owner; both BB1 failure/fallback lines name BB5 `SourceResolutionSession`; BE1 names BE2 as taste-state mutation owner and BE3 as deterministic ranking consumer. Proving check: static contract scan, not a CTest target. Current contracts no longer contain the four stale F5 hand-offs.

**F6 — COMPLETE.** BE2 now says W4-H reference-proves BA4 -> versioned recommendation derivation -> BE2 ingestion, with only production host wiring remaining. BA2-D now states that weak-client sidecar/event attribution, playlist-entry token ledger, serialized seek ownership and W4-H real-libmpv error/fallback composition are reference-proven; only production Colosseum player wiring/runtime qualification remain. The seal check's legitimate production-placement hits were left intact. Proving check: static contract inspection plus the green BE2/BA2-D/W4-H tests in the full sweep.

**Hygiene — COMPLETE.** `W4-H-HANDOFF.md` was refreshed from the stale BLOCKED/35-of-35 state to the current DONE/37-of-37 state. Deleted `w4-end-to-end-prototype/test_fake_play_journey.cpp` and `w4-end-to-end-prototype/W4HCompositionProofs.h` because neither belonged to an active CMake target and both were stale look-alikes of the canonical W4-H proof path. The active proof path is `fake_play_journey.cpp` plus `W4ComposedIntegration.cpp/.h`. Proving check: W4-H directory/CMake wiring audit and full CTest sweep.

**DEFER input — RECORDED, no code.**
1. Edition label + catalogue number: owner = future Vinyl catalogue-shape owner; the freeze may lift this when the catalogue model gains edition label/catalogue-number fields.
2. Credit-role display order: owner = BF1 context-graph contract owner; confirm whether `CreditRoleKind` declaration order is the intended main-roles-first display order.
3. Annotation vote count + artist-verified flag: owner = BC3 production-annotation owner; current BC3 has neither field.
4. Saving a specific edition: recorded v1 exclusion. BA5 continues to save Artist, ReleaseGroup and Recording only; current album Save targets ReleaseGroup. Owner if revisited = BA5 production-adoption owner.

## Fresh qualification

Build directory, verified absent before configure:
`C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-h-seal-repair-final-20261007`

Configure/build toolchain:
- Visual Studio 17 2022, x64
- MSVC 19.44.35223.0
- Windows SDK 10.0.26100.0
- Qt 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`
- CMake at `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- libmpv prefix `C:\tools\mpvqt-feasibility\libmpv-prefix`
- `COLOSSEUM_ROOT=C:\Users\Suprabha\Desktop\Brotherhood\Colosseum` read-only fixture root

Fresh configure: PASS. Release build from zero: PASS, exit 0. Build-output scan: 0 warning hits, 0 duplicate-symbol/ODR/linker-conflict hits, 0 error hits.

Generated `CTestTestfile.cmake` was inspected before the sweep. Release explicitly registers `vinyl_w4_h_real_cue_journey` with `tests/lanista_capture/assets/Colosseum.mp4`; it is not skipped.

Full CTest command:
`ctest --test-dir C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-h-seal-repair-final-20261007 -C Release --output-on-failure`

Result:
- **37/37 PASS**
- **100% tests passed**
- **0 failed**
- total CTest time: **39.87 s**
- #36 `vinyl_w4_h_fake_journey`: PASS, 2.56 s
- #37 `vinyl_w4_h_real_cue_journey`: PASS, 12.38 s, registered and executed

## Negative controls

**F1 fallback removal:** ran the real journey with `--negative-no-fallback`, which removes the second candidate while keeping the real mpv missing-path load. It exited **1** with:
`FAIL: real mpv failure must cause BB5 to choose a fallback source`
This shows the new F1 proof depends on the fallback arrow and fails when that source is removed.

**F4 corruption + previous version:** directly ran `vinyl_w4_ba5_persistence_contract.exe`; exit **0**. Its active negative-control assertions prove a current-v3 file truncated by one byte restores as typed `MalformedSnapshot`, and a synthesized previous-version v2 snapshot with the v3 display fields removed restores successfully with display fallback remaining unknown rather than invented. The same test also keeps v1 migration coverage.

## Files changed

- `BA1-QUEUE-CORE-CONTRACT.md`
- `BA2D-MPV-ADOPTION-CONTRACT.md`
- `BA4-LISTEN-EVENT-LEDGER-CONTRACT.md`
- `BA5-LIBRARY-PLAYLISTS-CONTRACT.md`
- `BB1-SOURCE-CANDIDATE-CONTRACT.md`
- `BE1-RECOMMENDATION-CANDIDATE-CONTRACT.md`
- `BE2-FEEDBACK-TASTE-STATE-CONTRACT.md`
- `ba3-session-persistence-prototype/test_vinyl_session_persistence.cpp`
- `ba3-session-persistence-prototype/fixtures/ba3-session-v1.json`
- `ba4-listen-event-ledger-prototype/VinylListenEventLedger.h`
- `ba4-listen-event-ledger-prototype/VinylListenEventLedger.cpp`
- `ba5-library-playlists-prototype/VinylLibrary.h`
- `ba5-library-playlists-prototype/VinylLibrary.cpp`
- `ba5-library-playlists-prototype/VinylPlaylist.h`
- `ba5-library-playlists-prototype/VinylPlaylist.cpp`
- `ba5-library-playlists-prototype/VinylPersonalStatePersistence.h`
- `ba5-library-playlists-prototype/VinylPersonalStatePersistence.cpp`
- `ba5-library-playlists-prototype/test_vinyl_personal_state_persistence.cpp`

- `w4-end-to-end-prototype/fake_play_journey.cpp`
- `w4-end-to-end-prototype/real_cue_journey.cpp`
- `w4-end-to-end-prototype/W4ComposedIntegration.cpp`
- `W4-H-HANDOFF.md`
- `W4-H-SEAL-REPAIR-HANDOFF.md`
- `WAVE4-AGENT-ROOM.md` (one T-8 HANDOFF message appended after this file was written)

## Files deleted

- `w4-end-to-end-prototype/test_fake_play_journey.cpp`
- `w4-end-to-end-prototype/W4HCompositionProofs.h`

## Scope / open items

No W4-H seal finding remains open. The four Vinyl-screen items above are deliberately future DEFER/exclusion inputs, not Wave 4 defects and not implemented here.

Production Colosseum remained read-only. No commit, push, reset, clean, stash, rebase, branch or worktree operation was performed. W4-I / Slice 9 freeze was not started. No contract hashes were recorded.

Claude should review this handoff and the current tree before any freeze work begins.
