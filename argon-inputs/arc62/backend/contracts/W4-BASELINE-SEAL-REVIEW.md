# Arc 62 Vinyl Wave 4 Baseline Seal Review

Date: 2026-10-07
Agent: Agent-9
Task: T-9 / W4-I Reference Backend Freeze
Production Colosseum status: NOT ADOPTED; read-only during this audit.

## Final verdict

**REPAIR REQUIRED**

Two independent freeze blockers were proved in the current repository.

1. The required fresh W4-H reproduction is red. Fresh configure and full Release build succeeded, but the single final CTest invocation ran 37 registered tests and passed only 36. `vinyl_w4_h_real_cue_journey` failed at the real-libmpv seek-completion assertion: `real seek completion should report logical target time`.
2. BB5 retains durable profile-scoped source memory, but its v1 restore API accepts the profile embedded in the document instead of requiring an expected `ProfileScopeId`. There is no typed `ProfileMismatch` path. A wrong-profile durable source-memory document is therefore not rejected by the reference boundary itself.

Either defect independently blocks an unconditional or caveated seal under the T-9 verdict law.

The audit stopped short of hash freezing. `W4-BASELINE-MANIFEST.md` is intentionally not created because there is no sealable baseline to freeze.

## Responsibility matrix

| Foundational responsibility | Contract / lane owner | Reference implementation | Current classification |
| --- | --- | --- | --- |
| canonical identity | BA0 Domain Contract | `ba0-domain-prototype` | baseline |
| profile identity | BA0 `ProfileScopeId` | `ba0-domain-prototype` | baseline |
| queue ownership / order | BA1 Queue Core | `ba1-queue-core-prototype` | baseline |
| QueueOccurrence identity | BA1 Queue Core | `VinylQueue.*` | baseline |
| enqueue origin | BA1 W4-D extension | `VinylQueue.*` | baseline |
| repeat / shuffle | BA1 + BA2C coordinator intent | BA1/BA2C + BA3 persistence | baseline |
| playback lifecycle | BA2-A | `ba2-playback-session-prototype` | baseline |
| live async fencing | BA2-A/BA2-D + BB5 | playback token + mpv entry ledger + resolver request fence | baseline |
| seek ownership | BA2-A/BA2-D | playback session + `SerializedMpvSeekState` | baseline, current real integration proof red |
| media-time / PlaybackWindow mapping | W4-A / BA2-BB3-BB4 seam | playback transform/window reference code | baseline, current real integration proof red |
| queue/playback coordination | BA2-C / W4-B | `QueueSessionCoordinator` | baseline |
| pending input | BA2-C / W4-B | coordinator + BA3 durable intent | baseline |
| transition facts | BA2-C / W4-B | coordinator transition facts | baseline |
| session persistence | BA3 | `VinylSessionPersistence` | baseline |
| listen history | BA4 | ledger + persistence prototypes | baseline |
| durable PlayInstance identity | BA0 + BA4 | `PlayInstanceId`, BA4 durable rows | baseline |
| Library | BA5 | `VinylLibrary` + personal-state persistence | baseline |
| Playlists | BA5 | `VinylPlaylist` + personal-state persistence | baseline |
| source candidates | BB1 | `SourceCandidateContract` | baseline |
| exact resolution | BB2 | `ExactTrackResolver` | baseline |
| CUE / whole-album slicing | BB3 | `CueWholeAlbum` | baseline; multi-FILE CUE deferred |
| resolved transport | BB4 | `ResolvedPlaybackSource` | baseline |
| async source-resolution session | BB5 | `SourceResolutionSession` | baseline |
| source selection policy | BB5 | `SourceSelectionPolicy/SourceSelector` | baseline |
| fallback / expiry | BB5 | `SourceResolutionSession` | baseline |
| source memory | BB5 | `SourceProfileMemory` | baseline semantics, FAIL profile-bound restore |
| timed text | BC1 | `TimedTextCore` | baseline |
| translation | BC2 | `TimedTextTranslation` | baseline |
| annotation/document identity | BC1 + BC3 | timed-text document ID + annotation meaning plane | baseline |
| signal-path model | BD1 | signal-path prototype | baseline |
| mpv observability | BD2 | mpv observability prototype | baseline |
| recommendation candidates | BE1 | candidate contract prototype | baseline |
| taste state | BE2 | taste state + persistence | baseline |
| history derivation | BE-DERIVATION / W4-G | recommendation derivation prototype | baseline |
| exposure log | BE-DERIVATION / W4-G | exposure record/log semantics | baseline semantics; production DB placement not frozen |
| deterministic ranking | BE3 | deterministic ranker | baseline |
| context graph | BF1 | context graph prototype | baseline |
| related video qualification | BF2 | related-video contract | baseline |
| video resolution integration | BF3 + BB5 | video adapter + common source replacement path | baseline |
| canonical catalogue | BG1 | catalogue client/types | baseline |
| discovery enrichment | BG2 | discovery enrichment prototype | baseline, non-authoritative |
| radio/autoplay sequence generation | BE4 | no frozen reference implementation | accepted DEFER |
| full QML/native facade | BH1 | no frozen full facade | accepted DEFER / production presentation boundary |

