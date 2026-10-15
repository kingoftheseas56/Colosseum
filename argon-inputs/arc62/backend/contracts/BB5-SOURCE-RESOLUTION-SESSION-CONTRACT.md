# Vinyl Backend BB5 - Source Resolution Session Contract

Status: W4-C reference implementation
Date: 2026-10-06
Owner: Arc 62 Vinyl / W4-C

## Purpose

BB5 is the missing Vinyl policy/orchestration layer between an active BA1 queue occurrence and a transport that W4-B is allowed to bind to BA2.

The boundary is:

```text
BA1 active QueueOccurrence
  + BB1 source candidates
  + BB2 Exact decisions
  + BF2 qualified Audio | Video mappings
  + profile-scoped source memory
    -> deterministic source order
    -> one fenced asynchronous resolution request
    -> current resolved transport
    -> W4-B restartActive(expectedOccurrenceId, input)
    -> typed fallback / expiry re-resolution / terminal exhaustion
```

BB5 does not create canonical music identity. RecordingMbid remains canonical and QueueOccurrenceId remains occurrence identity.

## Ownership laws

1. MusicBrainz RecordingMbid remains canonical playable-song identity.
2. QueueOccurrenceId identifies the exact queue insertion whose transport is being resolved.
3. Provider IDs, candidate IDs, video IDs, resolver implementation IDs, URLs, torrent IDs, and request sequence numbers are provenance/transport only.
4. BB1 discovers candidates. BB2 decides Exact versus Ambiguous/nonexact. BB5 selects only among already-Exact audio candidates.
5. BF2 decides whether a video is qualified for Audio | Video. BB5 treats a qualified video mapping as another selectable transport candidate without changing RecordingMbid.
6. BA1 still owns queue order and active occurrence.
7. BA2 still owns PlaybackWorkToken, playback generation, seek fencing, and engine callback freshness.
8. W4-B is the only legal same-occurrence transport replacement door.
9. BB5 must never call PlaybackEngineAdapter::beginOccurrence directly for fallback, expiry, or Audio | Video replacement.
10. A provider's arrival order is never selection or fallback authority.
11. Unknown expiry remains unknown. BB5 invents no TTL.
12. A transient resolver/playback failure does not automatically become durable dislike, known-wrong, or known-dead state.

## Selectable candidate gate

BB5 does not accept raw discovery results as playable choices.

### Audio

`SelectableSourceCandidate::fromExactAudio(...)` requires:

- BB1 candidate RecordingMbid equals the BB2 requested RecordingMbid;
- the BB2 manifest is bound to the exact BB1 SourceCandidateKey;
- BB2 returns `Exact` with one selected manifest entry;
- a currently non-retryable BB1 failure is not present.

`Ambiguous`, `Rejected`, and `InsufficientEvidence` fail closed with `exact_resolution_required`.

### Video

`SelectableSourceCandidate::fromQualifiedVideo(...)` consumes the frozen BF2 eligibility predicate.

Only BF2 candidates eligible for direct Audio | Video become selectable. Weak text-search candidates, unqualified candidates, qualified live performances, and unrelated videos remain ineligible.

The resulting candidate uses:

- the same RecordingMbid;
- provider/item provenance projected into SourceCandidateKey;
- SourceKind::ExternalResolver;
- SourceTransportKind::Video;
- refreshable resolution mode.

No provider video ID becomes canonical identity.

## Deterministic source-selection policy

BB5 owns an explicit, injectable deterministic policy. Provider result order is discarded.

Selection is evaluated in this order:

1. remove profile-memory candidates marked known-wrong or known-dead;
2. an explicit user pin wins among the remaining candidates;
3. compare the explicit cross-source-kind order;
4. compare the injected SourceKindScorer result;
5. compare the explicit provider-preference list;
6. use SourceCandidateKey::stableKey only as the final deterministic tie-breaker.

The stable key is a deterministic tie-breaker only. It is never a relevance or quality signal.

### Reference cross-kind order

The reference prototype uses this plain declared list:

1. LocalFile
2. PeerToPeer
3. Http
4. ProviderMediated
5. ExternalResolver
6. Unknown

This list is policy, not identity, and is replaceable.

### SourceKindScorer slot

BB5 does not make provider arrival order a scorer.

`SourceKindScorer` is injected. A production source-kind implementation may use facts specific to that source kind.

The reference scorer is intentionally simple and deterministic for audio:

1. lossless flag;
2. bitrate;
3. sample rate;
4. bit depth.

Higher known values win. Unknown remains zero evidence in this prototype rather than fabricated quality certainty.

