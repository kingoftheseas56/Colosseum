# Vinyl Backend BA2-B - Engine Adapter Contract

Status: verified isolated prototype
Date: 2026-10-06
Owner: Arc 62 Vinyl
Depends on: BA2-A Playback Session Lifecycle
Parent slice: BA2 Playback Session Lifecycle

## Purpose

BA2-B defines the smallest engine-neutral command and callback seam around the frozen BA2-A state machine.

BA2-A remains the lifecycle authority.

BA2-B does not create a second playback state machine. It translates BA2-A lifecycle decisions into engine commands and routes engine observations back into BA2-A with the exact identity that created the work.

## Frozen upstream

BA2-B consumes without modifying:

- BA0 Domain Contract;
- BA1 Queue Core Contract;
- BA2-A Playback Session Lifecycle Contract;
- BA2-A `PlaybackSession`;
- BA2-A `PlaybackWorkToken`;
- BA2-A `PlaybackSeekRequest`.

No incompatibility requiring BA0, BA1, or BA2-A mutation was found.

## Ownership

The ownership split remains:

- BA1 owns queue order and active `QueueOccurrenceId`;
- BA2-A owns logical playback lifecycle;
- BA2-B owns the replaceable engine command/callback boundary;
- BB owns candidate discovery, exact matching, source selection, fallback, and provider policy;
- BA3 owns persistence;
- BA4 owns listen-event history;
- QML remains presentation only.

BA2-B does not decide what should play next.

## Engine command port

`PlaybackEngineCommands` exposes five logical engine operations:

- `load(PlaybackWorkToken, PlaybackLoadInput)`;
- `play(PlaybackWorkToken)`;
- `pause(PlaybackWorkToken)`;
- canonical Wave 4 `seek(PlaybackSeekCommand)`;
- `stop(PlaybackWorkToken)`.

`PlaybackSeekCommand` contains the exact frozen BA2-A `PlaybackSeekRequest` plus the canonical track-relative integer-millisecond target. A legacy `seek(PlaybackSeekRequest)` overload remains only as source-compatibility scaffolding for earlier isolated prototypes; new concrete engines must consume `PlaybackSeekCommand`.

Every command carries lifecycle identity.

A concrete engine bridge must retain that identity for any asynchronous callback produced by the command.

A load command means replace the engine's current media resource with the supplied already-resolved input.

The command interface deliberately returns no playback success verdict.

Load success, state observations, EOF, and failure arrive through the callback side of the adapter. A concrete engine implementation must not infer lifecycle ownership from whatever item happens to be current when an event arrives.

## Load input

BA2-B introduces `PlaybackLoadInput` only because an engine cannot load without receiving transport material.

It contains:

- opaque resource locator;
- zero or more request headers;
- optional validated `PlaybackWindow { startMs, optional endMs }`.

`PlaybackWindow` is physical-media transport geometry, not identity. `startMs` is non-negative. A known `endMs` must be strictly later than `startMs`. Bounded logical duration is `endMs - startMs`.

The resource may represent a local path, file URL, network URL, or another resource form a future engine can consume.

BA2-B does not parse the resource into canonical identity and does not attach:

- provider identity;
- candidate identity;
- matching confidence;
- ranking;
- fallback order;
- canonical Recording identity.

Those decisions stay above this seam.

Request headers are transport material only. Their presence does not imply provider ownership.

The prototype bounds and validates resource/header text so accidental control-character or header-injection payloads do not become part of the frozen example contract.

## Begin/load flow

`PlaybackEngineAdapter::beginOccurrence(...)`:

1. asks BA2-A to begin the exact `QueueOccurrence`;
2. receives the new `PlaybackWorkToken`;
3. issues exactly one engine `load` using that token and injected load input.

The BA2-A generation is advanced before the engine load command is issued.

Therefore any callback from the previous lifecycle that arrives synchronously or asynchronously during replacement is already stale.