No foundational responsibility in this matrix is silently assigned to an unnamed later owner. The two blockers are defects inside named owners, not ownership gaps.

## Identity audit

PASS for the canonical type model:
- `ArtistMbid`, `ReleaseGroupMbid`, `ReleaseMbid`, `ReleaseTrackMbid`, `RecordingMbid`, and `WorkMbid` are distinct strong `Mbid<Tag>` types.
- `RecordingMbid` remains canonical playable-song identity.
- `ReleaseTrackMbid` remains edition/track context and is not collapsed into Recording identity.
- `QueueOccurrenceId` is occurrence identity only.
- `PlayInstanceId` is durable logical play-journey identity only.
- `PlaybackWorkToken` is live playback fencing only.
- source provider/candidate IDs are provenance and source-selection identity only.
- resolver request sequence is async work identity only.
- timed-text document identity is separate from its canonical `RecordingMbid`.
- recommendation exposure ID is separate from the exposed canonical entity.
- provider mappings do not become canonical music identity.
- no Spotify/provider identity was found in canonical stores.
- BG1 redirect handling preserves MusicBrainz authority at read time without silently rewriting old durable IDs.

## Profile and durable personal-state audit

Shared `ProfileScopeId` convergence is complete in current reference code. Searches found no live competing `SessionProfileScopeId`, `ListenProfileId`, or `TasteProfileScopeId` type in the current prototypes.

| Durable state | Version story | corruption | profile mismatch | live engine identity persisted | verdict |
| --- | --- | --- | --- | --- | --- |
| BA3 session | writer v2, reader v1-v2, checked-in genuine v1 fixture | typed | typed | no generation/work token/seek sequence | PASS |
| BA4 history | writer v3, reader v1-v3 | typed | typed | no live work token/generation | PASS |
| BA5 Library/Playlists | writer v3, reader v1-v3 | typed | typed | none | PASS |
| BE2 taste/suppression | writer v2, reader v1-v2 | typed | typed | none | PASS |
| BB5 source memory | canonical text schema v1 | typed `InvalidMemoryState` for malformed input | **missing expected-profile typed rejection** | none | **FAIL** |

BB5 details:
- header embeds `ProfileScopeId`;
- `SourceProfileMemory::fromCanonicalTextV1(const QString &text)` parses that embedded profile and constructs memory from it;
- there is no expected-profile argument;
- `SourceResolutionErrorCode` has no `ProfileMismatch`;
- the session accepts the supplied `SourceProfileMemory` object and has no separate profile binding;
- existing isolation coverage proves two separately constructed profiles stay independent, not that a persisted wrong-profile document is rejected.

