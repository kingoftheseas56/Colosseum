# Vinyl Backend BB4 - mpv Playback Adapter

Status: W4-A verified locally against Qt 6.11.1; bounded transport/timing handoff and isolated libmpv probes green
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BB4 is the source-to-playback seam after Vinyl has already selected and resolved a source.

It translates a BB-owned resolved source into BA2-B PlaybackLoadInput. It does not discover,
rank, accept, retry, or fall back between candidates, and it does not own playback lifecycle.

Dependency direction:

    BB source discovery / exact source policy
      -> ResolvedTrackSource
      -> BB4 transport adaptation
      -> PlaybackLoadInput
      -> BA2 PlaybackEngineAdapter
      -> BA2-D tokenized mpv bridge
      -> MpvQt / libmpv

## Frozen ownership

BB4 consumes without changing:

- BA0 canonical/provider identity;
- BB1 source candidate provenance and source kinds;
- BB2 exact-track acceptance semantics;
- BA2-A playback lifecycle identity;
- BA2-B PlaybackLoadInput, PlaybackWorkToken, and PlaybackSeekRequest;
- BA2-D concrete mpv token/event attribution;
- BD2 runtime audio/device evidence.

Production Colosseum is read-only for this slice.

## ResolvedTrackSource

ResolvedTrackSource means the caller has already selected the source and produced current playable
transport material.

It retains:

    RecordingMbid
    SourceCandidateKey
    SourceKind
    SourceResolutionPath
    ResolvedPlaybackResourceKind
    resource
    requestHeaders
    optional PlaybackWindow
    optional recordingTimeOffsetMs
    optional resolvedAtEpochMs
    optional expiresAtEpochMs

The canonical Recording and source-candidate key remain facts above the engine seam. They are not
copied into PlaybackLoadInput.

Before that identity is stripped away, toPlaybackLoadInput requires the expected RecordingMbid and
fails with RecordingMismatch when it does not equal the resolved source RecordingMbid. This closes
the accidental cross-wiring case where a valid source for recording A could otherwise be paired
with a queue occurrence for recording B.

This is an identity continuity check, not a second BB2 matcher. BB4 does not independently repeat
BB2 exact-track matching. The producer of a ResolvedTrackSource owns the proof that this is the
selected exact source.

## Playback-window and recording-time law

`PlaybackWindow` remains transport geometry. It is copied unchanged into `PlaybackLoadInput` after
the Recording fence succeeds. It never becomes Recording identity, source identity, or queue
occurrence identity.

For BB3 CUE / whole-album sources, `createForExactVirtualSlice(...)` consumes the already-exact
`ExactVirtualTrackSlice` and converts its physical `startMs` / optional `endMs` into the validated
BA2-B `PlaybackWindow`. BB4 does not reinterpret CUE identity or rerun BB2.

`recordingTimeOffsetMs` is separate from the physical playback window. Its signed semantics are:

    recordingMs = trackRelativeMs + recordingTimeOffsetMs

It exists for recording alignment across transports such as videos with intros or alternate
masters. Unknown remains unknown. BB4 never guesses an offset from duration, URL, provider, or
playback-window bounds.

`resolvedAtEpochMs` and `expiresAtEpochMs` are volatile transport freshness metadata. Known expiry
requires a known non-negative resolution timestamp and must be strictly later than it. Unknown
expiry is legal and does not mean permanent. Freshness metadata is not persisted as canonical media
identity.

## Source origin versus final transport

BB1 SourceKind describes source provenance/access shape. BB4 keeps it as provenance.

The final mpv resource is modeled separately:

- LocalPath;
- FileUrl;
- HttpUrl.

Resolution path is also separate:

- Direct;
- ExternalResolver.

This separation is deliberate. An external resolver may produce an HTTP URL or a file resource.
The resolver/provider identity does not become the final transport identity, and an HTTP URL does
not erase the fact that an external resolver produced it.

## Resource validation

BB4 fails closed at the source/playback boundary:

- local paths must be absolute;
- file resources must be valid file:// URLs;
- HTTP resources must use http:// or https:// and include a host;
- request headers are legal only on HTTP resources.

After shape validation, BB4 delegates the resource/header safety boundary to BA2-B
PlaybackLoadInput::create.

Therefore BA2-B remains authoritative for:

- resource length/control-character bounds;
- header count bounds;
- HTTP-token header names;
- header-value line-break/control-character rejection.

BB4 preserves the typed BA2-B validation code when that boundary rejects input.

## Header law

Headers are transport material only.

They do not imply:

- source-provider authority;
- candidate ranking;
- canonical Recording identity;
- exact-track acceptance;
- retry/fallback priority;
- account ownership.

Two resolved sources with different provider/resolver provenance may intentionally map to
identical PlaybackLoadInput when their final locator and headers are identical.

Production adoption must preserve current MpvItem behavior:

- header-bearing loads install validated http-header-fields;
- every plain load clears prior http-header-fields before loading;
- headers from one source must never leak into a later plain load.

Resolved headers may contain ephemeral access material. BB4 defines no persistence for them and
does not turn their contents into provider or canonical identity.

## BA2 identity boundary

BB4 creates no playback token and accepts no playback token.

