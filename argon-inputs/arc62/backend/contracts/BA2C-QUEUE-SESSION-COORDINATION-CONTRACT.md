# Vinyl Backend BA2-C - Queue / Session Coordination Contract

Status: Wave 4 repaired reference prototype
Date: 2026-10-06
Owner: Arc 62 Vinyl / W4-B
Depends on: BA1 Queue Core, BA2-A Playback Session Lifecycle, BA2-B Engine Adapter

## Purpose

BA2-C is the deterministic authority that joins BA1's active queue occurrence to BA2's live playback lifecycle.

BA1 remains the sole owner of queue order and active `QueueOccurrenceId`.
BA2 remains the sole owner of `PlaybackWorkToken`, generation, seek fencing, and lifecycle state.
BA2-C coordinates those authorities without creating a second queue cursor or a second playback state machine.

Wave 4 repairs four missing baseline facts:

1. a queue-checked same-occurrence replacement door;
2. typed transition facts with explicit causes;
3. honest coordinator-level pending-input intent;
4. explicit repeat policy.

## Identity law

The identities remain separate:

- `RecordingMbid` identifies the canonical recording;
- `QueueOccurrenceId` identifies one insertion of that recording in BA1;
- `PlaybackWorkToken = QueueOccurrenceId + generation` identifies one live BA2 async generation.

Provider/source/video identity is never canonical identity and is not stored in BA2-C.

Every path that binds BA2 to playback must preserve:

`BA2 active occurrence is none OR BA2 QueueOccurrenceId == BA1 active QueueOccurrenceId`.

## Public coordinator state

### Pending input

```cpp
struct CoordinatorPendingInput {
    QueueOccurrenceId occurrenceId;
    bool playWhenReady;
};
```

Pending input is coordinator state, not a BA2 `PlaybackState`.

It means:

> BA1 has this exact active occurrence and the coordinator still has a playback intent, but a usable `PlaybackLoadInput` is not available yet.

Therefore BA2 may honestly remain `Idle` while the coordinator is awaiting input.

A user stop clears pending input. A queue-active change clears the old pending input. If the successor also lacks input, a new pending value is created for that exact successor.

### Repeat policy

```cpp
enum class CoordinatorRepeatMode {
    Off,
    Queue,
};
```

Repeat is an EOF policy owned by BA2-C.

- `Off`: BA1's normal non-wrapping `advance()` is preserved.
- `Queue`: only an accepted current EOF may wrap deterministically to the first BA1 queue occurrence.

Skip, remove, Play Now, activate, and other queue operations do not implicitly wrap.

The representation is intentionally one-to-one with W4-D `DurableRepeatMode { Off, Queue }`.

## Legal same-occurrence replacement door

The public replacement API is:

```cpp
QueueSessionStatus restartActive(
    const QueueOccurrenceId &expectedOccurrenceId,
    const PlaybackLoadInput &input);
```

It fails closed unless all applicable fences hold:

1. BA1 has an active occurrence;
2. BA1 active ID exactly equals `expectedOccurrenceId`;
3. BA1 active lookup agrees with that ID;
4. any current BA2 binding belongs to that same ID;
5. any current pending-input intent belongs to that same ID;
6. replacement is armed by either a current BA2 binding or pending-input intent.

The sixth fence deliberately prevents a late async result from resurrecting playback after the user stopped. A normal explicit restart after stop still uses `start()`, not a stale replacement completion.

On success:

1. the old same-occurrence BA2 binding is stopped if one exists;
2. pending input is cleared;
3. BA2-B begins the exact current BA1 occurrence with the supplied input;
4. BA2-A creates a fresh generation/work token;
5. old load/position/seek/error/EOF callbacks remain stale under BA2's existing fences;
6. the resulting BA2 binding still names BA1's exact active occurrence.

BA2-C does not rank, resolve, retry, or choose the supplied input.

## Async pending companion seam

W4-C may explicitly arm a source-resolution wait with:

```cpp
QueueSessionStatus awaitInput(
    const QueueOccurrenceId &expectedOccurrenceId,
    bool playWhenReady);
```

This is also fail-closed on exact BA1 active identity.

It records only occurrence identity plus playback intent and emits `AwaitingInput`.
It does not stop a still-valid current transport by itself.
That permits an async same-occurrence replacement to resolve while an old transport is still playing.

If the queue changes or the user stops before the async result returns, the pending value is invalidated. A late `restartActive` for the old occurrence then fails closed.

## Transition facts

BA2-C returns typed backend facts with the operation status.

```cpp
struct OccurrenceStarted {
    QueueOccurrenceId occurrenceId;
    PlaybackWorkToken workToken;
    bool playWhenReady;
};

enum class OccurrenceEndCause {
    Eof,
    UserSkip,
    Removed,
    Stopped,
    Failed,
    Replaced,
};

struct OccurrenceEnded {
    QueueOccurrenceId occurrenceId;
    OccurrenceEndCause cause;
};

struct QueueExhausted {};

struct AwaitingInput {
    QueueOccurrenceId occurrenceId;
    bool playWhenReady;
};

using CoordinatorTransitionFact = std::variant<
    OccurrenceStarted,
    OccurrenceEnded,
    QueueExhausted,
    AwaitingInput>;
```

These are backend facts, not QML/UI events.

Stable cause names are:

- `eof`
- `user_skip`
- `removed`
- `stopped`
- `failed`
- `replaced`

The facts are sufficient for a later BA4 caller to distinguish natural completion, explicit skip, stop, playback failure, active removal, and same-occurrence source/video replacement without BA2-C owning BA4 persistence.

## Operation semantics

### Explicit start

If BA1 is inactive and non-empty:

1. BA1 activates its first occurrence;
2. BA2-C requests already-resolved input for that exact active occurrence;
3. if input exists, BA2 begins it and emits `OccurrenceStarted`;
4. if input is absent, BA2 stays Idle, BA1 remains active, coordinator pending input is set, and `AwaitingInput` is emitted.

Missing input is therefore no longer falsely reported as a stopped session or rolled back BA1 cursor.

If BA1 already has an active occurrence:

- a live same-occurrence BA2 lifecycle is left alone;
- an Idle or terminal same-occurrence session may be explicitly started again through the provider;
- a mismatched BA2 binding fails closed.

### Stop

`stop()` remains playback-only:

- BA1 active occurrence and queue order stay intact;
- BA2 binding is cleared;
- pending input is cleared;
- an open playback/pending journey emits `OccurrenceEnded { stopped }`.

A late replacement completion cannot restart the stopped occurrence through `restartActive` because replacement is no longer armed.

### User skip

`skip()` is the explicit coordinator skip path.

It calls BA1 `advance()` exactly once, emits `OccurrenceEnded { user_skip }` for the old coordinated occurrence, and starts or awaits input for BA1's successor.

Skip never consumes repeat policy.

If skip deactivates the tail, `QueueExhausted` is emitted.

### Active removal

BA1 remains authoritative for removal continuity:

- middle active removal selects BA1's successor;
- tail active removal selects BA1's predecessor;
- final active removal deactivates BA1.

BA2-C ends the old coordinated occurrence with cause `removed`, clears its pending input, and starts or awaits the BA1-selected replacement.

No second successor algorithm exists in BA2-C.

### Play Now / explicit active replacement

When a queue mutation deliberately replaces the active occurrence, BA2-C retires the old coordinated context with cause `replaced`, then starts or awaits the new exact BA1 active occurrence.

Non-active edits that leave the active `QueueOccurrenceId` unchanged do not restart BA2 and do not emit synthetic end/start facts.

### Playback failure

An accepted current BA2 failure emits:

`OccurrenceEnded { cause = failed }`.

BA1 is not advanced.
BA2-C does not choose fallback or retry policy.
A W4-C resolver may later arm pending input and call `restartActive`.

## EOF and repeat

EOF remains the only automatic completion path.

