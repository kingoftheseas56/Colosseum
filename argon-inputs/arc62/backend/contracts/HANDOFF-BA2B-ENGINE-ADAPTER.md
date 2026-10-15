# Arc 62 Vinyl - BA2-B Engine Adapter Handoff

> Continuation status (2026-10-06): BA2 is complete and verified through BA2-D-B3. Wave 2 is sealed locally. The original handoff below is retained as historical pre-BA2-B context; current authority is `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md` plus `backend/README.md`.

Date: 2026-10-06
From: BA2-A Session State Machine checkpoint
Target: fresh Preflight Architect chat
State: BA2-A locally complete and verified; BA2-B not started
Production Colosseum: read-only inspection only, untouched

## Entry point

Continue Arc 62 Vinyl backend with **BA2-B Engine Adapter Contract**.

Read current local Preflight authority first, then read completely:

- `arcs/62-vinyl/backend/README.md`
- `arcs/62-vinyl/backend/BA0-DOMAIN-CONTRACT.md`
- `arcs/62-vinyl/backend/BA1-QUEUE-CORE-CONTRACT.md`
- `arcs/62-vinyl/backend/BA2-PLAYBACK-SESSION-CONTRACT.md`
- `arcs/62-vinyl/backend/ba2-playback-session-prototype/`
- `arcs/62-vinyl/donor-reviews/feishin/DONOR-ASSESSMENT.md`
- `arcs/62-vinyl/donor-reviews/nuclear/DONOR-ASSESSMENT.md`

BA0 and BA1 remain frozen.

BA2-A is now the frozen upstream contract for BA2-B unless BA2-B proves a concrete incompatibility.

## BA2-A completion state

BA2-A created:

- `BA2-PLAYBACK-SESSION-CONTRACT.md`
- `ba2-playback-session-prototype/VinylPlaybackSession.h`
- `ba2-playback-session-prototype/VinylPlaybackSession.cpp`
- `ba2-playback-session-prototype/test_vinyl_playback_session.cpp`
- `ba2-playback-session-prototype/CMakeLists.txt`

The backend README was updated so BA2-A is the current completed checkpoint and BA2-B is next.

BA2-A required no BA0 or BA1 mutation.

## Frozen ownership

- BA1 owns queue order and active `QueueOccurrenceId`.
- BA2 owns playback/session lifecycle around that occurrence.
- BB owns source candidates and exact source resolution.
- BA3 owns persistence.
- BA4 owns listen-event history.
- QML remains presentation only.

BA2-B must preserve these boundaries.

## Critical concurrency invariant

All asynchronous playback work is scoped to:

```text
exact QueueOccurrenceId
+
exact playback generation
```

BA2-A represents this pair as `PlaybackWorkToken`.

A callback from any older occurrence or older generation is stale and must perform zero mutation.

This includes:

- load completion;
- pause/resume observation;
- position observation;
- duration observation;
- EOF;
- failure.

Seek completion has one additional fence:

```text
PlaybackWorkToken
+
per-generation seek sequence
```

BA2-A represents that as `PlaybackSeekRequest`.

An older seek completion from the same occurrence generation must not overwrite a newer seek.

## BA2-A state machine

Frozen logical states:

- `Idle`
- `Loading`
- `Playing`
- `Paused`
- `Ended`
- `Failed`

Important semantics:

- beginning any lifecycle increments generation;
- restarting the same queue occurrence also increments generation;
- play/pause during Loading alter `playWhenReady`;
- load completion lands in Playing or Paused from that intent;
- position and duration are optional observations, never fake zero;
- stop clears BA2 state only;
- EOF ends BA2 lifecycle only;
- BA2-A never advances BA1;
- terminal Ended/Failed cannot be revived by late callbacks;
- a new explicit begin is required for replay/replacement/next occurrence.

## BA2-B objective

Define and prove the smallest engine-neutral adapter seam between a concrete playback engine and the BA2-A state machine.

The original BA2 decomposition requires a minimal interface for:

- load the current occurrence's already-resolved source;
- play;
- pause;
- seek;
- stop;
- callbacks/observations for loaded;
- position;
- duration;
- pause state;
- EOF;
- error.

The adapter must consume BA2-A lifecycle identity rather than invent a second lifecycle.

## Required adapter fencing

Every engine operation initiated for a lifecycle must close over or otherwise retain the exact `PlaybackWorkToken` that authorized it.

Every asynchronous adapter callback forwarded into BA2-A must carry the exact token belonging to the engine work that produced it.

Seek operations must retain the exact `PlaybackSeekRequest`, not merely a target time.

Do not derive callback ownership from:

- current Recording MBID;
- queue index;
- whichever item is active when the callback arrives;
- current source URL;
- provider ID;
- mutable global player state alone.

The callback must know which occurrence generation created it.

## Source boundary

BA2-B may accept an opaque already-resolved load input if one is required to express the engine seam.

It must not implement:

- candidate discovery;
- provider ranking;
- exact-match source resolution;
- fallback policy;
- provider identity policy.

