# Vinyl Backend BF3 - Video Playback Adapter Contract

Status: W4-C converged reference implementation
Date: 2026-10-06
Owner: Arc 62 Vinyl / W4-C

## Purpose

BF3 is now a thin video transport adapter inside the general BB5 source-resolution lifecycle.

It no longer owns a parallel asynchronous orchestration universe.

The boundary is:

```text
BF2 qualified RelatedVideoCandidate
  -> BF3 VideoPlaybackSelection
  -> BB5 SelectableSourceCandidate(Video)
  -> BB5 SourceResolutionRequest
  -> BF3 external resolver dispatch (for example yt-dlp)
  -> BF3 ResolvedVideoTransport
  -> W4-A ResolvedTrackSource
  -> BB5 current-request / expiry / fallback checks
  -> W4-B restartActive
  -> BA2 / mpv
```

## Ownership

BF2 owns:

- related-video relationship classification;
- evidence strength;
- direct Audio | Video eligibility.

BB5 owns:

- asynchronous source-session identity;
- request sequence;
- stale success/failure rejection;
- source selection;
- ordered fallback;
- expiry re-resolution;
- profile memory;
- same-occurrence continuation;
- W4-B replacement orchestration.

BF3 owns only:

- conversion of a qualified BF2 video mapping into a BB5-selectable video candidate;
- dispatch of a BB5 video request to an external video resolver port;
- validation/adaptation of external resolver output into W4-A ResolvedTrackSource.

BA1 still owns the active QueueOccurrenceId.

BA2 still owns PlaybackWorkToken, playback generation, seek identity, and engine callback freshness.

W4-B remains the only legal same-occurrence replacement door.

## Identity law

RecordingMbid remains canonical playable-song identity.

ProviderId and ProviderItemId remain provenance only.

A YouTube/provider video ID that looks like an MBID is still an opaque provider item ID.

Audio -> Video and Video -> Audio keep:

- the same RecordingMbid;
- the same QueueOccurrenceId for same-occurrence replacement.

They may create a fresh BA2 generation through W4-B restartActive.

## Video selection

`VideoPlaybackSelection::create(expectedRecording, candidate)`:

1. requires the expected RecordingMbid to equal BF2 candidate RecordingMbid;
2. delegates direct-switch admission to BF2;
3. projects the qualified mapping into BB5 `SelectableSourceCandidate::fromQualifiedVideo`.

Only BF2 Audio | Video-qualified candidates enter this path.

Weak related-video mappings remain ineligible.

Qualified live/alternate performances remain ineligible unless BF2's direct-switch law explicitly says otherwise.

## General request identity

BF3 defines no `VideoResolutionRequest` sequence of its own.

The external resolver receives the exact BB5 `SourceResolutionRequest`, containing:

- QueueOccurrenceId;
- RecordingMbid;
- SourceCandidateKey;
- SourceKind::ExternalResolver;
- SourceTransportKind::Video;
- source-session sequence;
- source-request sequence;
- resolution reason.

Therefore video and audio async work share the same stale-result discipline.

A newer BB5 request supersedes an older video request without any BF3-local pending state.

## External resolver port

`VideoTransportResolverCommands` exposes:

```cpp
virtual void resolve(const SourceResolutionRequest &request) = 0;
```

`VideoPlaybackAdapter::dispatch(request)` accepts only:

- SourceTransportKind::Video;
- SourceKind::ExternalResolver.

A production implementation may wrap yt-dlp.

The resolver implementation itself is transport machinery. It is not provider identity, canonical identity, relationship evidence, or playback lifecycle state.

## ResolvedVideoTransport

`ResolvedVideoTransport::create(...)` consumes the exact BB5 request and external resolver material:

- resource kind;
- resource locator;
- request headers;
- resolvedAtEpochMs;
- optional expiresAtEpochMs;
- optional recordingTimeOffsetMs.

It validates/adapts that material into W4-A `ResolvedTrackSource`.

The source retains:

- request RecordingMbid;
- request SourceCandidateKey;
- SourceKind::ExternalResolver;
- SourceResolutionPath::ExternalResolver;
- optional freshness;
- optional recording-time offset.