For an engine EOF callback:

1. BA2-A validates the exact `PlaybackWorkToken`;
2. a real state change to Ended is required;
3. the token occurrence must still equal BA1 active occurrence;
4. BA1 `advance()` is called exactly once;
5. `OccurrenceEnded { eof }` is emitted;
6. the next BA1 active occurrence is started or put into pending input.

Duplicate EOF is harmless because the old token is stale after the old binding is cleared/replaced.

### Repeat Off

At tail:

- BA1 becomes inactive;
- BA2's terminal binding is cleared;
- no first-item load occurs;
- `QueueExhausted` is emitted.

There is no implicit wrap.

### Repeat Queue

At tail:

1. normal BA1 `advance()` first reaches inactive;
2. BA2-C explicitly activates BA1's first queue occurrence;
3. that occurrence starts or awaits input.

For a one-item queue, the wrapped `QueueOccurrenceId` is the same, but BA2 is still deliberately restarted so a fresh generation/work token is minted. Old callbacks cannot mutate the new generation.

## Pending-input durability projection

BA2-C does not depend on BA3 and does not persist itself.

Its public values project one-to-one into the W4-D durable coordinator shape:

```text
CoordinatorRepeatMode::Off   -> DurableRepeatMode::Off
CoordinatorRepeatMode::Queue -> DurableRepeatMode::Queue

CoordinatorPendingInput {
    occurrenceId,
    playWhenReady
}
->
SessionPendingInputIntent {
    occurrenceId,
    playWhenReady
}
```

No BA2 generation, `PlaybackWorkToken`, seek sequence, source/provider identity, or engine-local state is part of this durable projection.

## Error vocabulary

BA2-C exposes stable names for:

- `none`
- `empty_queue`
- `missing_playback_input` (legacy vocabulary; normal absence now becomes pending input)
- `queue_mutation_failed`
- `playback_mutation_failed`
- `active_occurrence_mismatch`
- `replacement_not_armed`

Queue and playback diagnostics remain visible through detail strings rather than being silently reclassified.

## Explicit non-goals

BA2-C contains no:

- provider/source ranking;
- source candidate discovery;
- exact-track resolution;
- fallback ordering;
- expiry policy;
- QML or BH1 UI/event bus;
- BA3 storage ownership;
- BA4 persistence;
- recommendation derivation;
- scrobbling;
- autoplay/radio policy;
- concrete mpv ownership.

Shuffle remains BA1/W4-D materialized queue order and is not reimplemented here.

## Prototype

Reference implementation:

`backend/ba2c-queue-session-coordination-prototype/`

Owned source:

- `QueueSessionCoordinator.h`
- `QueueSessionCoordinator.cpp`
- `test_queue_session_coordinator.cpp`
- `test_w4b_coordinator_adversarial.cpp`
- `CMakeLists.txt`

The prototype compiles the BA0/BA1/BA2-A/BA2-B sources directly and uses Qt Core only.

## Wave 4 acceptance proof

The native harnesses cover:

1. correct current `restartActive` succeeds;
2. stale/mismatched `restartActive` fails closed;
3. same-occurrence restart creates a fresh BA2 generation;
4. stale callbacks from the replaced generation cannot mutate the new one;
5. delayed replacement pending + user skip invalidates the old request context;
6. active removal while awaiting input carries exact successor + intent;
7. stop while awaiting input clears pending and prevents late resurrection;
8. pending input preserves both true/false `playWhenReady`;
9. double EOF cannot advance twice;
10. repeat Off exhausts without wrap;
11. repeat Queue wraps deterministically;
12. one-item Queue repeat restarts with a fresh generation;
13. queue exhaustion emits a typed fact;
14. EOF, skip, removal, stop, failure, and replacement causes are distinguishable;
15. after every adversarial operation, BA2 binding is either none or exactly BA1 active.

## Verification

Final verification evidence is recorded in `W4-B-HANDOFF.md` and the append-only Wave 4 agent room.

Production Colosseum is not modified by this slice.