BA2-B does not check whether the supplied occurrence is BA1's active occurrence. BA2-C owns that composition rule.

## Load completion and desired play state

The engine reports load completion by calling:

`engineLoaded(token)`

BA2-B first forwards the exact token to BA2-A.

Only when BA2-A accepts the callback and changes Loading into a loaded state does BA2-B issue the matching engine command:

- BA2-A Playing -> engine `play(token)`;
- BA2-A Paused -> engine `pause(token)`.

This preserves play/pause changes made while loading.

A duplicate accepted load callback is a no-op and does not redispatch play/pause.

A stale load callback does not issue any command.

## Play/pause commands

`play()` and `pause()` mutate BA2-A first.

If BA2-A is still Loading, they change `playWhenReady` only and BA2-B sends no premature engine command.

If BA2-A transitions into Playing or Paused after load, BA2-B sends exactly one matching command with the active work token.

Repeated idempotent play/pause requests do not redispatch engine commands.

Engine pause observations are routed back through:

`enginePauseObserved(token, paused)`

and remain protected by BA2-A's exact occurrence/generation fence.

## Seek commands

BA2-B never sends a naked seek target.

Canonical Wave 4 callers use `seek(targetMs)`, where `targetMs` is non-negative track-relative integer milliseconds. BA2-B asks frozen BA2-A to create the `PlaybackSeekRequest`, but the engine receives `PlaybackSeekCommand { request, targetMs }`.

The engine must retain:

- exact `PlaybackWorkToken`;
- per-generation seek sequence;
- exact integer-millisecond logical target.

Seek completion returns the same command identity to:

`engineSeekApplied(command, observedPositionMs)`

where `observedPositionMs` is already track-relative integer milliseconds.

This preserves both stale-generation protection and same-generation seek supersession. An older seek completion cannot overwrite a newer seek.

Frozen BA2-A stores seconds internally. BA2-B contains that implementation detail at its boundary. The compatibility double-seconds overload normalizes through integer milliseconds and is not the Wave 4 domain contract.

## Position and duration observations

Engine position and duration callbacks must carry the exact `PlaybackWorkToken` and canonical track-relative integer milliseconds.

BA2-B exposes integer-millisecond observations and `positionMs()` / `durationMs()`. It converts only at the frozen BA2-A compatibility boundary; concrete engine seconds never become domain time above BA2-D.

BA2-B forwards accepted observations to BA2-A without persistence, progress policy, history emission, or source interpretation.

Unknown timing remains unknown until the engine actually reports it.

## EOF and failure

Engine EOF is routed through:

`engineEnded(token)`

Engine failure is routed through:

`engineFailed(token, PlaybackFailure)`

Both require the exact work token.

A stale EOF or failure performs zero mutation.

A current EOF terminates BA2-A as Ended but does not call BA1 `advance()`.

A current failure terminates BA2-A as Failed and preserves only the supplied bounded failure evidence.

Recovery, source fallback, and next-track policy remain outside BA2-B.

## Stop ordering

Stop has one important ordering rule.

BA2-B copies the current `PlaybackWorkToken`, then clears BA2-A lifecycle state, then issues:

`engine.stop(oldToken)`

This ordering is deliberate.

If an engine emits EOF/error synchronously while processing stop, the callback already targets a cleared lifecycle and is rejected as stale.

This is stronger than issuing engine stop first and only later invalidating session state.

Stop does not mutate BA1.

## Replaceability

The adapter depends only on `PlaybackEngineCommands`.

No concrete engine class is named or included.

A future concrete bridge can map the five commands and seven callback shapes to a native engine without changing BA2-A lifecycle semantics.

The current production player was inspected only as read-only architecture evidence. Production adoption remains BA2-D.

## Callback surface

BA2-B exposes these canonical engine-to-session callback methods:

- `engineLoaded(PlaybackWorkToken)`;
- `enginePauseObserved(PlaybackWorkToken, bool)`;
- `enginePositionObserved(PlaybackWorkToken, qint64 positionMs)`;
- `engineDurationObserved(PlaybackWorkToken, qint64 durationMs)`;
- `engineSeekApplied(PlaybackSeekCommand, qint64 observedPositionMs)`;
- `engineEnded(PlaybackWorkToken)`;
- `engineFailed(PlaybackWorkToken, PlaybackFailure)`.

Compatibility double-seconds overloads remain only for the already-built pre-W4 isolated prototypes
and normalize immediately through integer milliseconds.

Every callback reuses BA2-A identity rather than reconstructing ownership from global engine state.

## Explicit non-goals

BA2-B does not implement:

- queue/session coordination;
- queue advance;
- next-track selection;
- candidate discovery;
- exact-track resolution;
- provider ranking or fallback;
- source persistence;
- queue/session persistence;
- resume restoration;
- listen history;
- scrobbling;
- audio-device/signal-path observability;
- QML registration;
- current Vinyl mock presentation;
- a concrete production playback engine.

## Prototype

Reference implementation:

`backend/ba2-engine-adapter-prototype/`

Files:

- `PlaybackEngineAdapter.h`;
- `PlaybackEngineAdapter.cpp`;
- `test_playback_engine_adapter.cpp`;
- `CMakeLists.txt`.

The prototype compiles directly against the frozen BA0, BA1, and BA2-A prototype sources.

It uses Qt Core only.

## Acceptance

BA2-B is accepted when the deterministic fake-engine harness proves:

1. begin issues one load with the exact BA2-A work token;
2. resolved load input is injected without provider/ranking semantics;
3. loading play/pause changes intent without premature engine commands;
4. current load completion applies the latest BA2-A play/pause intent;
5. duplicate load completion does not redispatch commands;
6. post-load play/pause commands carry the exact token;
7. every canonical seek carries the full `PlaybackSeekRequest` plus exact integer-millisecond logical target in `PlaybackSeekCommand`;
8. older same-generation seek completion is superseded;
9. old-occurrence and old-generation callbacks cannot mutate a newer lifecycle;
10. stale load callbacks cannot dispatch play/pause into the newer lifecycle;
11. same-occurrence retry receives a new generation;
12. stop invalidates BA2-A before issuing engine stop;
13. a synchronous callback emitted during engine stop is stale;
14. EOF reaches BA2-A but does not advance BA1;
15. adapter commands do not mutate BA1 queue state;
16. invalid or non-increasing playback-window bounds fail closed;
17. physical-window mapping is deterministic and bounded logical duration is `endMs-startMs`;
18. recording-time alignment is explicit through optional signed `recordingTimeOffsetMs`, with unknown remaining unknown;
19. canonical seek/position/duration surfaces above the concrete bridge use integer milliseconds;
20. no presentation, concrete-engine, provider, persistence/history, or queue-advance dependency leaks into the prototype.

## Verification

Verified locally on 2026-10-06:

- Qt: 6.11.1 MSVC 2022 x64;
- compiler: MSVC 19.44.35223;
- Windows SDK: 10.0.26100.0;
- CMake configure: PASS;
- `vinyl_ba2b_engine_adapter` compile/link: PASS;
- `vinyl_ba2b_engine_adapter_harness` compile/link: PASS;
- direct harness exit: PASS;
- CTest: 1/1 PASS;
- prohibited dependency scan: zero hits.

The only compile warning was MSBuild's warning about using a Windows temporary directory for incremental build output. It did not affect compilation or tests.

Production Colosseum remained read-only and unchanged.

## Wave 4 dependency boundary

W4-A extends this already-proven adapter seam without giving it source-selection policy.

Downstream coordination may consume `PlaybackWindow`, `PlaybackTimeTransform`, and the integer-millisecond adapter surface. W4-B owns queue/session restart and transition policy. W4-C owns source resolution/fallback. W4-F may consume recording-time alignment for timed-text synchronization.

Production Colosseum remains a separate adoption state.
