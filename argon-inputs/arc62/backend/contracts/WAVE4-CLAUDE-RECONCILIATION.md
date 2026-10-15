# Arc 62 Vinyl - Wave 4 Claude Reconciliation

Date: 2026-10-06
Source review: `claude-brainstorm-review-post-wave3/CLAUDE-REVIEW-RESPONSE.md`
Lead verdict: **ACCEPTED WITH OWNERSHIP CLARIFICATIONS**

## Decision

The provisional four-slice Wave 4 is superseded.

Claude's central finding is accepted: the backend has strong isolated lanes but lacks the orchestration and cross-lane state needed to prove the actual Play journey. Freezing after only BE3 + audit would be premature.

Wave 4 is therefore redefined as nine small slices, W4-A through W4-I, with the end-to-end Play journey as the real seal.

BE3 moves late in the wave, after its missing inputs exist.

## Independent Preflight confirmation

Preflight re-inspected the current repository after reading Claude's full review.

Confirmed directly:

- `PlaybackLoadInput` contains only resource + request headers. There is no playback window.
- `QueueSessionCoordinator` has no `restartActive` seam.
- its input provider is synchronous `optional<PlaybackLoadInput> inputFor(...)`.
- BA4 rejects append/observation time that moves backward.
- `TimedTextDocumentId` does not exist.
- `PlayInstanceId` does not exist.
- `EnqueueOrigin` does not exist.
- the backend currently has SessionProfileScopeId, ListenProfileId, BA5 ProfileScopeId, and TasteProfileScopeId for the same host-profile concept.
- Playlist has profile ID, playlist ID, provenance, and items, but no durable user-facing name metadata.
- no BG/backend `Redirected` outcome exists.
- fallback is repeatedly assigned to BB/later policy while BB1-BB4 implement no fallback owner.
- the Recommendation Brain explicitly models artist, album/release-group, and recording affinities, so BE1 being Recording-only is narrower than the written recommendation architecture.

These confirmations are source inspection only. No new builds were needed to decide the Wave 4 shape.

## Accepted CHANGE findings

### Playback/media-time seam

Accept C1/C3/C9 and the unit cleanup.

Wave 4 will add a playback window and a recording-time transform before freeze.

Domain-facing time above the concrete mpv bridge should converge on integer milliseconds. The mpv/libmpv boundary may convert to seconds where required by the engine API.

CUE slices, resume, source switches, history, and lyrics must observe track/recording-relative time rather than raw physical-file time.

### Coordinator ownership

Accept C2 and ADD-2/ADD-3.

The coordinator becomes the only legal owner for restarting the active occurrence with new transport.

It will expose transition facts and an explicit waiting-for-input state.
Transition facts are baseline backend facts, not the full BH1 facade.

### Queue/session context

Accept C4 and ADD-4.

Queue occurrences gain descriptive enqueue origin/context without changing QueueOccurrenceId identity or BA1 ordering semantics.

Repeat/shuffle policy is decided before freeze because persistence and history need its durable shape.

### Durable listening identity and clocks

Accept C5/C6.

BA4 may not use wall time as its ordering authority.

Wave 4 will introduce a durable PlayInstanceId and continuation semantics across same-occurrence source restarts.

Ownership clarification: PlayInstanceId is a coordinator/session-journey fact handed to BA4. It is not a replacement for BA2 PlaybackWorkToken, and BA2 generation remains the live async fence.

Wall time remains descriptive. Host-supplied monotonic elapsed time/revisions own ordering/accounting.

Continuous qualified listening is coalesced rather than stored as one row per sample pair.

### Recommendation entity scope

Accept C7 with one constraint.

BE1 candidates become CanonicalEntityRef restricted to Artist, ReleaseGroup, and Recording.

Only Recording candidates are directly playable. Artist and ReleaseGroup candidates are recommendation/discovery entities and must resolve through their normal catalogue/product flow before queueing.
### Timed-text identity

Accept C8/C9.

A timed-text document gets durable document/revision identity. Translation and cue-linked annotation must carry that identity and fail/degrade safely on mismatch.