Recommendation exposure has schema-v1 record semantics and profile-checked append behavior. Its production database/file location and sync transport are explicitly not frozen, so it is not treated as a frozen durable storage implementation in this audit.

## Dependency / authority-direction audit

The inspected reference direction is otherwise sane:
- BA4 does not mutate BE2 or recommendation state.
- BE3 implementation consumes taste/history/exposure/catalogue/context/discovery as inputs; mutation calls found under BE3 are fixture setup in tests, not ranker authority.
- BA2 playback/session prototypes contain no provider-selection authority.
- BB5 reads BA1 active occurrence and uses the W4-B coordinator replacement path; it does not mutate queue order directly.
- BF3 does not bypass W4-B with an independent occurrence-start path; its test explicitly requires `CoordinatorRestartActive`.
- BA3 persistence contains no durable `generation`, `workToken`, or seek-sequence field.
- BG2 does not own or mutate the BG1 catalogue client.
- BB source code does not own/mutate BG1 canonical catalogue state.
- no QML/QtQml ownership exists in the native reference prototypes.
- BA3/BA4/BA5 display snapshots are explicitly non-authoritative fallback presentation and cannot overwrite BG1 canonical truth.

## Current W4-H executable evidence

The active superbuild includes all retained BA0-BG2 reference implementation objects and registers:
- isolated contract/reference tests;
- the active fake composed journey;
- the real libmpv two-slice CUE journey when the production fixture exists.

Current source inspection confirmed the repaired W4-H paths are present:
- real-libmpv load failure enters coordinator `engineFailed`;
- BB5 receives `playbackFailed` and attempts fallback;
- quit-during-resolve restore invalidates old request identity and late old completion becomes `IgnoredStale`;
- real v1 BA3 fixture exists;
- BA5 is schema v3 with optional non-authoritative display snapshots;
- H6-A redirect/unavailable composition, H6-B document swap, and H6-C BA4 -> derivation/exposure/BE3 composition are wired into the active fake journey;
- stale orphan W4-H helper files identified by Claude are absent.

### Independent W4-I reproduction

Fresh build directory:
`C:\Users\Suprabha\AppData\Local\PreflightBuilds\vinyl-w4-i-agent9-seal-20261007-1933`

Observed toolchain:
- Qt 6.11.1
- Visual Studio 2022
- MSVC 19.44.35223.0
- Windows SDK 10.0.26100.0
- established libmpv prefix
- production Colosseum fixture read-only

Fresh configure: PASS.
Full Release build/link: PASS.
ODR/duplicate-symbol/link-diagnostic scan: no hit.
One final CTest invocation: **36/37 PASS, 1 FAIL**.

Failure:
`vinyl_w4_h_real_cue_journey`
at `real_cue_journey.cpp:1086-1088`:
`real seek completion should report logical target time`.

The test reached real mpv playback and accepted the logical seek, but the observed logical position at seek completion fell outside the frozen ±300 ms assertion. Because the required real journey is red in the fresh final reproduction, prior 37/37 receipts cannot be used to freeze the current baseline.

Full receipts are under `w4-baseline-seal-evidence/`.

## Claude baseline seal criteria

