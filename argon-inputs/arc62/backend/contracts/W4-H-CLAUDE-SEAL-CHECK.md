# W4-H Seal Check (Claude, independent)

FROM: Claude (Opus 5.5), reviewer
TO: Lead, Agent-9 (W4-I freeze)
DATE: 2026-10-07
CHECKED AGAINST: `claude-brainstorm-review-post-wave3/CLAUDE-REVIEW-RESPONSE.md` section 7, criteria 1-12
INPUT: `W4-H-EVIDENCE.md` (Agent-8-Recovery, STATE: DONE, 17:36 IST) and the current source tree

## Verdict

**W4-H is accepted. Freeze (W4-I) may start, with five items to close inside the freeze.**
Criteria 1, 2, 4, 5, 6, 7, 9, 10 pass. Criterion 3 passes narrowly and 8 passes with a rule left open. 11 and 12 are freeze-time criteria and are not yet met; that is expected, and the offending lines are listed below so the freeze does not have to find them.

Nothing here reopens a lane. Item F1 is one small test; the rest is text and file hygiene.

## Independent reproduction (criterion 1)

Not taken from the evidence file. Run by Claude on Laptop 1:

- fresh build dir `C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-h-claude-verify-20261007` (did not exist before)
- configure: Visual Studio 17 2022 x64, Qt 6.11.1 `msvc2022_64`, `COLOSSEUM_ROOT` = the Brotherhood Colosseum checkout, exit 0
- Release build from zero: exit 0, zero `warning` lines, zero LNK duplicate-symbol / ODR diagnostics
- `ctest -C Release --output-on-failure`: **37/37 passed**, 9.35 s; #36 fake journey passed; #37 real CUE journey passed in 6.61 s (registered, not skipped)

This matches the evidence file's claim exactly.

## Criteria

| # | Criterion | Result | Basis |
| --- | --- | --- | --- |
| 1 | One superbuild, one ctest, no ODR | PASS | reproduced above |
| 2 | Fake journey with causes, BA3 at three points | PASS | `fake_play_journey.cpp` `verifyMainJourney`: checkpoints A/B/C, Failure distinct from Stop, exhaustion ends in typed stop |
| 3 | Same journey once on real libmpv, with a CUE pair | PASS, narrow | see F1 |
| 4 | Seven scripted races | PASS | skip-audio, skip-video, play-now-during-fallback, remove, stop in `bb5 .../test_source_resolution_session.cpp` (`race-*`); double-EOF and quit-restore in `fake_play_journey.cpp`. All inside the one sweep. See F2 |
| 5 | BA2 bound occurrence is none or BA1 active, after every step | PASS | `requireInvariant` after each transition in W4-H, BB5, BF3 and W4-B adversarial |
| 6 | v1 BA3 under v2 reader; BA4/BA5/BE2 round-trip and typed corruption | PASS | all three return `MalformedSnapshot`; see F3 on the v1 fixture |
| 7 | BA4 across backward clock and restart, no identity collision | PASS | clock-step fixture; restart fixture reproduces the generation collision and separates by `PlayInstanceId` |
| 8 | Merged MBID via `Redirected` in library, playlist, queue, history; `Unavailable` restores a queue with snapshots | PASS, rule open | H6-A; see F4 |
| 9 | Lyric document swap attaches nothing to the wrong line | PASS | H6-B, typed `SourceDocumentMismatch` on both translation and annotation |
| 10 | BE3 deterministic, traceable, passive vs deliberate told apart | PASS | H6-C |
| 11 | No unowned "later owner / later policy / outside this slice" | NOT YET (freeze) | F5 |
| 12 | No "production adoption" about anything 1-10 exercised | NOT YET (freeze) | F6 |

## Findings to close inside the freeze

**F1. The real-libmpv journey is a CUE pair, not the journey.** `real_cue_journey.cpp` drives BA2-C, BA2-D and BB3/BB4 only. It includes no BB5 resolution session, no BA4 ledger, no BA3, and no failure or fallback. Criterion 3 asked for the same journey as criterion 2. The gap that matters is one arrow: a real libmpv load failure (missing or unreadable file) reaching `engineFailed`, BB5 choosing the fallback, and BA4 recording `Failure` then `SourceChange`. Real mpv error delivery is exactly where a fake and the engine can disagree. Ask: one added real-libmpv case, first source a nonexistent local path, second the existing fixture. If the Lead prefers not to add it, record it in the freeze DEFER list as an explicit exclusion with an owner (production adoption slice). Do not leave it implied as covered.

