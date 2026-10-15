# Arc 62 Vinyl - BA2-D Colosseum mpv Adoption Handoff

> Continuation status (2026-10-06): BA2-D-A, BA2-D-B1, BA2-D-B2, and BA2-D-B3 are complete and verified. Wave 2 is sealed locally. The original handoff below is retained as historical pre-BA2-D context; current authority is `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md` plus `backend/README.md`.

Date: 2026-10-06
From: BA2-C Queue / Session Coordination checkpoint
Target: fresh Preflight Architect chat
State: BA2-A + BA2-B + BA2-C locally complete and verified; BA2-D not started
Production Colosseum: read-only through BA2-C

## Entry point

Continue Arc 62 Vinyl backend with **BA2-D Colosseum mpv Adoption**.

Read current local Preflight authority first, then read completely:

- `arcs/62-vinyl/backend/README.md`
- `arcs/62-vinyl/backend/BA2-PLAYBACK-SESSION-CONTRACT.md`
- `arcs/62-vinyl/backend/BA2B-ENGINE-ADAPTER-CONTRACT.md`
- `arcs/62-vinyl/backend/BA2C-QUEUE-SESSION-COORDINATION-CONTRACT.md`
- `arcs/62-vinyl/backend/ba2-playback-session-prototype/`
- `arcs/62-vinyl/backend/ba2-engine-adapter-prototype/`
- `arcs/62-vinyl/backend/ba2c-queue-session-coordination-prototype/`

Re-inspect current production Colosseum player seams read-only before deciding the concrete bridge shape:

- `native/player/mpvitem.h`
- `native/player/mpvitem.cpp`
- `qml/AudiobookSession.qml`

Production Colosseum remains read-only unless Hemanth explicitly authorizes a later adoption mutation.

## Completed BA2 core

### BA2-A

BA2-A owns lifecycle and freezes:

- `Idle / Loading / Playing / Paused / Ended / Failed`;
- exact `QueueOccurrenceId + generation` work fencing;
- per-generation seek sequencing;
- stale load/pause/position/duration/seek/EOF/error rejection.

### BA2-B

BA2-B owns the engine-neutral seam and freezes:

- token-scoped load/play/pause/stop;
- full `PlaybackSeekRequest` on seek;
- narrow opaque `PlaybackLoadInput`;
- loaded/pause/position/duration/seek/EOF/error callback routing;
- invalidate-before-engine-stop ordering.

### BA2-C

BA2-C owns queue/session coordination and freezes:

- BA1 remains the sole queue authority;
- active occurrence changes are detected by exact `QueueOccurrenceId`, not queue index;
- unrelated queue edits do not restart playback;
- Play Now and active removal produce exactly one lifecycle transition;
- accepted current EOF calls BA1 `advance()` once;
- duplicate/stale EOF cannot advance twice;
- final EOF exhausts without wrap;
- playback-only stop preserves BA1 order and active occurrence;
- missing resolved input fails closed with BA1 remaining authoritative.

Reference contract:

`arcs/62-vinyl/backend/BA2C-QUEUE-SESSION-COORDINATION-CONTRACT.md`

BA2-C final verification:

- Qt 6.11.1 MSVC 2022 x64 configure: PASS;
- native compile/link: PASS;
- deterministic harness: PASS;
- CTest: 1/1 PASS;
- prohibited dependency scans: zero hits.

## BA2-D objective

Determine and prove the smallest concrete bridge from the frozen `PlaybackEngineCommands` / BA2 callback seam to Colosseum's current mpv/libmpv player machinery.

BA2-D must adapt the engine.

It must not redesign BA1/BA2-A/BA2-B/BA2-C.

The desired dependency direction is:

```text
BA1 queue
  -> BA2-C coordinator
  -> BA2-A lifecycle
  -> BA2-B engine-neutral port
  -> BA2-D Colosseum/mpv bridge
  -> existing mpv/libmpv runtime
```

## Critical problem to solve

The current Colosseum player signals are engine/global callbacks such as:

- file loaded;
- pause changed;
- position changed;
- duration changed;
- EOF;
- playback error.

Those callbacks are not inherently tagged with Vinyl's `PlaybackWorkToken`.

BA2-D must therefore define a concrete bridge that retains the exact token belonging to the engine work that produced each callback.

Do not derive callback ownership from:

- the current Recording MBID;
- the current queue index;
- whichever queue item is active when the callback arrives;
- the current source URL;
- provider ID;
- mutable global player state alone.

A delayed callback from an older load must still carry or resolve to the older token so BA2-A rejects it.

## Seek requirement

The same rule applies more strongly to seek.

A concrete seek must retain the exact `PlaybackSeekRequest`, including:

- `PlaybackWorkToken`;
- per-generation seek sequence;
- target seconds.

A later seek must supersede an earlier seek without allowing the earlier completion to land against the newer request.

Do not reduce seek ownership to target time alone.

