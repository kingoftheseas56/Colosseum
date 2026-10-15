# Arc 62 Vinyl - BA2 Playback Session Lifecycle Handoff

> Continuation status (2026-10-06): BA2 is complete and verified through BA2-D-B3. Wave 2 is sealed locally. The original handoff below is retained as historical pre-BA2-A context; current authority is `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md` plus `backend/README.md`.

Date: 2026-10-06
From: Preflight
Target: fresh Preflight Architect chat
State: BA0 + BA1 locally complete and verified; BA2 not started
Production Colosseum: untouched

## Objective

Implement Arc 62 Vinyl backend slice **BA2 Playback Session Lifecycle** as a mock-agnostic native backend contract and isolated Qt/C++ prototype.

BA2 consumes BA0 canonical music identity and BA1 queue-occurrence semantics.

BA2 must define playback/session lifecycle without stealing ownership from later lanes:

- BA1 owns logical queue order + active occurrence;
- BA2 owns playback-session state/transitions around that active occurrence;
- BB owns source candidates/exact source selection;
- BA3 owns persistence;
- BA4 owns listen-event history;
- QML remains presentation only.

## Required boot / authority

Use the current local Preflight authority first:

- `AGENTS.md`
- `MEMORY.md`
- `CAPABILITIES.md`
- routed engineering / persistence / verification docs as required

Then read completely:

- `arcs/62-vinyl/backend/README.md`
- `arcs/62-vinyl/backend/BA0-DOMAIN-CONTRACT.md`
- `arcs/62-vinyl/backend/BA1-QUEUE-CORE-CONTRACT.md`
- `arcs/62-vinyl/backend/ba0-domain-prototype/`
- `arcs/62-vinyl/backend/ba1-queue-core-prototype/`
- `arcs/62-vinyl/donor-reviews/feishin/DONOR-ASSESSMENT.md`
- `arcs/62-vinyl/donor-reviews/nuclear/DONOR-ASSESSMENT.md`

Inspect current Colosseum references only, do not modify production:

- `qml/AudiobookSession.qml`
- `native/player/mpvitem.h`
- `native/player/mpvitem.cpp`

Colosseum basis inspected for BA0/BA1:
`vinyl` at `e5bd76c638ae19b808eab2371cad70eacce96090`.

## Product law

The backend is **mock-agnostic**.

The current Vinyl HTML/QML mock is a reference consumer, not authority over backend names, stores, projections, or state.

Do not create types such as `ListeningRoomSession`, `NowPlayingCarouselState`, `QuickPicksPlayback`, etc.

BA2 should still make future designs possible, but should describe durable playback capability rather than today's mock composition.

## Frozen upstream contracts

### BA0

MusicBrainz owns canonical identity.

Important BA0 types:

- `RecordingMbid`
- `ReleaseTrackCoordinate`
- other typed MBIDs

Do not mutate BA0 for convenience.

### BA1

Queue identity and Recording identity are separate.

Important BA1 types:

- `QueueOccurrenceId`
- `QueueOccurrence`
- `VinylQueue`

Frozen BA1 behavior:

- duplicate Recording MBIDs are legal when occurrence IDs differ;
- active queue state is keyed by occurrence identity, not stored index;
- Play Next inserts after active without changing active;
- Play Now inserts after old active and activates the new occurrence;
- removing active selects successor, then predecessor, then none;
- clear-after-current requires active occurrence;
- `advance()` requires active occurrence and does not wrap;
- release context must agree with occurrence Recording MBID;
- source/provider/mpv/UI state is absent from BA1.

BA2 must not replace `QueueOccurrenceId` with Recording MBID.

## Donor lessons to preserve

### Feishin

Feishin's strongest BA2-relevant contribution:

- full logical queue belongs to the host, not mpv;
- mpv receives only a tiny current/next playback window;
- asynchronous resume/seek work is bound to the exact queue occurrence;
- if active occurrence changes before delayed work lands, stale work is cancelled/ignored.

The key invariant for BA2:

```text
async playback work
  must be scoped to
active QueueOccurrenceId + generation
```

A delayed seek/load completion must never mutate a later occurrence.

### Nuclear

Useful BA2 lessons:

- changing playback state for the active queue item may restart/re-resolve that item;
- editing non-active queue items must not interrupt current playback;
- queue and player state are distinct;
- richer listen-event history exists, but that belongs to BA4 rather than BA2.

## Current Colosseum playback references

### AudiobookSession

`qml/AudiobookSession.qml` proves an important ownership pattern already used in Colosseum:

- one app-wide playback session;
- hidden `MpvItem`;
- remotes bind to session state;
- session survives surface changes;
- same active identity reopen can be idempotent;
- pending resume seek is applied only after `fileLoaded`;
- explicit seek / source change / EOF are distinct transitions;
- closing really stops the session;
- UI surfaces do not own playback.

Use this as architecture reference, not as a code/template transplant.

### MpvItem

Current native mpv seam exposes relevant capabilities including:

- `loadFile(const QString &)`
- `loadFileWithHeaders(...)`
- `seekExact(double)`
- `pause` property + `pauseChanged`
- `position` property + `positionChanged`
- `duration` property + `durationChanged`
- `fileLoaded()`
- `endFile(QString reason)`

BA2 prototype should remain engine-independent unless the bounded sub-slice explicitly introduces an adapter seam.

## Scope guardrail

BA2 is likely too large for one reliable implementation pass under current Preflight rules.

Before coding, split BA2 into independently verified sub-slices and obey hard conversation-turn boundaries.

Recommended decomposition:

### BA2-A - Session State Machine Contract

No real mpv.

Define presentation-independent session states, active occurrence binding, command/transition semantics, generation fencing, and deterministic tests.

Likely concepts:

- `PlaybackSessionId` if genuinely needed;
- `PlaybackGeneration` or equivalent monotonic fence;
- playback state such as Idle / Loading / Playing / Paused / Ended / Failed, but do not freeze names without reviewing donor + Colosseum evidence;
- active occurrence snapshot/reference;
- position/duration observations if they belong in lifecycle state;
- commands: begin/load active occurrence, play, pause, seek request, stop, next/EOF transition;
- stale callback rejection keyed to occurrence + generation.

This is the recommended first bounded checkpoint.

### BA2-B - Engine Adapter Contract

After BA2-A acceptance.

Define a minimal engine-neutral interface for:

- load current occurrence's resolved source;
- pause/play;
- seek;
- stop;
- engine callbacks: loaded / position / duration / pause / EOF / error.

Do not implement BB source resolution here.

### BA2-C - Queue / Session Coordination

After BA2-A/B acceptance.

Prove:

- BA1 active transition drives BA2 target;
- editing unrelated queue occurrences does not interrupt active playback;
- removing active occurrence switches deterministically to BA1's new active occurrence;
- queue exhaustion ends session without wrapping;
- Play Now causes exactly one active transition;
- stale old-engine callbacks cannot mutate the new active occurrence.

### BA2-D - Colosseum mpv Adoption Plan or isolated adapter proof

Only if still within requested BA2 scope and after earlier checkpoints.

Do not modify production Colosseum unless Hemanth explicitly authorizes production adoption.

## Acceptance themes for BA2 overall

BA2 should eventually prove:

1. playback lifecycle is keyed to QueueOccurrenceId, not just Recording MBID;
2. duplicate occurrences of one Recording are independently playable;
3. active occurrence changes invalidate stale async work;
4. play/pause/seek are deterministic state transitions;
5. seek requests cannot land on a later occurrence;
6. EOF advances through BA1 semantics and does not invent repeat;
7. stop ends playback without deleting/reordering queue unless BA1 is explicitly commanded;
8. failure is typed and scoped to the active generation;
9. non-active queue edits do not restart playback;
10. source/provider details remain outside BA2 until injected through a later/adapter boundary;
11. no QML/current-mock vocabulary leaks into the domain;
12. BA3 persistence and BA4 listen-event history remain separate.

## Known rejected paths

Do not:

- use Recording MBID as queue/session occurrence identity;
- store current queue state solely as an integer index;
- let mpv own the full logical queue;
- let an old `fileLoaded`, seek completion, error, EOF, or other delayed callback mutate a newer occurrence;
- auto-wrap to queue item one when the queue is exhausted;
- put source ranking / provider identity into BA2;
- persist session state in BA2;
- write history in BA2;
- mutate BA0/BA1 because a local implementation is inconvenient;
- derive the backend from the current Vinyl mock.

## BA1 verification evidence

BA1 final state:

- Qt 6.11.1 CMake configure: PASS
- MSVC compile/link: PASS
- CTest: 1/1 PASS
- deterministic harness: 97 direct assertions
- invariant checks across 9 mutation scenarios
- invariants cover unique occurrence IDs, active cursor validity, Recording/release-context agreement
- zero prototype hits for QML, QObject, QVariant, mpv, ProviderId/ProviderItem, QuickPick, ListeningRoom, carousel, lyrics, artwork
- production Colosseum untouched

Implementation bug caught and repaired during BA1:
Qt 6 `QVector/QList::resize()` instantiated default construction on a shrink path; BA1 uses explicit tail erase instead of weakening non-default-constructible `QueueOccurrence`.

Adversarial semantic correction:
`advance()` does not auto-start an inactive non-empty queue. BA2 must explicitly activate/start the first occurrence when beginning a session.

## Durable files

Backend authority:
`arcs/62-vinyl/backend/README.md`

BA0:
`arcs/62-vinyl/backend/BA0-DOMAIN-CONTRACT.md`
`arcs/62-vinyl/backend/ba0-domain-prototype/`

BA1:
`arcs/62-vinyl/backend/BA1-QUEUE-CORE-CONTRACT.md`
`arcs/62-vinyl/backend/ba1-queue-core-prototype/`

Rolling checkpoint:
`recaps/2026-10-06-vinyl-backend-ba0.md`

## Repository state / caution

Preflight primary checkout is intentionally dirty/diverged with unrelated work.

Do not reset, clean, stash, mass-format, or absorb unrelated files.

BA0/BA1 are local Preflight artifacts and are not committed/pushed from this chat.

Production Colosseum `vinyl` worktree remained untouched:
`e5bd76c638ae19b808eab2371cad70eacce96090`
with only pre-existing untracked `experiments/`.

## Exact next action

Start a fresh Preflight chat and say:

```text
@Preflight Architect

Continue Arc 62 Vinyl backend with BA2 Playback Session Lifecycle.

First read completely:
arcs/62-vinyl/backend/HANDOFF-BA2-PLAYBACK-SESSION-LIFECYCLE.md

Treat that handoff as the current BA2 entry point, then re-check current local authority and repository state before acting.

BA0 and BA1 are frozen unless BA2 proves a real incompatibility.

Because BA2 crosses the Preflight oversized-slice threshold, decompose it under the current guardrail and implement only the first independently verifiable BA2 sub-slice in this turn. The recommended first checkpoint is BA2-A Session State Machine Contract.

Work only in Preflight-Architect. Do not modify production Colosseum.
```
