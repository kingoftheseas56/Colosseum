# K08-F2 source trace

This additive tranche exposes one engine-facing terminal-failure entry point on `FileReader`. The contract was independently approved before production implementation began.

The contract makes the first active failure call win and normalizes empty input to the exact fallback `file reader failed`. It requires state/ownership detachment before external cancellation and deselection, exact-once cancellation of detached active reads, disposal of waiting and out-of-order work, preservation of the already-ready queue, reentrant/late-completion rejection, one-shot error observation, and strict isolation from sibling readers.

Production implements one shared `State::terminalFailure` transition used by external `fail()`, source-read errors, and malformed short-piece failures. It records terminal state, snapshots and clears owned tokens/locks/completions/waiting state, and detaches selection ownership before invoking source or scheduler methods. `fail()` retains the shared state across reentrant callbacks. The pump checks terminal state on every scheduling iteration.

The code-review repair extracts shared `detachOwnership()` and `releaseDetached()` operations. Failure, explicit close, and destruction now clear active tokens, locks, waiting/out-of-order state, and selection ownership before any `cancelRead()` or `deselect()` callback. Public close/fail entry points retain the shared state while reentrant callbacks run.

FileReader is deliberately serialized-owner-lane code. Every public method, refresh callback, and source completion must execute on that same lane. It has no internal cross-thread synchronization. K11 must marshal transport observations and store completions onto the owner lane before calling the reader.

Testing exposed that reader-local request tokens collide when readers share one source. Production now allocates process-wide atomic read tokens so cancellation ownership is unambiguous across sibling readers.

Merge endpoint: fast-forward into `feature/colosseum-server-1.0` only after independent Sol contract/code approval, Agent 4 acceptance, and all K08 regression, mutation, and blob-seal gates pass.