Video candidates receive no invented audio-quality score.

The scorer slot exists so a future torrent scorer can consume torrent-lane swarm facts without polluting BB1 root identity/facts or hardcoding torrent logic into the BB5 session owner.

## Profile-scoped source memory

`SourceProfileMemory` is scoped by shared BA0 `ProfileScopeId`.

It can represent:

- one explicit user pin per Recording;
- candidate known wrong;
- candidate known dead/unusable;
- origin of a mark: user or explicitly confirmed runtime evidence.

Rules:

- profile A state never affects profile B;
- provider/candidate identity remains provenance only;
- exclusion marks win over a pin until the mark is explicitly cleared;
- transient resolver/playback failure does not mutate memory;
- callers must explicitly record a user or confirmed-runtime mark;
- source memory does not contain provider credentials/accounts.

### Durable representation

User-authored preference state uses a deterministic versioned text form:

```text
colosseum.vinyl.source-memory|1|<base64url profile token>
pin|<recording-mbid>|<source-provider-id>|<base64url candidate-id>
mark|<recording-mbid>|<source-provider-id>|<base64url candidate-id>|wrong|user
mark|<recording-mbid>|<source-provider-id>|<base64url candidate-id>|dead|confirmed_runtime
```

Rows are canonicalized deterministically by RecordingMbid and SourceCandidateKey.

This is a reference durable shape, not a sync/cloud/account protocol.

## Resolution-session identity

One active source-resolution session is for exactly one BA1 active occurrence.

Every asynchronous request carries:

- QueueOccurrenceId;
- RecordingMbid;
- SourceCandidateKey;
- SourceKind;
- SourceTransportKind Audio|Video;
- SourceResolutionReason;
- monotonically increasing sessionSequence;
- monotonically increasing requestSequence.

A result is current only when:

- the exact request equals the current pending request;
- the request occurrence equals the session occurrence;
- the request Recording equals the session Recording;
- BA1 still has that exact active occurrence; and
- W4-B still has replacement authority for that occurrence through pending input or the same BA2 binding.

Late success and late failure use the same fence.

Audio and video do not have separate freshness universes.

## Begin / waiting behavior

`beginForActive(...)`:

1. reads the exact BA1 active occurrence;
2. deterministically orders selectable candidates using policy + profile memory;
3. creates a fresh source-session sequence;
4. calls W4-B `awaitInput(expectedOccurrenceId, playWhenReady)`;
5. issues one request for the first allowed candidate.

W4-B pending input remains coordinator state. BB5 creates no BA2 lifecycle state.

For a live same-occurrence source or Audio | Video switch, `awaitInput` may arm replacement while the old transport continues playing.

## Result acceptance

`acceptResolved(request, ResolvedTrackSource, nowEpochMs)` first applies the exact request fence.

Then it verifies:

- non-negative clock input;
- exact RecordingMbid continuity;
- exact SourceCandidateKey continuity;
- exact SourceKind continuity;
- current freshness.

An invalid/mismatched resolved transport consumes that attempt and enters normal ordered fallback.

A current expired transport is never loaded.

A current fresh transport is converted through BB4 and is replaced only by:

```cpp
coordinator.restartActive(request.occurrenceId(), playbackLoadInput)
```

The replacement result records `SourceReplacementPath::CoordinatorRestartActive` so the legal path is inspectable.

## Ordered fallback

The attempt chain stores every candidate key that has been attempted.

Fallback is deterministic and finite.

Before dispatching any next asynchronous fallback candidate, BB5 re-arms W4-B `awaitInput(expectedOccurrenceId, playWhenReady)`. This makes the wait explicit and ensures stop/active-change invalidation and BA3 pending-intent projection remain available during fallback after a terminal playback failure.

- resolver failure -> next unattempted allowed candidate;
- current playback failure -> next unattempted allowed candidate;
- invalid resolved transport -> next unattempted allowed candidate;
- an immediately expired resolver result gets one fresh request for the same candidate;
- if that expiry-refresh result is already expired again, BB5 advances to the next unattempted candidate;
- candidates already consumed by the current attempt chain are not resurrected.

There is no implicit retry loop.

When no candidate remains, BB5 returns:

`SourceFallbackTerminal { occurrenceId, recordingMbid, ExhaustedCandidates, lastFailure, attemptedCandidateKeys }`

The terminal result is suitable for the composed W4-H caller to stop/fail/advance honestly. BB5 itself does not invent queue-advance policy after unplayability.

## Expiry and re-resolution

W4-A `ResolvedTrackSource` owns freshness metadata.