## Load input mapping

BA2-D may map `PlaybackLoadInput` to the current Colosseum player load methods.

Current architecture references have exposed both plain loads and header-bearing loads.

The bridge may therefore map:

- opaque resource -> player load;
- request headers -> header-bearing load path.

The bridge must not decide:

- which provider wins;
- which candidate is exact;
- fallback order;
- canonical Recording identity.

Those remain BB responsibilities above BA2-D.

## Play/pause/stop mapping

Map the frozen engine-neutral commands without changing semantics:

- play -> deterministic unpause/play;
- pause -> deterministic pause;
- stop -> concrete player stop;
- stop-triggered callbacks must remain stale because BA2-B invalidates lifecycle before issuing stop.

Do not reintroduce toggle-only semantics where BA2-B requires deterministic play/pause.

## Callback mapping

BA2-D must prove a mapping for:

- loaded;
- pause observation;
- position observation;
- duration observation;
- seek completion;
- EOF;
- failure.

The bridge should forward only evidence it can actually observe.

Do not invent seek-complete certainty if the concrete player exposes only position changes. If an exact completion event is unavailable, define the smallest honest mechanism or report the gap rather than fabricating one.

## Failure mapping

Preserve evidence honestly.

If current Colosseum/mpv supplies only a coarse error code/message for a condition, forward that coarse evidence into `PlaybackFailure`.

Do not manufacture provider/source-resolution meaning inside the bridge.

## Queue boundary

BA2-D must never:

- call BA1 `advance()`;
- insert/remove/reorder queue occurrences;
- choose next track;
- implement Play Now / Play Next;
- clear or deactivate BA1.

Those behaviors already belong to BA1 + BA2-C.

## Persistence/history boundary

BA2-D must not:

- write session snapshots;
- restore queue state;
- write listening history;
- emit scrobbles;
- calculate listening-time facts.

BA3 and BA4 remain separate.

## Presentation boundary

BA2-D is a native/backend bridge.

Do not make QML the lifecycle authority.

If existing QML currently drives some player commands, BA2-D should document the migration/adoption seam rather than cementing QML as durable playback ownership.

## Recommended checkpoint shape

Before editing production Colosseum, first determine whether BA2-D can be proven as an isolated native adapter around a fake/minimal player-facing port.

If yes, prefer an isolated proof in Preflight before production adoption.

If the concrete Colosseum types are required to prove the bridge, treat production mutation as a separate explicitly authorized adoption step.

Do not assume that “BA2-D” automatically authorizes changing Colosseum.

## Acceptance direction

A good BA2-D checkpoint should prove at minimum:

1. every load is bound to the exact `PlaybackWorkToken`;
2. loaded callbacks return the token belonging to the load that produced them;
3. old-load callbacks cannot mutate a newer lifecycle;
4. play and pause map deterministically;
5. stop maps without reviving stopped lifecycle through synchronous/late callbacks;
6. seek preserves the full `PlaybackSeekRequest`;
7. old seek completion cannot land after a newer seek;
8. position/duration/pause observations retain correct generation ownership;
9. EOF and error retain correct generation ownership;
10. plain and header-bearing load input can be represented without provider policy;
11. no BA1 queue policy leaks into the bridge;
12. no BA3/BA4/QML ownership leaks into the bridge;
13. concrete mpv details remain below the already-frozen BA2-B port.

## Open evidence question

The largest concrete unknown is whether the existing player seam exposes an event that can honestly qualify an exact seek completion.

Inspect current source before choosing a mechanism.

Possible outcomes include:

- a real observable seek-completion signal exists and can retain request identity;
- the bridge needs a small concrete tokenized completion mechanism;
- current production seam cannot prove exact completion, in which case BA2-D must report that gap and avoid fake certainty.

Evidence decides this, not convenience.

## Guardrail

BA2-D is the next checkpoint.

Re-check its size against current Colosseum reality before implementation.

If it crosses the active Preflight oversized-slice threshold, subdivide it and respect the hard-turn rule.

## Recommended fresh-chat prompt

```text
@Brotherhood Desktop Commander
@Preflight Architect

Continue Arc 62 Vinyl backend with BA2-D Colosseum mpv Adoption.

First read completely:
arcs/62-vinyl/backend/HANDOFF-BA2D-MPV-ADOPTION.md

Treat that handoff as the current entry point, then re-check current local authority and repository state before acting.

BA0 and BA1 remain frozen. BA2-A, BA2-B, and BA2-C are accepted upstream contracts unless BA2-D proves a concrete incompatibility.

Re-inspect the current Colosseum mpv/player seam read-only before designing the concrete bridge.

Work only in Preflight-Architect unless I explicitly authorize production Colosseum adoption.
```

## Hard stop

BA2-C is complete and verified.

BA2-D has not started.

Do not interpret this handoff as authorization to start BA2-D in the same turn that completed BA2-C.