Unknown recording-time offset remains unknown. It must not become a guessed synchronized lyric position.

### Shared profile scope

Accept C10 as a deliberate BA0 convergence change under BA0's shared-type rule.

One host-owned ProfileScopeId will replace lane-local aliases at freeze, with compatibility/migration proof.

### Durable schema evolution

Accept C11, playlist metadata, ResolvedTrackSource expiry, BD2 playback-instance binding, and durable formats for BA4/BA5/BE2.

BA3 v2 must read supported older versions and upgrade in memory rather than discard user queues on every additive schema change.

## Accepted ADD findings

Accept ADD-1 through ADD-7.

The missing source-selection/resolution/fallback owner is foundational and becomes W4-C.

Coordinator transition facts are foundational and become W4-B.

Waiting for a source is an explicit coordinator state, not BA2 lifecycle state.

Durable personal-state formats are baseline requirements because the user cannot reconstruct them after corruption.
MusicBrainz identity-over-time is part of "MusicBrainz owns identity"; redirect/merge handling, last-known non-authoritative display snapshots, batch lookup, and representative release selection belong before freeze.

Taste derivation/exposure is required before BE3. BE3 will not invent those inputs inside the ranker.

## Deferred findings

Accepted as explicit post-baseline/product gaps unless Hemanth later overrides them:

- BE4 Radio / Autoplay.
- full BH1 QML-facing facade, except transition facts moved into the coordinator baseline.
- gapless/crossfade implementation. Keep the port extensible and document prepare-next as a future seam.
- music with no MusicBrainz identity in Vinyl canonical state.
- volume/mute/rate/ReplayGain application/output-device command implementation.
- cross-device sync and scrobbling.
- optional ListenBrainz connector.
- fingerprint escalation.
- multi-FILE CUE.
- saving a specific Release as a separate Library semantic.
- persistent Audio|Video mode preference.
- learned ranking.

## Wave 4 ordering

Numeric worker mapping:

1. W4-A Playback Window + Recording-Time Model
2. W4-B Coordinator Repair + Transition Facts
3. W4-C Source Resolution Session
4. W4-D Queue + Session Schema v2
5. W4-E Durable Personal State
6. W4-F Catalogue + Timed-Text Identity
7. W4-G Recommendation Inputs + BE3
8. W4-H Superbuild + End-to-End Play Journey
9. W4-I Baseline Freeze

Initial parallel launch group: **1, 4, 5, 6**.
Slice 2 has no architecture dependency, but is queued behind the first launch group only to preserve the four-worker initial parallel plan. It may start as soon as a worker slot is available.

Slice 3 requires Slice 2's restart/transition interface and Slice 1's resolved-playback interface to avoid concurrent BB4/transport edits.

Slice 7 requires the public outputs of 4 and 5, and consumes 6 catalogue features.

Slice 8 waits for 1-7 and is the real seal candidate.

Slice 9 waits for 8 and performs the actual freeze/hashes/deferred-owner audit.

## Write-lock clarification

Claude's proposed A/D/E/F parallelism is preserved by assigning disjoint canonical write sets:

- W4-A: BA2-B/BA2-D/BB4 playback-window and time-bound transport surfaces.
- W4-D: BA0/BA1/BA3 queue/profile/schema-v2 surfaces.
- W4-E: BA4/BA5/BE2 durable personal-state surfaces.
- W4-F: BG1 + BC1/BC2/BC3 catalogue/timed-text identity surfaces.

When one slice needs another slice's new type, it must consume a posted public-interface checkpoint rather than edit the other slice's files.

Cross-cutting transport metadata that must land in a file owned by another slice is requested through the room and integrated by that owner.

## Seal doctrine

Wave 4 is not complete because nine workers say DONE.

The Lead must independently inspect handoffs and the final W4-H journey evidence.

W4-I may return REPAIR REQUIRED. If it does, repair the foundational issue before calling the prototype/reference baseline finished.

The baseline is sealed only when the Claude criteria are represented by executable evidence or an explicit accepted defer.