Provider/resolver output remains transport material only.

## Expiry

BF3 validates the structural W4-A freshness law when it creates ResolvedTrackSource.

BF3 does not decide whether a callback is current.

BB5 evaluates the resolver result against the current request and current clock.

If the transport is expired when BB5 receives it:

- it never reaches BA2;
- BB5 issues a fresh request identity;
- the same QueueOccurrenceId and RecordingMbid are retained;
- replacement still waits for a current fresh result.

Unknown expiry remains unknown.

## Stale success and failure

BF3 contains no private pending request to cancel.

Late success is rejected by BB5's request fence.

Late failure is reported to BB5 through the same request identity and rejected if stale.

This applies equally to:

- skip during video resolve;
- remove-active during video resolve;
- stop during video resolve;
- Play Now during video fallback;
- a second explicit Audio | Video request superseding the first.

## Playback handoff

BF3 no longer emits a direct BA2 handoff and no longer calls:

`PlaybackEngineAdapter::beginOccurrence(...)`.

A fresh `ResolvedTrackSource` returns to BB5.

BB5 converts it through BB4 and invokes only:

```cpp
QueueSessionCoordinator::restartActive(
    expectedOccurrenceId,
    playbackLoadInput)
```

This closes the Wave 3 BA1/BA2 cross-wire risk.

## Recording-time semantics

Video may carry optional `recordingTimeOffsetMs`.

Semantics remain W4-A:

`recordingMs = trackRelativeMs + recordingTimeOffsetMs`.

Unknown remains unknown.

An intro, alternate master, or provider video does not gain a guessed lyric alignment.

## Continuation semantics

BF3 does not mint PlayInstanceId.

BB5 carries the optional W4-E continuation ancestor for same-occurrence replacement.

The BA4 caller can therefore link the fresh transport generation into the same logical listening journey instead of recording an unrelated replay.

## Error vocabulary

BF3 exposes:

- none;
- recording_mismatch;
- candidate_not_eligible;
- invalid_resolution_request;
- invalid_resolved_transport.

Stale-resolution and expiry policy are no longer BF3 errors because those lifecycle decisions belong to BB5.

## Security and transport

Resolved URL/header material is untrusted transport input.

W4-A/BB4/BA2 validation remains authoritative.

BF3 does not persist:

- cookies;
- credentials;
- temporary signatures;
- exact resolved URLs as canonical media identity.

## Explicit non-goals

BF3 does not implement:

- a private request sequence;
- a private pending request;
- source ranking;
- source fallback;
- expiry policy;
- BA2 direct begin/restart;
- queue order/advance;
- BA4 history;
- QML;
- yt-dlp installation/update management;
- production Colosseum integration.

## Prototype

Reference implementation:

`backend/bf3-video-playback-adapter-prototype/`

Files:

- VideoPlaybackAdapter.h
- VideoPlaybackAdapter.cpp
- test_video_playback_adapter.cpp
- CMakeLists.txt

The CMake harness compiles the BB5 reference owner directly so BF3 can prove the composed request/replacement seam rather than testing a private duplicate lifecycle.

## W4-C native proof

The Qt 6.11.1 harness proves:

1. RecordingMbid survives BF2 -> BF3 -> BB5 -> resolved video transport;
2. ProviderId/ProviderItemId remain provenance only;
3. weak candidates cannot enter direct playback;
4. qualified live performance remains ineligible for direct switch;
5. BF3 dispatch receives the exact BB5 request;
6. resolver output becomes W4-A ResolvedTrackSource;
7. accepted video transport re-enters BB5 and reaches playback only through W4-B restartActive;
8. a superseded video request is stale under BB5;
9. skip during video resolve invalidates the old result;
10. expired video output never reaches BA2 and causes BB5 expiry re-resolution;
11. optional recordingTimeOffsetMs survives as explicit timing metadata;
12. BA2 bound occurrence remains none or exactly BA1 active after the tested video races.

Production Colosseum remains unchanged.
