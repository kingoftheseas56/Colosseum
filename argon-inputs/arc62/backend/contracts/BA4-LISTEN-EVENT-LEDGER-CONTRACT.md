# Vinyl Backend BA4 - Listen Event Ledger Contract

Status: W4-E hardened and verified, including durable schema v3  
Date: 2026-10-06  
Owner: Arc 62 Vinyl, Wave 4 Slice 5  
Depends on: BA0 Domain Contract, BA1 Queue Core, BA2 Playback Session, BA2-B Engine Adapter

## Purpose

BA4 defines factual local listening history around the exact BA2 playback lifecycle.

It records what was observed. It does not decide what should play, what source should win, what should be recommended, what should be scrobbled, or what should sync.

The contract is intentionally append-oriented and profile-scoped.

## Frozen upstream ownership

BA4 consumes without modifying:

- BA0 canonical Recording MBID identity;
- BA1 QueueOccurrenceId and QueueOccurrence;
- BA2 PlaybackWorkToken, including exact occurrence plus playback generation;
- BA2-B's rule that engine callbacks retain the exact token that created the work.

No incompatibility requiring BA0, BA1, BA2-A, or BA2-B mutation was found.

The ownership split remains:

- BA1 owns queue order and active occurrence;
- BA2 owns playback/session lifecycle;
- BB owns source candidate and exact-source policy;
- BA3 owns durable queue/session persistence;
- BA4 owns factual listen-event history and local listening-time accounting;
- BE may later consume history as recommendation input, but BA4 never produces recommendation scores;
- tracker/scrobble integrations remain downstream consumers, never BA4 authorities;
- QML remains presentation only.

## Read-only production evidence

Current Colosseum was inspected only as architecture evidence.

The useful existing patterns are:

1. ActivityPlaybackTracker qualifies time only when wall time and media advancement agree, rejects pauses/stalls/rewinds/oversized gaps, and resets its sampling baseline across discontinuities.
2. ActivityPlaybackTracker::discontinuity() explicitly discards the seek/load bridge instead of calling it watched time.
3. ActivityStore persists immutable semantic facts into a profile-owned ledger rather than mutating a single aggregate truth.
4. ProfileStoreRuntime rebinds ProfileActivity when the profile changes.
5. tst_profile_activity_isolation proves the prior profile store is destroyed/rebound and its activity does not leak into the next profile.

BA4 reuses those mechanisms conceptually. It does not reuse the current account/QML schema and does not modify production Colosseum.

## Donor lesson

The Nuclear donor review established a useful event vocabulary:

- started;
- paused;
- resumed;
- seeked;
- finished/skipped/stopped/failed;
- source invalidation/change;
- derived listening intervals.

The donor's artist/title history fingerprint is rejected.

BA4 uses MusicBrainz Recording MBID plus exact QueueOccurrenceId context. BA2 PlaybackWorkToken/generation remains the live stale-callback fence, while durable listen identity is the host-owned BA0 PlayInstanceId.

No Nuclear implementation code is copied.

## Event identity

Every BA4 event carries one immutable ListenContext.

A ListenContext contains:

- shared BA0 ProfileScopeId;
- ListenSessionId;
- BA1 QueueOccurrence;
- BA2 PlaybackWorkToken for live observation fencing;
- durable BA0 PlayInstanceId;
- optional continuationOf PlayInstanceId for same-occurrence transport restarts;
- optional W4-F LastKnownDisplaySnapshot, explicitly non-authoritative.

Therefore every live event preserves canonical RecordingMbid and QueueOccurrenceId while separating the live BA2 generation fence from durable play-journey identity.

ListenContext::create() rejects a QueueOccurrence whose occurrence ID does not equal the PlaybackWorkToken occurrence ID.

A generation of zero is rejected.

This prevents callers from pairing a canonical recording with an unrelated asynchronous playback token.

## Profile scope

ListenEventLedger is constructed for one immutable shared BA0 ProfileScopeId.

An event from another profile is rejected before append.

The prototype exposes:

- append through ListenEventRecorder;
- ordered read-only access to events;
- derived total qualified listening time.

It exposes no update, replace, reorder, or delete operation.

Durable privacy reset/retention policy is intentionally not invented here. Those are storage/privacy integration concerns, not reasons to make factual rows mutable.

## Append order

Each accepted event receives a profile-ledger sequence number.

Sequence numbers are:

- monotonic;
- contiguous in the isolated prototype;
- independent of QueueOccurrenceId and RecordingMbid;
- ordering evidence only, never canonical media identity.

Observed wall time is descriptive and may move backward. Append order is owned by the ledger sequence; listening-time accounting is owned by host-supplied monotonicElapsedMs rather than wall time.

## Event vocabulary

BA4 defines nine event kinds.

Lifecycle/discontinuity facts:

- start;
- pause;
- resume;
- seek;
- skip;
- stop;
- failure;
- completion;
- source_change.

Accounting fact:

- listening_span.

The nine lifecycle/discontinuity facts remain semantically distinct. A skip is not rewritten as stop, playback failure is not rewritten as user stop, completion is not inferred from position, and source change is not treated as a new Recording identity.

## Start semantics

start() means the first factual consuming observation for one live BA2 playback generation and one durable PlayInstanceId.

It:

1. validates profile, descriptive wall timestamp, monotonic elapsed time, position, and rate;
2. appends start;
3. installs that exact ListenContext as BA4's current observation context;
4. opens a sampling baseline at the supplied monotonic elapsed time/position/rate.

Starting a different exact token replaces BA4's observation context without inventing a terminal fact for the old one.

This is deliberately fail-closed toward undercounting.

If a prior lifecycle disappeared without a factual stop/skip/failure/completion observation, BA4 does not fabricate one merely to make history look tidy.

Starting the same exact token twice is rejected as an invalid transition.

## Pause and resume

pause():

- requires the exact active PlaybackWorkToken;
- qualifies any final continuous interval up to the pause observation;
- appends pause;
- clears the sampling baseline;
- stops listening-time accounting.

Samples received while paused append nothing and add zero listening time.

resume():

- requires the exact active token;
- requires a paused BA4 context;
- appends resume;
- opens a fresh sampling baseline at the resume position.

No interval bridges a pause.

## Qualified listening-time law

BA4 does not equate timer ticks with listening.

While consuming, consecutive observations qualify one listening_span only when all of these hold:

- exact active PlaybackWorkToken still matches;
- rate remains unchanged across the interval;
- monotonic elapsed delta is positive and at most 30 seconds;
- media position advances;
- rate is positive.

For a qualifying pair:

contentEquivalentElapsedMs = mediaDeltaMs * 1000 / rateMilli

qualifiedMs = min(monotonicElapsedDeltaMs, contentEquivalentElapsedMs)

The ledger appends only qualifiedMs. Adjacent qualified pairs for the same live token are coalesced into one continuous listening_span row until a discontinuity closes that run.

This means:

- a stalled media position contributes zero;
- a rewind contributes zero unless represented as a seek boundary and followed by fresh forward playback;
- an oversized sampling gap contributes zero;
- a rate transition contributes zero for the transition bridge;
- fast playback counts host monotonic elapsed listening time rather than inflated media time;
- repeated samples cannot manufacture more listening time than monotonic elapsed time.

Rejected continuity bridges reset the baseline and fail closed toward undercounting.

## Seek discontinuity

seek() carries both factual positions:

- fromPositionMs;
- toPositionMs.

If playback is consuming, BA4 may qualify the continuous interval ending at fromPositionMs.

It then appends seek and resets the baseline to toPositionMs.

The distance between from and to is never counted as listening time.

This holds for forward seeks and rewinds.

## Source-change discontinuity

BA4 uses ListenSourceTraceId only as bounded opaque provenance.

It is not:

- ProviderId;
- candidate ranking;
- exact-source authority;
- canonical media identity;
- a scrobble destination.

sourceChanged():

