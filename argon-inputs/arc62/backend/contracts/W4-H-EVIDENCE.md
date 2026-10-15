# W4-H Final Evidence

AGENT: Agent-8-Recovery
TASK: T-8 / W4-H One Superbuild + End-to-End Play Journey
STATE: DONE
DATE: 2026-10-07

## Recovery basis

This file supersedes the stale pre-repair BLOCKED status that the original Agent-8 left behind. The historical blocker record remains preserved in `W4-H-BLOCKER-BA4-FAILURE-CAUSE.md`.

The original blocker was real: W4-B could emit `OccurrenceEndCause::Failed`, while BA4 could not persist Failure distinctly from Stop. Hemanth later authorized the repair. The current tree now has factual BA4 Failure semantics, exact stale-token rejection, durable schema v3 with v1/v2 read compatibility, and a W4-G regression proving Failure does not fabricate completion/skip/taste evidence. The Lead subsequently verified the blocker resolution before this recovery pass.

Agent-8-Recovery resumed from that post-repair state. Existing green H1-H5/H7 behavior was preserved; the recovery implementation added only the three composed H6 proofs identified by `MSG-LEAD-W4H-READINESS-AUDIT`, then ran a fresh full seal.

## Final toolchain

- Windows / Visual Studio 2022 generator
- MSVC 19.44.35223.0
- Windows SDK 10.0.26100.0
- Qt 6.11.1, `C:\Qt\6.11.1\msvc2022_64`
- CMake, `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- libmpv prefix, `C:\tools\mpvqt-feasibility\libmpv-prefix`
- production Colosseum fixture root passed read-only as `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`

## Final clean build

Fresh build directory:

`C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-h-final-seal`

The directory did not exist before final qualification.

Configure:
- fresh CMake configure: PASS
- generator: Visual Studio 17 2022, x64
- compiler detected: MSVC 19.44.35223.0
- Qt 6.11.1 exact package: PASS
- generation: PASS

Release build:
- full superbuild from zero: PASS
- exit code: 0
- all retained BA0-BG2 contract executables linked
- `vinyl_w4_h_fake_journey.exe`: built
- `vinyl_w4_h_real_cue_journey.exe`: built
- `vinyl_w4_superbuild_link_smoke.exe`: built

Before CTest, the generated `CTestTestfile.cmake` was inspected. The Release configuration explicitly registers `vinyl_w4_h_real_cue_journey` against the production read-only fixture `tests/lanista_capture/assets/Colosseum.mp4`; it was not skipped or absent.

Both final W4-H executables were also launched directly from the fresh Release tree with the Qt/libmpv runtime PATH:
- fake journey direct execution: exit 0
- real CUE/libmpv direct execution: exit 0

## Final CTest seal

Exactly one full executing CTest sweep was run from the fresh tree:

`ctest --test-dir C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-h-final-seal -C Release --output-on-failure`

Result:

- **37/37 tests PASS**
- **100% tests passed**
- **0 tests failed**
- total CTest time: 8.69 s
- test #36 `vinyl_w4_h_fake_journey`: PASS
- test #37 `vinyl_w4_h_real_cue_journey`: PASS, 6.55 s

## H1-H5 retained integration evidence

The active one-superbuild still composes all retained Wave 1-4 reference lanes and the W4-H integration executables. The final clean sweep keeps every BA0-BG2 contract green alongside W4-H.

The fake Play journey still proves the composed path:

`queue -> source resolution -> playback -> history -> persistence -> fallback -> next track`

Retained behavioral proof includes:
- deterministic source selection and ordered fallback
- fallback after resolve/playback failure
- fallback exhaustion terminating honestly
- natural EOF advancing the queue
- factual BA4 Completion, SourceChange, Failure, Skip, Stop and ListeningSpan semantics
- PlayInstance continuation across same-occurrence source replacement
- BA1 active occurrence / BA2 bound occurrence invariant after relevant transitions
- profile isolation
- fresh live BA2 identity after restore

BA3 still proves the three journey checkpoints used by W4-H:
- awaiting-source checkpoint
- active playback checkpoint
- later/advanced journey checkpoint

Restored state does not reuse stale live work identity; fresh playback work is minted after restore.

## Race / stale-work matrix

The final active W4-H CTest set retains the previously green race matrix across W4-B adversarial coverage plus the W4-H fake journey:

- skip during audio resolve
- skip during video resolve
- Play Now during fallback
- remove-active during resolve
- stop during resolve
- double EOF
- quit during resolve + restore
- stale resolver success
- stale resolver failure
- expiry refresh racing queue mutation
- late fallback success after stop

The active-occurrence invariant remains: BA2 is either unbound or bound to exactly BA1's active `QueueOccurrenceId`.

## BA4 factual terminal facts

The repaired BA4 seam is exercised in the final clean run:
- Failure is distinct from Stop
- stale callbacks are rejected by exact work token
- durable listen schema is v3
- v1/v2 compatibility remains retained
- W4-H factual history includes Failure without reclassifying it as completion or skip

The recovery recommendation proof also derives the actual W4-H Play-journey ledger and verifies that its playback Failure does not fabricate positive or negative taste outcomes.

## H6-A: MusicBrainz Redirected + catalogue Unavailable

The active `vinyl_w4_h_fake_journey` now compiles and executes a composed durable-state proof using the BG1 canonical fixtures.

Fixture identity:
- old Recording MBID: `69999999-9999-4999-8999-999999999999`
- canonical replacement Recording MBID: `62222222-2222-4222-8222-222222222222`
- canonical catalogue title after recovery: `光`

Durable/reference state is exercised through:
1. BA3 queue/session state with a queue display snapshot
2. BA5 Library membership
3. BA5 Playlist item
4. BA4 durable history with a history display snapshot

With the catalogue forced Unavailable:
- BA3 queue/session restores successfully
- BA5 Library and Playlist restore successfully
- BA4 history restores successfully
- catalogue lookup returns typed `Unavailable`
- batch lookup returns typed `Unavailable`
- stored display snapshots remain readable fallback only
- no canonical metadata is fabricated

When the catalogue becomes available again:
- old MBID returns `Redirected`, not `NotFound`
- the redirect target is the typed canonical `RecordingMbid`
- a separate missing MBID returns `NotFound`
- batch lookup preserves `Redirected` and `NotFound` as distinct states
- queue, Library, Playlist and history all resolve through the same MusicBrainz read-repair seam
- provider identity cannot become repair identity; the proof includes compile-time non-identity/non-convertibility checks between `ProviderId` and `RecordingMbid`
- the stale local title does not overwrite authoritative catalogue truth
- durable old IDs are intentionally not rewritten in place; canonicalization is read-time repair, faithful to the current contract

Important contract caveat: BA5 does not own display snapshots. H6-A therefore proves readable Unavailable fallback on the current snapshot-owning BA3 queue and BA4 history seams, while BA5 proves independent structural Library/Playlist restoration. No new storage owner was invented.

## H6-B: timed-text document swap

The active W4-H fake entrypoint now composes the real BC1/BC2/BC3 contracts.

The proof creates:
- timed-text document A for Recording X with cue `cue-1`
- translation bound to document A
- cue-linked annotation bound to document A
- timed-text document B for the same Recording X, with the same cue ID/ordinal but a different document identity/revision

Verified:
- A and B have distinct `TimedTextDocumentId`
- projecting A's translation onto B fails with typed `TranslationErrorCode::SourceDocumentMismatch`
- resolving A's annotation against B fails with typed `AnnotationErrorCode::SourceDocumentMismatch`
- cue-ID equality alone never crosses document identity
- no guessed attachment occurs

The same composed proof then performs a real W4-C source replacement on the same QueueOccurrence:
- BA2 work generation increases
- `QueueOccurrenceId` remains the same
- Recording remains the same
- the still-valid timed-text document remains applicable
- translation ownership remains applicable

This proves the W4-F law that timed-text ownership is Recording/QueueOccurrence/document based, not BA2-generation based.

## H6-C: recommendation composition

The active W4-H fake entrypoint now proves the full composition:

`BA4 factual ledger -> W4-G deriveRecommendationInputs(...) -> BE2 behavioral state -> RecommendationExposureLog -> BE3 rankDeterministically(...)`

Two connected proofs run.

First, W4-G derives directly from the existing W4-H Play-journey BA4 ledger:
- source-replacement continuation does not fabricate Replay
- continuation does not inflate factual play count
- actual W4-H Failure produces no completion, skip, passive-completion, natural-completion or early-skip taste outcome
- derivation leaves BA4 persistence bytes unchanged

Second, a tightly integrated factual BA4 fixture records:
- `EnqueueOriginKind::User` completion for a Recording
- `EnqueueOriginKind::Radio` completion for the same Recording
- a separate factual Failure

Verified:
- derivation version is explicit
- repeated derivation from the same ledger/config produces the same evidence shape
- derivation is pure and does not mutate BA4
- User derives deliberate-queue + natural-completion interpretation
- Radio derives passive-autoplay-completion interpretation for the same Recording
- Failure retains factual start but fabricates no completion/skip outcome evidence
- derived behavioral observations can be applied to BE2
- RecommendationExposureLog append does not add BA4 listening facts
- exposure does not mutate BE2 or create explicit positive/negative taste
- BE3 consumes BA4-derived history, BE2, exposure and catalogue feature copies
- BE3 does not mutate BA4
- BE3 does not mutate BE2
- BE3 does not mutate catalogue state
- raw generator/provider score contributes only for a generator with explicit calibration
- a deliberately huge uncalibrated raw score does not produce calibrated-score evidence
- a candidate with missing history/taste evidence remains unknown rather than receiving fabricated signals
- same inputs/config/clock produce exactly identical ranking output twice
- every rank explanation evidence item has a non-empty traceable source ID

BA4 remains factual throughout; recommendation policy stays outside the ledger.

## Real libmpv / CUE proof

The final active test #37 is the real runtime journey, not a fake-only substitute.

The retained real proof covers:
- local fixture load through real libmpv
- mpv playlist-entry attribution through the reusable BA2-D sidecar
- serialized seek ownership through the reusable BA2-D sidecar
- logical seek mapped to physical source offset
- user-facing logical track-relative position
- logical slice duration
- Slice A EOF at its `PlaybackWindow` bound
- queue advancement to Slice B
- Slice B as a separate `QueueOccurrence`
- Slice B as a separate Recording identity
- two logical CUE slices sharing one physical source
- no physical-file time leaking into user-facing track-relative time

The real CUE executable passed both direct execution and final CTest.

## Production / scope safety

Production Colosseum was **not modified** by Agent-8-Recovery.

All source/document writes in this recovery pass were under:

`C:\Users\Suprabha\Desktop\Preflight-Architect`

The only production Colosseum path supplied to CMake/runtime was the existing media fixture root for read-only qualification.

No reset, clean, stash, rebase, force-push, branch/tag deletion, commit, or push was performed. Unrelated dirty/untracked work in the shared Preflight checkout was preserved.

## Known caveats / accepted defers

- This is a reference-backend seal, not production Colosseum adoption.
- H6-A intentionally proves read-time MusicBrainz canonicalization rather than rewriting durable stored IDs in place.
- BA5 currently has structural Library/Playlist identity but no display-snapshot ownership; the proof respects that boundary.
- The local `preflight readiness` CLI command was not available on PATH in this session; repository inspection, build, runtime and CTest verification were performed through Brotherhood Desktop Commander instead.
- W4-I / Slice 9 baseline freeze was not started.

## Seal conclusion

W4-H now proves the arrows between the retained backend lanes, including the three composed gaps from the Lead audit, under one active W4-H CTest entrypoint and one fresh 37/37 superbuild sweep.

T-8 is DONE. Slice 9 / W4-I Baseline Freeze is READY for the Lead to start.