Its only engine-facing output is PlaybackLoadInput. The expected RecordingMbid must match before
that transport-only value can be produced, so canonical identity cannot be silently cross-wired
at the point where provider/candidate facts are intentionally dropped.

The caller must enter playback through BA2-B PlaybackEngineAdapter::beginOccurrence, which creates
the exact PlaybackWorkToken before issuing the engine load.

All play, pause, seek, stop, load completion, EOF, and failure traffic remains owned by BA2.
BB4 has no command path that can send a naked seek or untagged mpv lifecycle mutation.

BA2-D remains responsible for preserving the work token through concrete mpv load attribution and
the complete PlaybackSeekRequest through physical seek completion.

## Playback failure

Playback failure is not source-selection policy.

When mpv fails the current source:

1. BA2-D reports bounded engine evidence under the exact work token.
2. BA2-B/BA2-A transition the matching lifecycle to Failed.
3. BB4 performs no automatic reload, source re-ranking, candidate mutation, or fallback.

W4-C's source-resolution session may explicitly select or re-resolve another source and create a
new `ResolvedTrackSource`. That new source must re-enter through the W4-B coordinator restart seam
and normal BA2 work-token identity. BB4 itself never ranks, retries, or falls back.

## mpv adoption shape

BB4 does not create another mpv bridge.

BA2-D already owns the concrete command/event adaptation and proves:

- tokenized load attribution;
- exact seek-request completion attribution;
- stale callback rejection;
- raw mpv failure evidence without provider interpretation.

BB4 supplies only validated transport material to that verified seam.

The current production MpvItem was inspected read-only. Its behavior is compatible:

- loadFile() clears http-header-fields, then issues loadfile ... replace;
- loadFileWithHeaders() installs validated header fields, then issues the same replace load;
- Colosseum disables mpv's ytdl hook because Colosseum resolves its own sources.

## Observability boundary

BB4 does not populate Signal Path or device evidence.

BD2 remains the owner of source/runtime/output observations. A source provider's claims and a
resolved URL cannot manufacture codec, device, exclusive-mode, or bit-perfect certainty.

## Explicit non-goals

BB4 does not implement:

- candidate discovery;
- BB2 exact-track matching;
- candidate/source ranking;
- provider fallback;
- queue mutation or advance;
- playback session state;
- token generation;
- seek ownership;
- source/session persistence;
- listen-event history;
- recommendation policy;
- device/signal-path observability;
- QML or current Vinyl mock presentation;
- production Colosseum mutation.

## Prototype

Reference prototype:

backend/bb4-mpv-playback-adapter-prototype/

Files:

- ResolvedPlaybackSource.h;
- ResolvedPlaybackSource.cpp;
- test_mpv_playback_adapter.cpp;
- test_cue_window_handoff.cpp;
- mpv_resource_probe.cpp;
- CMakeLists.txt.

The core BB4 library uses Qt Core plus the frozen BA0, BB1, BA1, BA2-A, and BA2-B prototype
sources. The optional isolated mpv probe links the locally installed libmpv and consumes a
read-only Colosseum fixture when those dependencies are present.

## Acceptance

BB4 is accepted when Qt 6.11.1 native evidence proves:

1. local paths, file URLs, and HTTP URLs preserve validated transport material;
2. source/resolver provenance remains separate from final transport shape;
3. a resolved source cannot produce PlaybackLoadInput for a different expected RecordingMbid;
4. canonical Recording and candidate/provider provenance do not enter PlaybackLoadInput;
5. optional PlaybackWindow survives ResolvedTrackSource -> PlaybackLoadInput unchanged;
6. a real BB3 ExactVirtualTrackSlice can become a bounded ResolvedTrackSource without losing start/end;
7. optional signed recordingTimeOffsetMs remains source metadata, with unknown remaining unknown;
8. optional resolvedAt/expiresAt metadata obeys the same strict future-expiry law as BF3;
9. invalid resource shapes, timing metadata, playback windows, and unsafe headers fail closed;
10. the mapped input enters playback only through BA2 and receives the BA2 work token;
11. old load and seek callbacks remain stale after replacement;
12. current playback failure reaches BA2 Failed without BB4 ranking, retry, or fallback;
13. no queue, persistence, QML, recommendation, or observability ownership leaks into BB4;
14. libmpv accepts a BB4-mapped local resource and emits FILE_LOADED;
15. production Colosseum remains unchanged.

## Native verification

W4-A qualification on 2026-10-06:

- Qt package: C:/Qt/6.11.1/msvc2022_64;
- compiler: MSVC 19.44.35223;
- Windows SDK: 10.0.26100.0;
- configure/build: PASS;
- vinyl_bb4_mpv_playback_adapter compile/link: PASS;
- vinyl_bb4_mpv_playback_adapter_harness: PASS;
- vinyl_w4a_cue_window_handoff: PASS;
- vinyl_bb4_mpv_resource_probe against read-only Colosseum tiny-audio.m4a: PASS;
- CTest: 3/3 PASS, 0 failed;
- the CUE handoff fixture proves 10987..60000 ms BB3 bounds reach PlaybackLoadInput unchanged;
- source timing tests prove explicit known recording offsets/freshness and fail-closed invalid expiry;
- production Colosseum was not modified.

BB4 is verified for the W4-A reference boundary. Source selection, fallback, and expiry-driven
re-resolution belong to W4-C; production adoption remains a separate state.