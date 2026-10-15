# Arc 62 Vinyl - BA2-C Queue / Session Coordination Handoff

> Continuation status (2026-10-06): BA2 is complete and verified through BA2-D-B3. Wave 2 is sealed locally. The original handoff below is retained as historical pre-BA2-C context; current authority is `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md` plus `backend/README.md`.

Date: 2026-10-06
From: BA2-B Engine Adapter checkpoint
Target: fresh Preflight Architect chat
State: BA2-A + BA2-B locally complete and verified; BA2-C not started
Production Colosseum: read-only inspection only, untouched

## Entry point

Continue Arc 62 Vinyl backend with **BA2-C Queue / Session Coordination**.

Read current local Preflight authority first, then read completely:

- `arcs/62-vinyl/backend/README.md`
- `arcs/62-vinyl/backend/BA0-DOMAIN-CONTRACT.md`
- `arcs/62-vinyl/backend/BA1-QUEUE-CORE-CONTRACT.md`
- `arcs/62-vinyl/backend/BA2-PLAYBACK-SESSION-CONTRACT.md`
- `arcs/62-vinyl/backend/BA2B-ENGINE-ADAPTER-CONTRACT.md`
- `arcs/62-vinyl/backend/ba1-queue-core-prototype/`
- `arcs/62-vinyl/backend/ba2-playback-session-prototype/`
- `arcs/62-vinyl/backend/ba2-engine-adapter-prototype/`

BA0 and BA1 remain frozen.

BA2-A and BA2-B are now frozen upstream contracts for BA2-C unless BA2-C proves a concrete incompatibility.

## Completed BA2 foundations

### BA2-A

BA2-A owns logical playback lifecycle and freezes:

- `Idle / Loading / Playing / Paused / Ended / Failed`;
- exact active `QueueOccurrence` binding;
- `PlaybackWorkToken = QueueOccurrenceId + generation`;
- per-generation `PlaybackSeekRequest` sequencing;
- stale async load/pause/position/duration/seek/EOF/error rejection;
- stop clearing BA2 state only;
- EOF terminating one lifecycle without advancing BA1.

Canonical contract:

`arcs/62-vinyl/backend/BA2-PLAYBACK-SESSION-CONTRACT.md`

### BA2-B

BA2-B owns the engine-neutral command/callback seam and freezes:

- `PlaybackEngineCommands` load/play/pause/seek/stop port;
- exact work token on load/play/pause/stop;
- full `PlaybackSeekRequest` on seek;
- narrow `PlaybackLoadInput` containing only opaque resource + request headers;
- engine callbacks routed back through BA2-A fences;
- loading play/pause intent applied only after accepted load completion;
- stale/duplicate load completion never dispatching commands;
- stop invalidating BA2-A before engine stop, so synchronous stop callbacks are stale;
- no queue mutation, provider policy, persistence, history, QML, or concrete engine dependency.

Canonical contract:

`arcs/62-vinyl/backend/BA2B-ENGINE-ADAPTER-CONTRACT.md`

Reference prototype:

`arcs/62-vinyl/backend/ba2-engine-adapter-prototype/`

BA2-B final verification:

- Qt 6.11.1 MSVC 2022 x64 configure: PASS;
- MSVC 19.44 compile/link: PASS;
- direct deterministic fake-engine harness: PASS;
- CTest: 1/1 PASS;
- prohibited dependency scan: zero hits.

## Frozen ownership

- BA1 owns queue order and active `QueueOccurrenceId`.
- BA2-A owns logical playback lifecycle around one occurrence.
- BA2-B owns replaceable engine command/callback transport.
- BA2-C owns coordination between BA1 active-cursor transitions and the already-frozen BA2 lifecycle/adapter.
- BB owns source candidates, exact matching, source selection, fallback and provider policy.
- BA3 owns persistence.
- BA4 owns listen-event history.
- QML remains presentation only.

BA2-C must preserve these boundaries.

## Critical identity law

The queue and playback layers use different but linked identities:

`RecordingMbid` answers which canonical song.

`QueueOccurrenceId` answers which insertion of that song is active.

