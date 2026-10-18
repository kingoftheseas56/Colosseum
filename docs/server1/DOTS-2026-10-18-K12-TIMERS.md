# K12 timer and ordering foundation (partial)

Basis: `26acb4be6eb0657384e83374f6c4b71883f1b44f`.
This checkpoint extends the existing counter utility. EngineFS/HTTP lifecycle
integration and complete K12 remain pending.

## Changes

- `EngineRegistry::list` uses the existing monotonic creation generation to
  preserve M172 insertion order. Reuse retains position; recreation appends.
  Operational removal/generation safety has not been weakened.
- `qtEngineAfter` uses the existing Qt event loop, with no new worker or thread.
  It provides asynchronous single-shot scheduling, cancellation, owner-death
  cancellation, and deferred QObject cleanup. Timer callbacks report exceptions
  to a required nonthrowing error sink; exceptions cannot escape Qt dispatch.
- Timer handles support cancellation/destruction from another thread. The timer
  remains handle-owned, foreign stop is queued, and deletion uses deleteLater.
  EngineCounter teardown is synchronized with state delivery. New calls may not
  race destruction of the C++ counter object itself.
- Oracle loader now supplies Node's external `require` and a VM-local `global`.
  The original repository Python splitter was recovered unchanged (Git blob
  `258283d2785d0d098d73bb4ed18189830990f054`); extraction yields 1312 modules with
  exact byte round-trip. Both bundle and module hash guards remain in force.

## Evidence

- R17 insertion/reuse/recreation test failed on hash-sorted listing, then passed.
- Real Qt timer test failed with the initial unscheduled stub, then passed.
- A throwing callback escaped Qt dispatch before containment; its retained
  negative test fails. The corrected adapter reports the error and continues.
- Foreign-thread destruction originally aborted under QT_FATAL_WARNINGS=1:
  `QObject::killTimer: Timers cannot be stopped from another thread`.
  The fixed adapter passes all 10 real-event-loop cases under the same setting.
- 14 hash-locked synthetic-clock counter scenarios and 10 native ownership cases
  continue to pass.
- Full existing registered suite: 85/85 passed, 0 skipped, 55.85 seconds.
  Two subsequently registered oracle checks separately passed 2/2. No native
  source changed between these runs; final registered coverage totals 87/87.
  This is two commands, not a claimed single 87-case run.
- AddressSanitizer Debug runs pass 3/3 focused tests with detect_leaks=0.
  LeakSanitizer-enabled runs fail 3/3 with its own fatal runtime error and ptrace
  hint. The actual underlying LSan host cause is unestablished. Leak checking
  remains BLOCKED, not passed; original failures are preserved.

Ordinary Release executable SHA256 values:
- K12 Qt timer: c4fd2280d58f62cd576a62fdcfa05375bab186e01a9fb30f840d39176709f77a
- K12 counter: 2ff4e031f71a9aad98c9ce25ffa512fc3310093784e5a54be597a0b253fe95d2
- K11 registry: 3c5814514056c7359ce899c89bf23a230a3616798087d4f98040fe6f20931c21

Build setup remains in LINUX-BUILD.md. Configure with SERVER1_K12_ORACLE pointing
to the exact server.js. Commands: `cmake --build build/server1`, then
`ctest --test-dir build/server1 --output-on-failure`; focused tests use
`-R 'K12|R17'`. Real Qt tests enforce QT_FATAL_WARNINGS=1 in CTest.

## Boundaries

The owning event loop must outlive normal deferred timer teardown. The injected
error sink must not throw; violating that contract is explicitly fatal. This
adapter supports delays through INT_MAX milliseconds and rejects larger values;
it is not a claim to implement every JavaScript timer-coercion quirk.

The M172 registry oracle uses controlled engine/destroy stubs to establish map
ordering and visibility. Its synchronous ready stub does not establish M814's
next-tick ready behavior. Same-hash create during pending destroy reuses a
retiring source object, then old completion removes visibility. Native
operational Registry.create deliberately retains its tested generation safety.
A source-facing resolver/visibility projection must reconcile that distinction;
no full removal/create-race parity claim is made here.

No actual HTTP response lease, cache watcher, statistic serialization,
keepConcurrency or streaming/player integration is qualified by this checkpoint.
Windows is NOT RUN. LeakSanitizer is BLOCKED as above.

## Broader Python lab qualification is not green

The first unittest attempt used the wrong working directory and is retained.
The corrected source-root invocation ran 66 tests and reported 6 failure events,
19 error events and 8 skips (subtest events are not independent top-level counts).
Observed causes include unconditional Windows Popen._handle access on Linux,
missing historical evidence files in the partial snapshot, unavailable external
Windows authority paths, and an unset qualified Node executable. Native/Qt results
above do not override these failures. Fifteen missing committed historical input files have since been recovered and
Git-blob verified; a corrected rerun and Linux lab-runner repair are pending.
The external PARALLEL-WORK-ITEMS.json authority (SHA256
777319421c8c8d50348492ace51d7c51eb8c6b14e8e225d5d1e8962bdb1c3dc7)
is absent from the supplied packets and repository; no existing assertions were weakened.
