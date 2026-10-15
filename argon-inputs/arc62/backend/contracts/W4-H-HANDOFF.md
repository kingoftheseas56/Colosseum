# W4-H One Superbuild + End-to-End Play Journey Handoff

AGENT: Agent-8-Recovery / Preflight seal repair
TASK: T-8 / W4-H One Superbuild + End-to-End Play Journey
STATE: DONE
DATE: 2026-10-07

## Current state

- W4-H is complete through the Claude seal findings F1-F6 plus hygiene.
- The active superbuild still contains 37 registered tests.
- Final fresh Release sweep: 37/37 PASS, zero failures.
- Test #36 `vinyl_w4_h_fake_journey`: PASS.
- Test #37 `vinyl_w4_h_real_cue_journey`: PASS and executed against the read-only Colosseum fixture.
- Production Colosseum was not modified.
- W4-I / Slice 9 was not started and no contract hashes were recorded.

## Seal repair now included

- Real libmpv missing-local-source failure reaches `engineFailed`, BB5 fallback, BA4 Failure then SourceChange, then real fixture playback.
- Quit-during-resolve restoration starts a fresh resolver request, rejects the pre-quit late result as stale, and reaches Playing.
- BA3 restores both the retained synthesized v1 compatibility case and a checked-in Wave 3 v1 snapshot fixture.
- BA5 durable schema v3 carries optional non-authoritative title/artist-credit display snapshots for Library and Playlist rows while preserving v1/v2 read compatibility.
- H6-A proves those BA5 rows remain readable during catalogue Unavailable and never overwrite canonical catalogue truth.
- F5/F6 stale ownership/adoption wording is repaired to current owners and reference-vs-production status.

## Hygiene

- Deleted `w4-end-to-end-prototype/test_fake_play_journey.cpp`.
- Deleted `w4-end-to-end-prototype/W4HCompositionProofs.h`.
- They were unbuilt look-alikes of the active proof path; canonical W4-H coverage is `fake_play_journey.cpp` plus `W4ComposedIntegration.*`.
- The four Vinyl-screen needs are recorded as named DEFER/exclusion inputs in `W4-H-SEAL-REPAIR-HANDOFF.md`.

## Final qualification

Fresh build directory:
`C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-h-seal-repair-final-20261007`

Toolchain:
- Visual Studio 17 2022 x64 / MSVC 19.44.35223.0
- Windows SDK 10.0.26100.0
- Qt 6.11.1 `C:\Qt\6.11.1\msvc2022_64`
- libmpv `C:\tools\mpvqt-feasibility\libmpv-prefix`

Build completed with exit 0 and no warning, duplicate-symbol, ODR, or linker-conflict diagnostics. Full CTest completed 37/37 PASS.

Detailed finding-by-finding evidence and negative controls:
`W4-H-SEAL-REPAIR-HANDOFF.md`

NEXT: Claude reviews this handoff and the current tree. Freeze remains a separate later slice.