`PlaybackWorkToken` answers which playback generation for that exact occurrence owns an asynchronous callback.

BA2-C must never collapse these identities.

Duplicate Recording MBIDs in the queue remain independently playable because their occurrence IDs differ.

Restarting the same occurrence still receives a new playback generation.

## BA2-C objective

Define and prove the smallest deterministic coordinator that keeps BA1 active occurrence and BA2 active lifecycle aligned.

The coordinator should react to authoritative BA1 state rather than create a second queue cursor.

At a clean boundary, the invariant should be:

`BA1 active occurrence == BA2 bound occurrence`

when playback is active or loading.

When BA1 has no active occurrence, BA2 must not remain bound to an unrelated old occurrence.

## Resolved playback input

BA2-C may require an already-resolved `PlaybackLoadInput` to begin a newly active occurrence.

Do not resolve it inside BA2-C.

Use an injected/fake input supplier or equivalent test seam keyed to the exact `QueueOccurrenceId`.

The supplier is plumbing only.

It must not:

- discover candidates;
- rank candidates;
- choose providers;
- perform exact-track matching;
- implement fallback;
- persist source choice.

Those remain BB responsibilities.

If no resolved input is available for the active occurrence, fail closed with a typed coordination result rather than inventing one.

## Start semantics

BA1 deliberately does not auto-start an inactive non-empty queue.

BA2-C may expose an explicit start operation that:

1. requires a non-empty queue;
2. explicitly activates the first BA1 occurrence;
3. obtains already-resolved playback input through the injected seam;
4. begins BA2 through BA2-B exactly once.

This explicit start must not be confused with BA1 `advance()`.

After queue exhaustion, another EOF/advance path must not wrap back to item one.

A future explicit user start may start again.

## Active transition semantics

BA2-C should compare the authoritative BA1 active occurrence with the BA2 bound occurrence.

When the occurrence identity is unchanged:

- unrelated queue insertions must not reload;
- unrelated removals must not reload;
- unrelated moves must not reload;
- moving the active occurrence to another index must not reload;
- a no-op queue mutation must not reload.

When BA1 active occurrence changes to another occurrence:

- stop/invalidate the old BA2 lifecycle;
- obtain already-resolved input for the new exact occurrence;
- begin the new occurrence once;
- preserve occurrence identity even when Recording MBID is the same.

Do not use queue index equality as the playback identity test.

## Play Now

BA1 `playNow()` owns its queue mutation:

- insert immediately after old active;
- activate the new occurrence.

BA2-C observes that one authoritative active transition.

Acceptance should prove:

- the old active lifecycle is invalidated once;
- the new occurrence begins once;
- the old occurrence remains in BA1 queue order;
- no second coordinator-owned queue insertion or activation occurs.

## Active removal

BA1 already freezes active-removal continuity:

1. successor at the removed index;
2. otherwise predecessor;
3. otherwise none.

BA2-C must consume the resulting BA1 state.

It must not reproduce the successor/predecessor algorithm independently.

Acceptance should cover:

- removing active middle occurrence -> successor begins once;
- removing active tail -> predecessor begins once;
- removing final active occurrence -> BA2 becomes idle;
- stale callbacks from removed occurrence cannot mutate replacement lifecycle.

## Non-active edits

Nuclear donor evidence makes this boundary important.

Changing a non-active queue item must not interrupt current playback.

Acceptance should cover at minimum:

- append;
- Play Next;
- insert before active;
- move non-active;
- remove non-active;
- clear-after-current when active survives.

If BA1 active `QueueOccurrenceId` is unchanged, BA2 lifecycle generation should remain unchanged.

## EOF

BA2-A/BA2-B EOF only proves that the current exact lifecycle ended.

BA2-C owns the coordination step after accepted current EOF:

1. confirm the EOF belonged to the current BA2 work token;
2. ask BA1 to `advance()`;
3. if BA1 selected a successor, begin that exact occurrence once;
4. if BA1 became inactive at tail, leave BA2 ended or clear it according to the coordinator contract without wrapping;
5. never call `advance()` again from a stale/duplicate EOF.

