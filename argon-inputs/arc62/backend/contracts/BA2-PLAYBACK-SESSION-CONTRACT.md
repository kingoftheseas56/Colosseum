# BA2 Playback Session Lifecycle

Status: BA2 complete locally through BA2-D; production Colosseum adoption pending
Date: 2026-10-06
Arc: 62 Vinyl
Scope: presentation-independent playback lifecycle around one BA1 queue occurrence

## Decision

BA2 owns the logical playback lifecycle around the queue occurrence selected by BA1.

It does not own queue ordering, source discovery, exact source resolution, persistence, listen history, UI composition, or an mpv implementation.

The first bounded checkpoint, BA2-A, freezes the state-machine contract and proves its concurrency boundary without a real playback engine.

Reference implementation:

`ba2-playback-session-prototype/`

## Frozen upstream inputs

BA2 consumes these contracts without changing them:

- BA0 canonical music identity;
- BA1 `QueueOccurrence` and `QueueOccurrenceId`;
- BA1 ownership of queue order and active queue occurrence.

BA0 and BA1 required no compatibility change for BA2-A.

## Ownership boundary

### BA1 owns

- queue occurrence creation;
- queue order;
- duplicate Recording MBIDs as distinct queue occurrences;
- the active `QueueOccurrenceId`;
- queue mutation;
- non-wrapping queue advance.

### BA2 owns

- binding a playback lifecycle to one BA1 occurrence;
- lifecycle generation;
- logical playback state;
- play/pause intent and transitions;
- optional position and duration observations;
- seek request identity;
- EOF and failure termination for the bound occurrence;
- stale asynchronous-work rejection.

### BB owns

- source candidates;
- exact source resolution;
- source/provider identity;
- source selection and replacement policy.

A source change may later restart the same BA1 occurrence through a fresh BA2 generation. Source vocabulary does not belong in BA2-A.

### BA3 owns

- durable queue/session snapshots;
- restore policy;
- persistence format and migration.

BA2-A is memory-only.

### BA4 owns

- listen-event history;
- starts, pauses, resumes, seeks, skips, stops, completions;
- listening-time accounting.

BA2-A exposes lifecycle facts but does not persist or interpret them as listening history.

### QML owns

Presentation only.

No QML concept appears in the BA2-A prototype.

## Core invariant

Every asynchronous playback mutation is scoped to:

```text
exact QueueOccurrenceId
+
exact playback generation
```

The pair is represented by `PlaybackWorkToken`.

A callback is stale if either component differs from the active token.

Stale work returns `stale_async_work` and performs zero state mutation.

This applies to:

- load completion;
- pause/resume observation;
- position observation;
- duration observation;
- seek completion;
- EOF;
- playback failure.

The generation is mandatory even when the queue occurrence is unchanged.

Restarting playback for the same `QueueOccurrenceId` creates a new generation, so callbacks from the previous load cannot mutate the replacement load.

Stopping clears the active token. Any callback from the stopped lifecycle is therefore stale.

## Why occurrence identity is mandatory

Recording MBID identifies the song, not one queue occurrence.

The queue may legally contain:

```text
occurrence A -> Recording X
occurrence B -> Recording X
```

A delayed callback from occurrence A must never mutate occurrence B merely because both represent Recording X.

BA2 therefore binds to BA1 `QueueOccurrenceId`, not Recording MBID and not a queue index.

## Lifecycle states

BA2-A freezes these presentation-independent states:

- `Idle`
- `Loading`
- `Playing`
- `Paused`
- `Ended`
- `Failed`

Stable serialized/debug names are:

```text
idle
loading
playing
paused
ended
failed
```

These are logical session states, not claims about physical-device output.

Observed signal-path truth belongs to BD.

## State semantics

### Idle

No occurrence is bound.

There is no active work token.

Play, pause, and seek cannot implicitly pick a BA1 queue item.

Beginning playback requires an explicit `QueueOccurrence`.

This preserves BA1's rule that an inactive non-empty queue is not permission to auto-start item one.

### Loading

An exact occurrence is bound and a new generation exists.

`playWhenReady` records whether load completion should land in `Playing` or `Paused`.

Play and pause during loading change this intent without pretending that loading has completed.

### Playing

The bound lifecycle is logically playing.

Repeated play is idempotent.

Pause transitions to `Paused`.

### Paused

The bound lifecycle is logically paused.

Repeated pause is idempotent.

Play transitions to `Playing`.

### Ended

EOF terminated the current lifecycle.

The occurrence binding and work token remain available so a later coordinator can identify exactly which occurrence ended.

BA2-A does not advance the BA1 queue.

Play or seek cannot implicitly revive an ended lifecycle.

A new begin operation is required for replay or for the next occurrence.

### Failed

A typed failure terminated the current lifecycle.

The first accepted failure is retained.

Late EOF cannot replace failure with `Ended`.

A new begin operation is required to retry or replace the lifecycle.

## Begin semantics

`beginOccurrence(occurrence, playWhenReady)`:

1. consumes a valid BA1 `QueueOccurrence`;
2. increments the playback generation;
3. creates a `PlaybackWorkToken` from the exact occurrence ID and generation;
4. enters `Loading`;
5. records play/pause intent;
6. clears prior timing observations;
7. clears any pending seek;
8. clears prior failure.

The generation never wraps. Counter exhaustion fails closed.

Calling begin for the same occurrence is a real restart and therefore still increments generation.

This is the seam later source-replacement policy can use without putting source identity into BA2.

## Load completion

`markLoaded(token)` first validates the exact work token.

For the current generation:

- `Loading + playWhenReady=true -> Playing`;
- `Loading + playWhenReady=false -> Paused`;
- duplicate completion after already reaching `Playing` or `Paused` is a no-op;
- completion after terminal `Ended` or `Failed` cannot revive the lifecycle.