1. optionally qualifies continuous time up to the transition;
2. appends source_change with prior and next opaque source trace when known;
3. resets the sampling baseline at the current media position.

Repeating the same source trace is a no-op and does not manufacture a source-change event.

W4-H additionally freezes one narrow post-failure fallback seam: `fallbackSourceChangedAfterFailure()` is valid only after BA4 has closed its active context with `failure()`, only when the immediately preceding ledger fact is that `Failure`, and only to append the factual `SourceChange` from the failed opaque source trace to the replacement opaque source trace. It does not reopen consuming state or weaken terminal semantics; the replacement playback must still call `start()` with its fresh BA2 token.

If source replacement causes BA2 to issue a fresh playback generation, the following start() installs that new token and durable PlayInstanceId. When it is the same QueueOccurrence continuation, continuationOf links the new PlayInstanceId to the replaced play instance so history derivation does not fabricate an unrelated replay.

## Terminal facts

skip(), stop(), failure(), and complete() are distinct terminal observations.

Each:

- requires the exact current token;
- optionally qualifies the final continuous interval;
- appends its own event kind;
- closes BA4's active observation context.

BA4 does not advance BA1.

BA4 does not change BA2 state.

BA4 does not infer completion from a percentage threshold.

## Exact stale-work fence

Every mutating observation after start carries the BA2 PlaybackWorkToken.

BA4 compares the full token:

- QueueOccurrenceId;
- playback generation.

If either differs from the active ListenContext, the observation returns stale_context and performs zero append/accounting mutation.

The harness proves both:

1. an old occurrence for the same Recording MBID cannot land on a newer occurrence;
2. an old generation of the same QueueOccurrenceId cannot land on a fresh retry generation.

This is the history-side continuation of BA2's exact async identity invariant.

## Source and provider separation

A BA4 row may describe an opaque source transition because that transition is a fact of the listening session.

It does not answer:

- whether the source was the best candidate;
- whether the provider was preferred;
- whether the source should be retried;
- whether another provider should replace it;
- whether a source should affect recommendation ranking.

Those remain owned by BB/BE policy.

## Recommendation separation

BA4 contains no:

- score;
- rank;
- affinity;
- recommendation weight;
- discovery reason;
- recommendation candidate selection.

Future recommendation code may read BA4 events as evidence.

It must perform ranking in its own layer.

## Tracker and scrobble separation

BA4 contains no external tracker credentials, service identity, delivery state, retry state, or provider-specific scrobble policy.

A later tracker adapter may translate accepted local events into its own protocol.

That adapter may not rewrite BA4's local factual identity or become the source of BA4 truth.

## Failure behavior

Malformed identifiers, positions, timestamps, and rates are typed failures.

Cross-profile events fail before append.

Stale tokens fail before append.

Duplicate same-source transitions are successful no-ops.

Continuity uncertainty such as:

- a sample gap over 30 seconds;
- a rate transition;
- non-advancing position;
- same-time repeated sample;

resets the sampling baseline and contributes zero time.

The bias is deliberate: undercount uncertain intervals rather than invent listening.

## Prototype

Reference implementation:

backend/ba4-listen-event-ledger-prototype/

Files:

- VinylListenEventLedger.h;
- VinylListenEventLedger.cpp;
- test_vinyl_listen_event_ledger.cpp;
- CMakeLists.txt.

The prototype compiles directly against the frozen BA0, BA1, and BA2-A prototype sources.

It uses Qt Core only.

## Deterministic harness coverage

The native harness proves:

1. invalid profile/session/source trace inputs are rejected;
2. QueueOccurrence and PlaybackWorkToken mismatch is rejected;
3. every event retains RecordingMbid, QueueOccurrenceId, generation, profile, and listen-session identity;
4. start emits exactly one factual row;
5. advancing samples create bounded qualified listening spans;
6. pause closes time exactly once;
7. repeated paused samples add zero rows and zero time;
8. resume begins from a fresh baseline;
9. seek counts only the pre-seek continuous interval and discards the jump;
10. source change is distinct, preserves before/after provenance, and resets continuity;
11. repeating the same source is a no-op;
12. two queue occurrences containing the same RecordingMbid remain independent;
13. an old occurrence callback is rejected as stale with zero mutation;
14. skip closes its context;
15. stop is distinct from skip;
16. playback failure is distinct from explicit stop and skip;
17. completion is distinct from stop, failure, and skip;
18. profile A ledger rejects profile B context with zero mutation;
19. rate-change bridge contributes zero;
20. stable 2x playback counts host monotonic elapsed time, not doubled media time;
21. sampling gap over 30 seconds contributes zero;
22. rewind seek discards the rewind distance;
23. profile-ledger sequence is append-ordered and contiguous;
24. every required lifecycle/discontinuity kind is inspectable;
25. an old generation of the same QueueOccurrenceId is stale after retry.

The main accounting fixture reaches its expected totals solely through qualified spans. No test injects an aggregate listen duration directly.

## Durable ledger format

W4-E freezes a versioned local durable representation through `VinylListenEventPersistence`.

- schema: `colosseum.vinyl.listen-event-ledger`;
- writer version: 3;
- reader accepts supported versions 1 through 3 and upgrades supported v1/v2 state in memory;
- v3 adds the distinct factual `failure` terminal kind; historical v1/v2 rows remain readable without reclassifying their existing terminal facts;
- unknown fields fail closed inside a declared version;
- malformed/corrupt JSON returns typed persistence errors;
- canonical compact JSON is deterministic for one ledger sequence;
- durable rows store ProfileScopeId, ListenSessionId, QueueOccurrence identity/context, PlayInstanceId, optional continuationOf, optional non-authoritative LastKnownDisplaySnapshot, ledger sequence, descriptive wall time, monotonic elapsed fact, positions, qualified duration, and bounded source trace;
- BA2 PlaybackWorkToken and playback generation are deliberately absent from durable bytes.

A simulated process restart proves that the same QueueOccurrenceId can return to BA2 generation 1 without colliding in durable history because the later play carries a different PlayInstanceId.

## Verification

Verified locally on 2026-10-06:

- Qt: 6.11.1 MSVC 2022 x64;
- compiler: MSVC 19.44.35223;
- Windows SDK: 10.0.26100.0;
- CMake configure: PASS;
- vinyl_ba4_listen_ledger compile/link: PASS;
- vinyl_ba4_listen_ledger_harness compile/link: PASS;
- direct harness exit: 0;
- CTest: 2/2 PASS: live ledger contract plus durable persistence/migration contract;
- production Colosseum: read-only, no BA4 changes made there.

The Preflight convenience command 'preflight readiness' was not available on this machine's current PATH, so it is not claimed as a verification signal. Toolchain capability was instead proven by the successful native configure/build/test above.

## Acceptance mapping

Wave 3 Slice 2 acceptance is satisfied as follows:

- RecordingMbid + QueueOccurrenceId/session context: immutable ListenContext on every event;
- required lifecycle facts distinguishable: dedicated event kinds;
- repeated samples cannot bridge seek/pause: baseline reset rules plus harness proofs;
- stale old occurrence cannot land: exact PlaybackWorkToken fence;
- append-oriented and profile-scoped: immutable profile ledger plus append-only sequence and no edit/delete API;
- no recommendation/scrobble authority: absent from code contract and explicitly outside ownership;
- Qt 6.11.1 deterministic native harness: configure/build/CTest verified.

## Production adoption boundary

This prototype is not production Colosseum integration.

The durable format is frozen in this reference prototype. Production Colosseum adoption must choose the concrete profile-owned file/database placement without changing these domain semantics.

Production adoption must preserve:

- exact BA2 token at observation time;
- canonical Recording MBID from the QueueOccurrence;
- profile isolation;
- append-oriented factual rows;
- discontinuity-safe listening-time accounting;
- no QML playback authority;
- no recommendation or scrobble policy inside BA4.

Retention/privacy reset, account sync transport, and external tracker delivery remain separate adoption/product decisions. The BA4 storage schema itself is no longer deferred.