`refreshIfExpired(nowEpochMs)`:

- returns NoChange for unknown expiry;
- returns NoChange while known expiry is still in the future;
- fails closed for negative clock input;
- when expired, re-arms W4-B pending input for the same occurrence;
- issues a fresh request identity for the same candidate with `ExpiryRefresh`;
- keeps QueueOccurrenceId and RecordingMbid unchanged;
- replaces playback only through restartActive after a fresh result is accepted.

A successfully refreshed source may expire again later and start another explicit expiry event. The bounded rule applies to each immediate refresh attempt and prevents a resolver from returning already-expired resources forever.

## Playback failure

BA2/W4-B first records the actual current engine failure under the correct PlaybackWorkToken.

W4-B keeps BA1 unchanged and reports `OccurrenceEnded { Failed }`.

Then BB5 `playbackFailed(expectedOccurrenceId)` may advance the same source attempt chain.

The fallback request remains async. If the user stops, skips, removes active, or performs Play Now before the new result returns, the coordinator/request fence rejects the late result.

## Same-occurrence continuation

Fallback, expiry refresh, and Audio | Video replacement do not change QueueOccurrenceId or RecordingMbid.

BB5 accepts an optional W4-E `PlayInstanceId` continuation ancestor at session start and returns it with `SourceReplacementFacts`.

BB5 does not fabricate a new durable play identity itself.

The composed caller may mint the next PlayInstanceId and pass `continuationOf` to BA4 so the transport replacement remains one logical listening journey instead of an unrelated replay.

## BF3 convergence

BF3 no longer owns:

- a private resolution sequence;
- a private pending request;
- separate stale-success/stale-failure state;
- direct BA2 handoff;
- direct `beginOccurrence`.

BF3 consumes BB5 `SourceResolutionRequest`, dispatches it to the external resolver port, and adapts resolver output into `ResolvedTrackSource`.

The exact same BB5 session owns:

- video stale-result rejection;
- video expiry;
- video fallback;
- skip/remove/stop/Play Now invalidation;
- coordinator replacement.

yt-dlp is therefore transport machinery only.

## Race law

After every state-changing callback in the W4-C harness:

`BA2 bound occurrence is NONE OR BA2 QueueOccurrenceId == BA1 active QueueOccurrenceId`.

The native harness explicitly reproduces:

- skip during audio resolve;
- skip during video resolve;
- Play Now during fallback;
- remove-active during resolve;
- stop during resolve;
- old resolver success after fallback;
- old resolver failure after fallback;
- video request supersession;
- video expiry.

## Explicit non-goals

BB5 does not implement:

- QML/BH1;
- catalogue policy;
- BE3 or recommendation ranking;
- radio/autoplay;
- source-provider accounts or credential storage;
- provider networking;
- yt-dlp process parsing;
- production Colosseum integration;
- queue ordering or queue-advance-after-unplayable policy;
- BA4 persistence;
- BA2 generation/seek fencing.

## Prototype

Reference implementation:

`backend/bb5-source-resolution-session-prototype/`

Files:

- SourceResolutionSession.h
- SourceResolutionSession.cpp
- test_source_resolution_session.cpp
- CMakeLists.txt

The prototype consumes the frozen W4-A and W4-B interfaces without modifying them.

## Acceptance matrix

The native Qt 6.11.1 harness proves:

1. normal async resolution succeeds;
2. two Exact candidates are deterministically ordered;
3. input/provider permutation produces the same order;
4. Ambiguous and nonexact BB2 outcomes cannot become selectable;
5. old resolver success is stale;
6. old resolver failure is stale;
7. skip during audio resolution invalidates old work;
8. stop during resolution invalidates old work;
9. remove-active during resolution invalidates old work;
10. Play Now during resolution/fallback cannot cross-wire a result;
11. expiry creates a fresh request;
12. resolver failure falls through to the next allowed candidate;
13. playback failure falls through to the next allowed candidate;
14. exhaustion produces a typed terminal result;
15. retry/fallback is bounded;
16. fallback preserves QueueOccurrenceId;
17. replacement path is W4-B restartActive;
18. no stale replacement can bind BA2 to a non-active BA1 occurrence;
19. video uses the same request fence;
20. Audio -> Video -> Audio preserves RecordingMbid;
21. known-wrong and known-dead memory change selection deterministically;
22. profile memory is isolated by ProfileScopeId;
23. explicit user pin is a deterministic policy input;
24. transient failure does not automatically become permanent memory;
25. source-memory v1 round-trips canonically.

Production Colosseum remains read-only.