Those belong to BB.

If BA2-B needs a source-shaped input, keep it deliberately narrow and injected.

Do not pull BB policy into BA2 to make a demo easier.

## Queue boundary

BA2-B must not call BA1 `advance()`, mutate queue order, remove occurrences, or decide next-track policy.

EOF should reach the BA2-A lifecycle as evidence for the exact token.

**BA2-C Queue / Session Coordination** remains responsible for proving how BA1 active-occurrence transitions and BA2 lifecycle transitions compose.

## Persistence/history boundary

BA2-B must not write:

- session snapshots;
- restore state;
- listening history;
- scrobbles;
- completion ledger entries;
- listening-time facts.

BA3 and BA4 remain separate lanes.

## Production boundary

Do not modify production Colosseum in BA2-B.

Current Colosseum player files may be inspected read-only as architecture references:

- `qml/AudiobookSession.qml`
- `native/player/mpvitem.h`
- `native/player/mpvitem.cpp`

The current production seam exposes the relevant shape:

- file load;
- exact seek;
- pause;
- position;
- duration;
- file loaded;
- EOF;
- playback error.

Queued callback delivery is one reason BA2's generation fence is non-negotiable.

BA2-B should remain engine-neutral.

Concrete mpv adoption belongs to BA2-D unless a later explicit task changes that scope.

## BA2-A verification evidence

Verified locally on Windows with:

- Qt 6.11.1 Core;
- C++17;
- MSVC 19.44 / Visual Studio 2022;
- Windows SDK 10.0.26100.0.

Results:

- CMake configure: PASS;
- native compile/link: PASS;
- direct adversarial harness execution: PASS;
- CTest: 1/1 PASS;
- QML dependency scan: zero hits;
- mpv dependency scan: zero hits;
- provider dependency scan: zero hits;
- Listening Room dependency scan: zero hits;
- persistence dependency scan: zero hits;
- history dependency scan: zero hits.

The harness proves stale load/pause/position/duration/seek/EOF/error callbacks cannot mutate a newer occurrence generation.

It also proves duplicate Recording MBID queue occurrences remain independent and that a same-occurrence restart still invalidates older callbacks by generation.

## Repository state at handoff

Preflight root:

`C:\Users\Suprabha\Desktop\Preflight-Architect`

Observed branch state at BA2-A closeout:

`main...origin/main [ahead 106, behind 168]`

Observed HEAD:

`c4d51eb38771bc16db445674965823a0865f2922`

The checkout is heavily dirty with unrelated concurrent work.

Arc 62 is part of that local untracked/dirty state.

Do not:

- clean the checkout;
- reset;
- stash unrelated work;
- stage unrelated work;
- overwrite other agents' changes.

Re-check live repository state before BA2-B begins because this state may move.

Production Colosseum was inspected read-only and was not modified by BA2-A.

## Oversized-slice guardrail

The current Preflight guardrail treats roughly 15 minutes of sustained implementation/verification as oversized.

BA2 was therefore decomposed into:

- BA2-A Session State Machine Contract;
- BA2-B Engine Adapter Contract;
- BA2-C Queue / Session Coordination;
- BA2-D Colosseum mpv adoption plan or isolated adapter proof.

BA2-A is complete.

BA2-B is the next independently verifiable checkpoint.

Do not continue into BA2-C in the same turn after BA2-B is completed or attempted.

## Acceptance direction for BA2-B

A good BA2-B checkpoint should prove at minimum:

1. engine commands are driven by explicit BA2-A lifecycle state;
2. load work is associated with the exact active `PlaybackWorkToken`;
3. stale loaded/error/EOF/observation callbacks cannot mutate a newer generation;
4. seek work retains the exact `PlaybackSeekRequest`;
5. an older seek completion cannot land after a newer seek;
6. play/pause/stop do not mutate BA1 queue ownership;
7. adapter source input does not implement BB resolution policy;
8. no QML or Listening Room vocabulary enters the backend;
9. no persistence or listen-history side effects appear;
10. the adapter remains replaceable enough for a later concrete mpv bridge.

## Recommended fresh-chat prompt

```text
@Brotherhood Desktop Commander
@Preflight Architect

Continue Arc 62 Vinyl backend with BA2-B Engine Adapter Contract.

First read completely:
arcs/62-vinyl/backend/HANDOFF-BA2B-ENGINE-ADAPTER.md

Treat that handoff as the current entry point, then re-check current local authority and repository state before acting.

BA0 and BA1 are frozen. BA2-A is the accepted upstream lifecycle contract unless BA2-B proves a real incompatibility.

Implement only BA2-B under the current oversized-slice guardrail. Do not continue into BA2-C in the same turn.

Work only in Preflight-Architect.
Production Colosseum is read-only unless I explicitly authorize adoption.
```

## Hard stop

This handoff closes BA2-A.

Do not interpret it as authorization to implement BA2-B in the same conversation turn that completed BA2-A.
