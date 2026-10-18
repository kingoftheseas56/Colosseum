# Delivered-tick clock and existing-loop repeat adapter

Parent: f68dcd561716182e3d16a24bd28e2014b9823b5c.

This extends the existing QtEngineTimer owner with qtEngineRepeat, preserving
one-shot behavior. Positive intervals are required. Qt coalesces overdue timer
events instead of synthesizing elapsed-time catch-up calls. Callback exceptions
cancel the timer before reporting through the existing nonthrowing error sink.
No worker, thread, global timer or alternate scheduler is added.

engineSpeedometerClock creates one shared reader for the source-facing app owner.
Its atomic uint16 tick starts at 1 and advances once per delivered 250ms callback.
Copies share the same tick/lifetime; transport-lane reads are side-effect-free.
The final reader release cancels the timer. Timer callbacks hold only weak state,
so late callbacks cannot revive it. Production owners must create this clock once
and pass copies to their engines rather than create a clock per peer or engine.
The EngineRepeat deferred app-lane delivery contract is now explicit.

## Regression and evidence

A first uncommitted repeat implementation copied std::function on every tick.
The mutable-callable regression failed with "repeat discarded callable-local
state", exit 1. Review also identified a potentially throwing copy outside the
exception boundary. The final implementation invokes the retained callable inside
try while a shared self reference protects its lifetime. Tests verify mutable
state and a callable armed to reject copies after timer registration.

The real Qt lifecycle executable now checks 29 cases, retaining the original ten
one-shot cases. Added checks cover recurring delivery/cancel, zero interval,
owner departure, self-destruction, terminal callback error, foreign-thread
release, stalled event-loop coalescing, shared source tick/readers, foreign tick
read/final release, absent/refused timer, modulo wrap and stale callback safety.
Manual delivered ticks qualify wraparound only; actual Qt event-loop tests
qualify scheduling/lifecycle. Neither proves live torrent metering integration.

Commands (existing configured Linux build):

    cmake --build build/server1 -j2
    QT_FATAL_WARNINGS=1 build/server1/p03-tests/server1_k12_timer_test
    ctest --test-dir build/server1 --output-on-failure --timeout 90

Matching RelWithDebInfo-ASan uses -fsanitize=address -fno-omit-frame-pointer and
ASAN_OPTIONS=detect_leaks=0. LeakSanitizer remains unqualified. Windows NOT RUN.
Source transfer-event ingestion and statistics snapshot barriers are next gates;
no complete Server 1.0 or HTTP-to-player qualification is claimed.

Final full suite: 103/103 PASS, zero skipped, 68.91s. Matching ASan timer test
1/1 PASS, 1.26s. Executable hash and retained failure are in
evidence/dots-2026-10-18/shared-clock-tests.txt.
