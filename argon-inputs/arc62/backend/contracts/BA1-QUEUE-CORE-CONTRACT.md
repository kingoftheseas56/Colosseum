# Vinyl Backend BA1 - Queue Core Contract

Status: W4-D queue-context/shuffle convergence complete and locally verified
Date: 2026-10-06
Owner: Arc 62 Vinyl
Depends on: BA0 Domain Contract

## Purpose

BA1 defines Vinyl's presentation-independent logical queue.

It consumes BA0 canonical identity and introduces one new load-bearing distinction:

> queue occurrence identity is not recording identity.

The same Recording MBID may appear more than once in one queue. Every insertion is its own independently addressable occurrence.

BA1 owns queue order, the active queue cursor, descriptive enqueue origin carried by each occurrence, and materialized reversible shuffle state. It does not own audio playback, source resolution, persistence, history derivation, autoplay/radio generation, recommendation ranking, repeat EOF policy, QML, or the current Vinyl mock.

## Basis

BA1 is grounded in:

- BA0 Domain Contract;
- Feishin's full logical queue above mpv and generated per-insertion `_uniqueId`;
- Feishin's stale-work rule binding delayed work to the exact active occurrence;
- Nuclear's Play Now / Play Next / Add to Queue behavior and persisted current-index precedent;
- Colosseum's existing rule that durable/business state should live below QML.

Feishin and Nuclear are mechanism donors only. BA1 is a clean-room Vinyl-owned contract.

## Core types

### QueueOccurrenceId

Opaque Vinyl queue identity.

Rules:

- non-empty;
- max 128 characters;
- no surrounding whitespace;
- no control characters;
- case-sensitive and preserved exactly;
- not a MusicBrainz identifier;
- not a provider identifier;
- uniqueness is required inside one queue.

BA1 does not freeze how production occurrence IDs are generated. **DEFER owner: Production Colosseum Vinyl queue-adoption owner** may use UUIDs or another collision-resistant scheme when wiring BA1 into production. BA1 only defines the identity boundary.

### QueueOccurrence

One logical occurrence contains:

```text
occurrenceId
recordingMbid
releaseContext?
enqueueOrigin?
```

`recordingMbid` is BA0 canonical playable-song identity.

`releaseContext`, when present, is BA0 `ReleaseTrackCoordinate`.

If release context is present, its Recording MBID must equal the occurrence Recording MBID. Otherwise construction fails with `RecordingContextMismatch`.

### EnqueueOrigin

`EnqueueOrigin` is optional descriptive context attached to a queue occurrence. It never participates in QueueOccurrenceId identity or BA1 ordering.

It contains:

```text
kind = user | album | playlist | radio | restore
batchId
container?
```

`batchId` is an opaque, case-preserving token supplied by the host so one multi-item enqueue action can be recognized downstream without collapsing occurrence identity.

`container`, when present, is either:

- a BA0 `CanonicalEntityRef` for canonical album/artist/etc. context; or
- an opaque playlist identifier boundary, without importing BA5 storage ownership into BA1.

The origin may later be copied into listen/history context and recommendation derivation. BA1 itself does not interpret it as intent policy.

No provider/source object is stored in BA1. BB owns source candidates and exact-source state.

### VinylQueue

Ordered occurrences plus an optional active occurrence ID.

The active cursor is an occurrence identity, not an index. The index is derived.

This is deliberate: insertions, removals and reorders before the active item must not silently retarget the active item.

## Queue operations

### append

Append a new occurrence to the tail.

Does not implicitly activate it.

### insert

Insert at an explicit queue index in the range `0..size`.

Does not implicitly activate it.

### playNow

Queue semantics only. No audio starts in BA1.

- with an active occurrence: insert immediately after the active occurrence and make the new occurrence active;
- without an active occurrence: insert at the head and make the new occurrence active.

The old active occurrence remains in the queue before the new active occurrence. This preserves queue history/ordering rather than deleting state as a side effect.

BA2 will decide how a resulting active-cursor transition maps to mpv.

### playNext

- with an active occurrence: insert immediately after active;
- without an active occurrence: insert at the head;
- never changes the active occurrence.

### remove

Remove exactly one occurrence by `QueueOccurrenceId`.

Removing one duplicate Recording MBID must not affect another occurrence of the same Recording.

If the removed occurrence was active:

1. select the successor now occupying the same index when one exists;
2. otherwise select the predecessor;
3. if the queue became empty, active becomes none.

This is a queue-cursor continuity rule, not a playback command.

### move

Move exactly one occurrence by occurrence ID to a destination index in the final queue order.

The active occurrence remains the same occurrence even if its index changes.

### clear

Remove all occurrences and clear active state.

### clearAfterCurrent

Requires an active occurrence.

Keep the active occurrence and everything before it. Remove all following occurrences.

If there is no active occurrence, return `NoActiveOccurrence` instead of guessing an anchor.

### activate

Set the active cursor to an existing occurrence.

### deactivate

Clear active cursor without altering queue order.

### advance

Pure queue cursor semantics:

- requires an active occurrence;
- active item with a successor -> activate successor;
- active final item -> active becomes none;
- inactive or empty queue -> `NoActiveOccurrence`.

BA1 deliberately does not interpret an inactive queue as "start from the beginning." BA2 may explicitly activate the first occurrence when a playback session starts. This prevents a second `advance()` after queue exhaustion from accidentally wrapping to item one.

### shuffle

Shuffle is a materialized queue reorder, not a hidden playback permutation.

Rules:

- the host injects a 64-bit seed; production should use fresh entropy per shuffle while harnesses may use fixed seeds;
- when an occurrence is active, only items after the active occurrence are shuffled, so already-played/visible history and the active occurrence stay in place;
- when no occurrence is active, the whole queue may be shuffled;
- BA1 remembers the full original occurrence-ID order before the first materialized shuffle;
- `unshuffle()` restores that remembered order by occurrence identity;
- the active occurrence remains the same QueueOccurrenceId across shuffle/unshuffle;
- ordinary mutations keep the remembered original-order set coherent so the queue remains reversible;
- BA3 persists both the visible shuffled order and remembered original order, so restore never reshuffles.

Repeat remains coordinator-owned EOF policy. Autoplay/radio sequence generation remains outside BA1.

## Error vocabulary

BA1 defines:

- `None`
- `InvalidOccurrenceId`
- `DuplicateOccurrenceId`
- `OccurrenceNotFound`
- `IndexOutOfRange`
- `RecordingContextMismatch`
- `NoActiveOccurrence`
- `InvalidEnqueueOrigin`
- `InvalidShuffleState`

Errors are stable snake_case at the prototype boundary.

## Mutation semantics

Every mutating API returns `QueueMutationStatus`:

```text
ok
changed
error
```

A successful no-op is allowed where the requested state is already true, for example:

- `deactivate()` when already inactive;
- `clear()` when already empty;
- moving an occurrence to its current index;
- clearing after current when nothing follows.

Unknown occurrence IDs and invalid indices are not silent no-ops. They are typed failures.

## Invariants

After every successful mutation:

1. occurrence IDs are unique;
2. each occurrence carries one BA0 Recording MBID;
3. optional release context agrees with that Recording MBID;
4. active ID is absent or names exactly one existing occurrence;
5. queue order is explicit and deterministic;
6. Recording MBID duplication is legal;
7. optional enqueue origin remains descriptive and cannot replace occurrence/Recording identity;
8. when shuffled, remembered original order is a duplicate-free permutation of the current occurrence IDs;
9. provider/source state is absent from BA1.

## Explicit non-goals

BA1 does not define:

- mpv state;
- play/pause/seek;
- source candidates;
- selected source;
- provider mappings;
- queue persistence;
- cross-device sync;
- history/listen events;
- playlist persistence;
- repeat EOF policy;
- autoplay/radio sequence generation;
- recommendation ranking or taste derivation;
- UI labels;
- QML models;
- current mock layout.

These belong to downstream lanes.

## BA2 handoff

BA2 Playback Session consumes:

- ordered `QueueOccurrence[]`;
- `QueueOccurrenceId`;
- active-cursor transitions;
- BA0 Recording identity.

BA2 may react to a queue transition by loading/stopping mpv, but it must not replace occurrence identity with Recording MBID.

Any asynchronous playback work must be bound to the active occurrence identity/generation so stale work cannot land on a later occurrence.

## Prototype

Reference prototype:

`backend/ba1-queue-core-prototype/`

It compiles BA0 directly from the sibling BA0 prototype rather than copying BA0 types.

Files:

- `VinylQueue.h`
- `VinylQueue.cpp`
- `test_vinyl_queue.cpp`
- `CMakeLists.txt`

Qt Core only. No QML, networking, persistence, provider, database, or mpv dependency.

## Acceptance

BA1 is accepted when the native harness proves:

1. the same Recording MBID may occur twice under distinct occurrence IDs;
2. removing one duplicate leaves the other;
3. moving one duplicate moves only that occurrence;
4. Play Next inserts relative to active without changing active;
5. Play Now inserts relative to active and changes active to the new occurrence;
6. clear-after-current preserves active and removes only following entries;
7. unrelated insert/move operations preserve active occurrence identity;
8. removing active deterministically selects successor, then predecessor, then none;
9. invalid occurrence IDs, duplicate occurrence IDs and invalid indices are typed failures;
10. release context remains attached and a Recording/context mismatch fails closed;
11. advance follows the frozen cursor semantics;
12. EnqueueOrigin kind, optional container, and batch identity survive occurrence construction without affecting occurrence identity or queue ordering;
13. fixed seed + identical queue state produces identical materialized shuffle order;
14. shuffle preserves active occurrence and already-visible history, while unshuffle restores the exact remembered original order;
15. remembered original order remains coherent across supported queue mutations and can be restored from persistence;
16. provider/source/mpv/UI concepts are absent from the prototype.

The W4-D BA1 harness was rebuilt and passed on Qt 6.11.1/MSVC 19.44 on 2026-10-06. Production Colosseum adoption is a later state.