A stale load completion cannot complete a newer load.

## Play and pause

`play()` and `pause()` are deterministic logical session commands.

During `Loading`, they update `playWhenReady`.

During `Playing` or `Paused`, they transition logical state and are idempotent when repeated.

They do not select queue items, resolve sources, or call an engine in BA2-A.

BA2-B will translate these commands to an engine adapter.

`observePause(token, paused)` is the fenced observation path for adapter feedback.

It uses the same occurrence-plus-generation rule as every other asynchronous observation.

## Timing observations

Position and duration are optional observations.

Unknown is represented by absence, never a fabricated zero.

Accepted values must be finite and non-negative.

A new lifecycle clears prior observations so one occurrence cannot inherit another occurrence's timing facts.

Timing observations are lifecycle facts only.

BA4 later decides which facts become listen-history events.

## Seek contract

A seek is asynchronous work and requires stronger fencing than occurrence identity alone.

`PlaybackSeekRequest` contains:

```text
PlaybackWorkToken
seek sequence
target seconds
```

The token prevents a seek from landing on another occurrence or generation.

The monotonically increasing per-generation sequence prevents an older seek completion from overwriting a newer seek within the same occurrence generation.

Only the latest pending sequence may complete.

An older same-generation completion returns:

`superseded_seek`

and performs zero mutation.

A cross-generation completion returns:

`stale_async_work`

and performs zero mutation.

Seek target and observed applied position must be finite and non-negative.

EOF, failure, stop, or a new begin clears pending seek state.

## Stop contract

`stop()` terminates BA2 session ownership by returning to `Idle` and clearing:

- occurrence binding;
- work token;
- timing observations;
- pending seek;
- failure;
- play-when-ready intent.

It does not:

- remove the BA1 occurrence;
- change BA1 active occurrence;
- reorder the queue;
- clear the queue;
- write persistence;
- create history.

Repeated stop while already idle is a no-op.

## EOF contract

`markEnded(token)` only ends the current exact lifecycle.

It does not call BA1 `advance()`.

That coordination belongs to BA2-C, after the engine seam exists.

This separation is deliberate:

```text
engine EOF evidence
-> BA2 exact-token lifecycle termination
-> BA2-C coordination
-> BA1 queue advance policy
-> BA2 begin next occurrence
```

BA1 remains the only owner of queue movement.

## Failure contract

`PlaybackFailure` contains:

- an opaque bounded `code`;
- optional bounded diagnostic `detail`.

BA2 does not manufacture fine-grained engine certainty.

If an adapter can only prove a coarse failure such as `unknown`, BA2 preserves that evidence honestly.

Provider/source failure taxonomies remain outside BA2-A.

A stale failure is ignored through token rejection before it can mutate the active lifecycle.

## Typed errors

BA2-A defines:

- `none`
- `no_active_occurrence`
- `invalid_state`
- `invalid_time_value`
- `invalid_failure`
- `stale_async_work`
- `superseded_seek`
- `counter_exhausted`

Errors are deterministic domain results, not UI strings or engine exceptions.

## Deliberate omissions

BA2-A contains no:

- real mpv object;
- native player adapter;
- source URL;
- provider ID;
- source candidate;
- queue-order mutation;
- automatic next-track policy;
- persistence;
- restore;
- listen-event ledger;
- scrobbling;
- QML type;
- Listening Room assumption.

These omissions are ownership safeguards, not missing features.

## Relationship to current Colosseum seams

Current Colosseum already exposes the raw events BA2-B can later adapt:

- file load completion;
- EOF;
- playback error;
- pause;
- position;
- duration;
- exact seek.

Current signals can arrive through queued connections, which reinforces rather than weakens the generation-fence requirement.

BA2-A deliberately does not import those concrete player classes.

## Deterministic proof

The isolated Qt/C++ harness proves at minimum:

1. idle does not auto-start a queue item;
2. begin binds the exact occurrence and enters loading;
3. loading play/pause changes desired state;
4. load completion enters playing or paused deterministically;
5. repeated play/pause/load completion is idempotent where appropriate;
6. timing observations reject invalid values;
7. a newer same-generation seek supersedes the older seek;
8. a new occurrence rejects every tested callback from the old occurrence;
9. duplicate Recording MBIDs remain independent by occurrence ID;
10. restarting the same occurrence increments generation and rejects old callbacks;
11. failure is typed and terminal;
12. EOF is terminal but does not advance BA1;
13. stop clears only BA2 lifecycle state;
14. BA1 queue order and active occurrence survive BA2 stop/EOF unchanged.

Verification environment:

- Qt 6.11.1 Core;
- C++17;
- MSVC 19.44 / Visual Studio 2022;
- Windows SDK 10.0.26100.0.

Final BA2-A verification:

- CMake configure: PASS;
- native compile/link: PASS;
- harness execution: PASS;
- CTest: 1/1 PASS;
- prohibited dependency scan for QML/mpv/provider/Listening Room/persistence/history: zero hits.

## BA2 downstream completion

This file freezes BA2-A, while the downstream BA2 checkpoints are now complete and verified in their own owned contracts/evidence:

- BA2-B Engine Adapter: verified;
- BA2-C Queue / Session Coordination: verified;
- BA2-D Colosseum/mpv isolated adoption proof: verified;
- BA2-D-B1 raw API qualification: verified;
- BA2-D-B2 playlist-entry attribution: verified;
- BA2-D-B3 exact seek-completion attribution: verified.

The final BA2-D clean qualification passes 4/4 registered tests, including real libmpv load-identity and seek-completion probes against read-only Colosseum fixtures.

Production Colosseum adoption remains intentionally separate.
