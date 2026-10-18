# K12 counter utility checkpoint (partial)

Source basis: `ffc2870d2c697b65948b9fab66320a86b8f84fde` on
`feature/colosseum-server-1.0`. This adds the M662 counter utility, not complete
K12, registry/HTTP integration, server streaming or full Stremio parity.

## Source authority

Exact server.js SHA256:
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
The comparator extracts bytes [3424943,3425719), verifies their SHA256
`fb3f3872c39d5587bf694679581f4a7d0a497a86b0fc21ad3d02c884de94ad26`,
and evaluates that unchanged M662 function. It does not fetch or substitute an
oracle. The existing general module splitter failed on this bundle and its
Python fallback is absent from this partial checkout; that failed attempt is
retained locally. The narrow extraction is independently guarded by both hashes.

Node used: 24.19.0, separate from the historical 22.16.0 qualification.
Both comparator sides use deterministic synthetic timers. These results prove
the tested count/event/timer-call order, not real Node timer coercion or OS timing.

## Behavior

- First open notifies before increment; reopen during grace cancels without
  another positive event.
- Close without open retains NaN behavior. Duplicate close can become negative
  while the previous timer remains armed.
- Zero callback precedes deletion, including reentrant open erasure and the
  source's surviving reentrant timer whose map entry is deleted.
- Throwing callbacks interrupt the source's subsequent mutations.
- Native ownership cancels all pending handles, including the surviving
  reentrant timer. Weak state and post-callback liveness checks prevent delivery
  after close/destruction. This is native lifetime safety, not a JS API claim.
- One-shot scheduler contract: never inline, same app lane. Foreign-lane
  operations and inline delivery are rejected. No worker pool is introduced.

## Tests and retained failures

14 hash-locked differential scenarios and 10 native ownership cases pass.
Full registered native suite: 83 executed, 83 passed, 0 failed, 0 skipped
(55.16 seconds), in the explicit fixed-interface profile from LINUX-BUILD.md.
Final focused rerun passed after a configure-message-only edit.
See evidence/dots-2026-10-18/k12-counters-tests.txt for final and negative logs.
The initial absent-candidate invocation failed. The initial inline-scheduler
implementation failed its ownership assertion because it notified before rejecting;
the corrected guard rejects before the callback. Deliberately removing the
key-callback liveness guard causes ownership failure. Deliberately dropping the
fired handle after onZero throws causes differential failure (lost cancellation).
Those failed logs are retained, separate from final results.

Test executable SHA256:
`7e08ac136bd271b67d8a8f1c98d27bda501b0c2b5bbc04b43318987046dfa86a`.

Reproduce with the Linux dependency setup in LINUX-BUILD.md:

```sh
cmake -S native/colosseum_server_v1 -B build/server1 \
  -DSERVER1_K12_ORACLE=/path/to/exact/server.js
cmake --build build/server1 --target server1_k12_counter_test
ctest --test-dir build/server1 -R K12 --output-on-failure
```

Ownership tests are always registered. Differential qualification requires Node
and the explicitly supplied oracle; absent inputs are reported at configure time
and that test is not registered. A run without it is not differential qualification.

## Remaining gates

No counter wiring to stream lifecycle, idle/inactivity policy, statistics,
keepConcurrency, removals, file guessing, or real HTTP requests yet. Timers are
synthetic here; a real app-lane one-shot adapter and owner integration remain.
Real HTTP duplicate finish/close suppression belongs to its existing caller.
Windows and complete server runtime are NOT RUN. The host still declares
`streaming-ready=NO`.