This requires care because BA2-A currently treats duplicate matching EOF as an idempotent success/no-op.

BA2-C must distinguish a newly accepted EOF transition from a duplicate no-op before advancing BA1.

A stale EOF already fails through BA2-A and must perform zero queue mutation.

## Queue exhaustion

The frozen BA1 contract is non-wrapping.

When the final active occurrence reaches accepted EOF:

- BA1 `advance()` clears active;
- BA2-C must not reactivate item one;
- another stale or duplicate EOF must not change the queue;
- another generic sync must not auto-start the queue.

Repeat/autoplay/radio remain separate future policy.

## Stop

User/session stop should stop BA2 through BA2-B.

BA2-C must define explicitly whether this also deactivates BA1.

The recommended BA2 ownership model for this checkpoint is:

- playback stop clears BA2 lifecycle;
- BA1 queue/order remains intact;
- BA1 active occurrence remains intact unless an explicit BA1 command changes it.

That preserves the frozen BA2-A/B rule that stop does not mutate queue ownership.

A later explicit resume/start of the same active occurrence creates a new BA2 generation.

Do not silently reinterpret stop as clear/deactivate.

## Error behavior

A current playback failure belongs to BA2-A Failed state.

BA2-C should not automatically advance the queue or select fallback source on failure.

Automatic source fallback belongs BB/source policy.

Automatic skip-on-error is a separate product policy and is not authorized by BA2-C.

Stale failures must remain harmless through the BA2 token fence.

## Required adversarial cases

At minimum prove:

1. explicit start activates first item once;
2. unrelated append does not restart playback;
3. Play Next does not restart playback;
4. insert before active changes index but not playback occurrence/generation;
5. move active changes index but not playback occurrence/generation;
6. remove non-active does not restart playback;
7. Play Now causes exactly one playback occurrence transition;
8. removing active middle selects BA1 successor and starts it once;
9. removing active tail selects BA1 predecessor and starts it once;
10. removing final active stops/clears BA2 without inventing another queue item;
11. accepted current EOF advances BA1 once;
12. duplicate EOF does not advance twice;
13. stale old-generation EOF does not advance;
14. final EOF exhausts queue without wrap;
15. duplicate Recording MBIDs remain distinguished by occurrence;
16. stale load/error/position callbacks after active transition cannot mutate new lifecycle;
17. stop leaves BA1 order and active occurrence intact;
18. missing injected playback input fails closed.

## Explicit non-goals

BA2-C does not implement:

- source discovery;
- source ranking;
- exact source resolution;
- provider fallback;
- CUE/whole-album handling;
- persistence or restore;
- listen-event history;
- scrobbling;
- repeat;
- shuffle;
- autoplay/radio;
- concrete mpv integration;
- QML or Listening Room models.

## Production boundary

Production Colosseum remains read-only for this Arc 62 prototype work.

Current live playback seam can be inspected as architecture evidence only.

Do not modify production Colosseum unless Hemanth explicitly authorizes a later adoption slice.

## Guardrail

BA2-C is the next independently verifiable checkpoint.

Implement BA2-C only in the next explicit continue turn.

Do not continue into BA2-D in the same turn after BA2-C is completed or attempted.

## Recommended fresh-chat prompt

```text
@Brotherhood Desktop Commander
@Preflight Architect

Continue Arc 62 Vinyl backend with BA2-C Queue / Session Coordination.

First read completely:
arcs/62-vinyl/backend/HANDOFF-BA2C-QUEUE-SESSION-COORDINATION.md

Treat that handoff as the current entry point, then re-check current local authority and repository state before acting.

BA0 and BA1 are frozen. BA2-A and BA2-B are accepted upstream contracts unless BA2-C proves a real incompatibility.

Implement only BA2-C under the current oversized-slice guardrail. Do not continue into BA2-D in the same turn.

Work only in Preflight-Architect.
Production Colosseum is read-only unless I explicitly authorize adoption.
```

## Hard stop

BA2-B is complete and verified.

BA2-C has not started.

Do not interpret this handoff as authorization to implement BA2-C in the same conversation turn that completed BA2-B.