1. **FAIL** — one superbuild configures/builds/links cleanly, but the required one-shot CTest is not zero-failure: 36/37.
2. **PASS** — active fake journey test 36 passed and covers the composed resolve/play/fallback/terminal/history/persistence chain.
3. **FAIL** — real libmpv/CUE journey is registered and executed but fails at logical seek completion; the required real journey therefore does not complete.
4. **PASS** — race gauntlet, including quit-during-resolve restore and stale old request rejection, is present in active fake journey and passed.
5. **PASS** — BA1-active / BA2-bound ownership invariants are covered by coordinator/adversarial/fake integration tests, all green in this run.
6. **PASS** — BA3 genuine v1 restore plus BA4/BA5/BE2 current round-trip/corruption coverage is registered and green.
7. **PASS** — BA4 backward-wall-clock/process-restart identity coverage remains green.
8. **PASS** — MusicBrainz Redirected/NotFound/Unavailable composition through queue/Library/Playlist/history is in H6-A and active fake journey passed.
9. **PASS** — timed-text document-swap fail-closed composition is in H6-B and active fake journey passed.
10. **PASS** — BE3 deterministic identical-input ranking plus traceable evidence and passive-vs-deliberate distinction is covered by BE3/H6-C and passed.
11. **PASS** — frozen-contract wording scan found no surviving unnamed foundational “later owner/policy/outside” hole; surviving later/future language is a named defer, explicit production boundary, or still-correct historical description.
12. **PASS** — current contracts distinguish verified reference backend behavior from production Colosseum adoption; no inspected contract claims the Preflight proofs are already shipped/adopted in production.

Criteria 1 and 3 are sufficient for REPAIR REQUIRED.

## Claude F1-F6 and hygiene recheck

- F1 real-libmpv fallback/history wiring: closure code is present in current source. The new red test is a later real-seek observation failure, not the old missing fallback path.
- F2 quit-during-resolve restore/resume: closed in current active fake journey.
- F3 genuine stored v1 BA3 fixture: closed; checked-in fixture is consumed by current test.
- F4 BA5 display snapshot: closed; schema v3 stores optional non-authoritative `title/artistCredit` fallback and preserves v1/v2 reads.
- F5 stale later-owner wording: closed in current contracts.
- F6 stale production-adoption wording: closed in current contracts.
- stale W4-H blocked handoff: closed; current handoff is DONE.
- orphan/unwired W4-H helpers: closed; only active current helper/source set remains.

## “Later owner / policy” freeze scan

Surviving contract references were classified:
- BA1 autoplay/radio outside BA1 -> accepted BE4 defer with named domain.
- BB3 adjacent same-file gapless qualification -> accepted gapless/crossfade defer.
- BA2B/BA2D/BA4/BA5/BB3/BB4/BD2/BF3 production-adoption language -> explicit production implementation boundary, not a claim of adoption.
- no surviving instance was classified as an unnamed foundational ownership hole.

## Production-adoption claim audit

Current contract language consistently treats production adoption as separate work.
The production Colosseum checkout was read-only for the existing fixture.
The fresh W4-I verification changes no production file.
This review therefore makes no claim that Vinyl is implemented, integrated, shipped, or adopted in production Colosseum.

## Accepted DEFER / exclusion list

These remain non-blocking because they do not remove an owner required for the reference backend’s foundational identity, queue, playback, source, persistence, history, catalogue, timed-text, observability, or deterministic recommendation contracts:

- BE4 Radio / Autoplay session generation: sequence-generation product layer above the normal queue.
- full BH1 QML-facing native service facade: presentation/adoption boundary after backend contracts.
- gapless/crossfade implementation: playback optimization, not correctness of occurrence/track identity.
- non-MusicBrainz canonical content: v1 canonical scope is intentionally MusicBrainz.
- volume/mute/rate/ReplayGain/output-device command implementation: control surface beyond the sealed signal/observability model.
- cross-device sync: transport/sync layer, not local durable semantics.
- scrobbling: external delivery integration, not local factual history ownership.
- optional ListenBrainz connector: external ecosystem integration.
- fingerprint escalation: optional ambiguity escalation after fail-closed exact resolution.
- multi-FILE CUE: broader CUE format support beyond the proved whole-file virtual-slice foundation.
- saving a specific Release as a separate Library semantic: v1 BA5 intentionally saves Artist, ReleaseGroup, Recording.
- persistent Audio|Video preference: later user preference policy, not canonical/source identity.
- learned ranking: deterministic ranker is the v1 reference; learned policy is a later ranking implementation.

