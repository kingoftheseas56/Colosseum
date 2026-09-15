# K06-F2 aggregate result

Fresh configure and focused build used the frozen Server 1.0 MSVC/C++17/Qt dependency configuration.

- Full candidate build: 70/70 build steps, exit 0.
- Full CTest inventory: 61/61 passed, exit 0.
- Focused K06-F2 stability: 3/3 consecutive executions passed.
- Legacy K06: 3/3 passed.
- Combined K06: 4/4 passed.
- Focused K03: 3/3 passed.
- Executable mutation gate: 8/8 rejected with exact patches and raw logs.

The review repair adds deterministic open-failure and Windows read-only stale-bitmap tests. A failed constructor repair throws before a `PersistentPieceStore` can engage in the test's optional owner.

This is deterministic packet and aggregate test evidence. It does not claim a live playback or real-device journey.
