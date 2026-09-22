# K11-v4 producer self-review

Reviewer: `[Agent (Claude), K11-A repair producer self-review]`. This is not independent acceptance.

- MET: Scope. Changed production files are `EngineRegistry.h`, `EngineRegistry.cpp`, `TorrentEngine.cpp`; plus `test_engine_registry.cpp`, `cmake/packets/K11.cmake` (adds K05 link) and K11-A artifacts. No aggregate CMake, STATE, accepted predecessor header, FileReader or transport change.
- MET: Finding 1. Fenced-lane regressions prove metadata install, restore scan, verify/commit, cache reads, upload reads and transport open/connect never complete while the work lane is held, and the app lane returns in under 1 s during a blocked transport open. Inline and app-thread executors are refused.
- MET: Finding 2. Terminal completions are ledgered; destruction from a foreign thread posts through callbackExecutor, decided Removed survives a fenced close, late close completions are no-ops, and remove races (before open, during install, during commit) terminate once with no late HAVE, data or Ready.
- MET: Finding 3. 10000 ms interval from ontorrent, rechoke unchoke on tick, one cancellation, zero effective post-close ticks.
- MET: Finding 4. Drain, dedup, malformed rejection, min/max with and without a swarm cap, swarm pause/resume, stale-generation isolation, blocking connects on the work lane.
- MET: Findings 5/6. Observational raw traces on both sides, shared normalizer, disclosed divergence explicit, 24 drift controls, v3 oracle blindness demonstrated.
- MET: Safety. Committed-only upload and visibility proven against the staged-bytes case the K06 store can otherwise serve.
- MET (round 2): Partial restore. A real piece is advertised or uploaded only when every virtual component is committed; a surviving component stays readable; re-download restages survivors and yields exactly one HAVE; a corrupt survivor resets and refetches the group (R6, 5 new mutants killed).
- RISK: Registry-owned worker threads are detached when their last reference drops on themselves; a process exiting mid-close can abandon a libtorrent session teardown.
- RISK: Default (no executor) dispatch-mode destruction from a foreign thread abandons terminal callbacks by contract.

PRODUCER READY. Agent 4 review and B-W3E formation remain open.
