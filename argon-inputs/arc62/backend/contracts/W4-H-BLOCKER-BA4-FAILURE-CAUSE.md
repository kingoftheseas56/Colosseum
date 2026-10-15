# W4-H Blocker: BA4 Cannot Represent Playback Failure Cause

Date: 2026-10-07
Task: T-8 / W4-H One Superbuild + End-to-End Play Journey
State: BLOCKED
Owner requiring repair: T-5 / W4-E (BA4)

## Failing seam

Producer: W4-B QueueSessionCoordinator.

`QueueSessionCoordinator::engineFailed(...)` emits a coordinator transition fact:
`OccurrenceEnded { QueueOccurrenceId, OccurrenceEndCause::Failed }`.

Consumer: BA4 factual listen history.

BA4's public event vocabulary is:
Start, Pause, Resume, Seek, Skip, Stop, Completion, SourceChange, ListeningSpan.

`ListenEventRecorder` exposes terminal recording methods for `skip(...)`, `stop(...)`, and `complete(...)`, but no failure terminal operation and no generic terminal-cause field.

## Reproduction

1. Queue and activate an occurrence.
2. Resolve a source through W4-C and restart playback through W4-B.
3. Deliver a real/current playback failure to `QueueSessionCoordinator::engineFailed(...)`.
4. W4-B correctly emits `OccurrenceEnded(..., Failed)`.
5. Attempt to persist that factual terminal cause in BA4.
6. No BA4 public API can represent failure distinctly.
7. Mapping failure to `stop(...)` would collapse engine failure and explicit user stop into the same factual history event.

This violates the W4-H requirement that failure and stop remain distinguishable in factual history, and the W4-B acceptance law that skip/remove/stop/failure causes are distinguishable for BA4.

## Why W4-H cannot repair this

BA4 contract/prototype files are frozen W4-E ownership, outside the W4-H write set.

Silently adding a failure event from W4-H would violate the cross-slice ownership rule and could also require persistence compatibility and W4-G derivation review.

## Smallest proposed repair

W4-E should add the smallest BA4-owned factual representation for playback failure.

Preferred shape:
- add a distinct `ListenEventKind::Failure`;
- add `ListenEventRecorder::failure(...)` with the same factual clock/position validation as other terminal facts;
- persist/restore that event kind in BA4 durable history;
- add round-trip and stale-token tests;
- verify W4-G derivation handles failure explicitly without turning it into positive/negative taste unless policy says so.

No W4-B change is required: its `OccurrenceEndCause::Failed` producer fact already exists and is correct.

## W4-H status impact

H1 superbuild: PASS.
H2 reusable mpv sidecar extraction: PASS.
H3-H7: halted before broad implementation because the fake Play journey cannot satisfy the BA4 failure-history acceptance criterion honestly.

Production Colosseum was not modified.