Four screen/product exclusions recorded by the W4-H seal repair are preserved with their accepted meaning:

1. Edition label + catalogue number. Future owner: Vinyl catalogue-shape owner. Lift only when catalogue shape gains edition label/catalogue-number fields.
2. Credit-role display order. Future owner: BF1 context-graph contract owner. Confirm whether `CreditRoleKind` declaration order is intentionally the main-roles-first display order.
3. Annotation vote count + artist-verified flag. Future owner: BC3 production-annotation owner. Current BC3 reference intentionally has neither field.
4. Saving a specific edition. v1 exclusion. Current BA5 saves Artist, ReleaseGroup, Recording and album Save targets ReleaseGroup. If revisited, owner is BA5 production-adoption owner.

None of these accepted defers cures or causes the two freeze blockers above.

## Required twelve review answers

1. Does every foundational backend capability have an owner? **Yes.** The matrix has named owners; the defects are inside named BB5/W4-H seams.
2. Are canonical identity boundaries coherent? **Yes.**
3. Do queue/playback/source/persistence/history compose safely? **Not proven in the final required real journey.** The real seek-completion proof is red.
4. Does recommendation ranking consume catalogue/candidate/taste/history/context/discovery without authority inversion? **Yes, in the inspected reference and green tests.**
5. Are provider identities prevented from becoming canonical identities? **Yes.**
6. Is profile state properly separated and composable? **No.** BA3/BA4/BA5/BE2 are profile-bound, but BB5 durable source-memory restore lacks typed expected-profile rejection.
7. Are dependency directions sane enough for production implementation? **Yes in the inspected graph, subject to repairing BB5 profile binding.**
8. Do all required reference prototypes compile/test or have explicit accepted caveats? **No.** They compile, but the required real W4-H journey is red and is not an accepted caveat.
9. Is any unresolved foundational issue still missing a prototype/reference owner? **No unnamed owner remains, but two owned foundational defects remain.**
10. Are BE4/BH1 and other accepted defers correctly excluded from the baseline? **Yes.**
11. Are all Claude seal criteria satisfied or explicitly accepted with a justified caveat? **No. Criteria 1 and 3 fail.**
12. Does final W4-H executable evidence actually prove the composed backend rather than only isolated boxes? **No in the current final reproduction.** Fake composition is green, but the mandatory real libmpv/CUE proof is red.

## Smallest required repairs

### Repair A — W4-H real CUE / BA2-D seek-attribution integration
Owner: W4-H real-libmpv/CUE integration owner, with BA2-D/W4-A seek/time-mapping ownership consulted as needed.

Smallest repair:
- trace why `finishSeekIfReady()` observes a logical position outside the required ±300 ms target window after the real exact mpv seek;
- determine whether the defect is backend time mapping/seek attribution or a nondeterministic harness event/sampling race;
- repair the owning seam without weakening the frozen logical-time invariant merely to make the test green;
- reproduce a fresh Release superbuild with the real CUE journey completing and one 100% green CTest sweep.

### Repair B — BB5 durable source-memory profile binding
Owner: W4-C / BB5 Source Resolution Session owner.

Smallest repair:
- make durable source-memory restore require an expected `ProfileScopeId` (or an equivalent profile-bound restore factory);
- add a typed `ProfileMismatch` error;
- reject a syntactically valid v1 source-memory document whose embedded profile differs from the expected host profile;
- add a focused wrong-profile restore test and preserve deterministic v1 round-trip/corruption tests;
- ensure a source-resolution session cannot silently consume restored memory from another profile.

No feature expansion is required for either repair.

## Freeze state

**REPAIR REQUIRED**

The Arc 62 Vinyl reference backend baseline is **not frozen**.
No final SHA-256 baseline manifest has been issued.
After the two bounded repairs, W4-I must run a new fresh final verification before any hashes are frozen.
Production Colosseum remains unmodified and unadopted.