**F2. Quit-during-resolve restores but never resumes.** `race-quit-restore` proves the pending intent survives and no BA2 generation is resurrected. It stops there. Nothing shows the restored pending intent starting a fresh resolve and reaching playback, and the pre-quit request's late result is never delivered to the restored state. Checkpoint B covers restore-then-play for the active-playback case only. Small extension of the existing fixture, or a DEFER line.

**F3. The v1 BA3 fixture is synthesised from a v2 snapshot** (fields removed and renamed in the test). It proves the reader's upgrade path against the test author's memory of v1, not against a v1 file. A real v1 snapshot from the Wave 3 tree, checked in as a fixture, costs one file. Low severity; BA3 v1 never shipped.

**F4. BA5 stores no display snapshot.** Ruling R11 point 2 is "display snapshot beside every stored ID". BA3 (queue) and BA4 (history) have one. Library and Playlists do not, and the evidence file says so honestly. Consequence for the user: with the catalogue unavailable, saved albums and playlist rows have IDs and no titles. Criterion 8 as worded only names the queue, so it passes, but the rule is open. Either add the snapshot to BA5's durable format before hashes are taken (a format change after freeze is a migration), or record a named exclusion. Claude recommends adding it now: it is the cheapest moment it will ever have.

**F5. Criterion 11 hits in the contracts today (4):**
- `BA1-QUEUE-CORE-CONTRACT.md:49` "A later owner may use UUIDs" for occurrence ID generation. Needs a named owner or a DEFER line.
- `BB1-SOURCE-CANDIDATE-CONTRACT.md:308` and `:426` "later policy" for candidate failure and provider fallback. Stale: BB5 (W4-C) owns this now. Amend to name BB5.
- `BE1-RECOMMENDATION-CANDIDATE-CONTRACT.md:41` "authoritative in later policy". Stale: BE2 and BE3 own it. Amend.

**F6. Criterion 12 hits that describe something now exercised:**
- `BE2-FEEDBACK-TASTE-STATE-CONTRACT.md:151` "Future production adoption may derive this from the host event ledger". The derivation exists (W4-G) and criterion 10 exercises it. Amend.
- `BA2D-MPV-ADOPTION-CONTRACT.md:142`, `:507`, `:515` describe the sidecar and token ledger as a future production shape. `MpvSidecarReference` now exists and test #37 runs on it. Amend to say what is reference-proven and what remains (wiring into production Colosseum).
- The other hits (BA2B:220, BA4:437-443, BA5:89, BB3:3, BB4:148/290, BD2:447) are about production Colosseum placement and are legitimate. BB3:3's status line should lose "pending" wording only if the Lead reads it as covering the CUE window, which #37 exercises.

## Hygiene, before hashes are recorded

- `W4-H-HANDOFF.md` still reads `STATE: BLOCKED` with the 35/35 figures. The Lead's audit asked for both files to be refreshed; only the evidence file was.
- The Agent Room has no completion message for T-8. The last entry is `MSG-AGENT-8-RECOVERY-CLAIM` (16:52).
- Two orphans in `w4-end-to-end-prototype/`: `test_fake_play_journey.cpp` (in no CMake target) and `W4HCompositionProofs.h` (36 KB, included by nothing). Delete or wire them. A frozen baseline with unbuilt look-alike tests is where the next reader copies from.

## Separate from the seal: four needs from the Vinyl screens

From the approved song page and album page spec (`Brotherhood/docs/superpowers/specs/2026-10-07-vinyl-song-page-and-album-page-design.md` section 7), checked against the headers today. These are not Wave 4 defects. They are listed so the freeze DEFER list can name them instead of the screens discovering them later.

1. **Edition label and catalogue number.** BG1 `Release` has no label or catalogue-number field (no match for either in `bg1-catalogue-client-prototype/*.h`). The album page's Editions list shows both.
2. **Order of credit roles.** BF1 `CreditRoleKind` is declared PrimaryArtist, FeaturedArtist, Performer, Producer, Composer, Lyricist, Engineer, Other. The album page wants main roles first. If that declaration order is the intended display order, one sentence in the BF1 contract settles it.
3. **Annotation vote count and artist-verified flag.** Absent from BC3 (`bc3-annotation-prototype/*.h` has neither). The song page's note picker ranks by both.
4. **Saving a specific edition.** BA5 rejects it: `isSupportedLibraryKind` accepts Artist, ReleaseGroup and Recording only (`VinylLibrary.cpp:9`). This matches the review's DEFER item 6. The album page's Save acts on the album (release group), so nothing is blocked; it only matters if a per-edition save is wanted later.

Items 1 and 3 are catalogue-shape questions that the Grand Music Database arc will also face. Recording them as named DEFER items is enough for the freeze.
