# K11-v3 producer self-review

Reviewer: `[Agent 4 (Codex/Sol subagent), K11-v3 producer self-review]`. This is not independent acceptance.

- MET — Scope: only three authorized production files, K11 test/manifests, and K11-A artifacts change. Aggregate CMake, STATE, precursor headers, and rejected v2 history remain untouched.
- MET — Registry: canonical identity, alias-before-hook, async idempotence, shallow merge, fallback path, fresh ids, exact event order, cached next-turn, and remove/destroy/missing semantics are tested.
- MET — Lifetime: shared core, weak posted work, separate deferred lanes with inline rejection, real/injected timer cancellation, throw-after-continuation zero construction, and callback remove/destroy reentrancy are tested.
- MET — Pump: reader demand crosses SchedulerActionContract and K10; verification wire coordinates map privately to virtual buffers; stale ownership is rejected; retry is fresh; corruption resets its group.
- MET — Storage/read: exact out-of-order 524289-byte group stays invisible before commit, then reaches readers. Persistent upload checks commit; circular upload aborts; restored commits advertise one verification piece.
- MET — Availability/caps/search: verification availability expands to virtual pieces; stale snapshots are ignored; peer state, pause/caps, options, clock, and timer ticks use accepted contracts.
- MET — Isolation: premetadata creates/readers share E1; another infohash isolates transport, store, scheduler, settings, readers, and timer.
- MET — Oracle: exact M172 and controlled M814 match native normalized traces; committed-only divergence is explicit.
- MET — Verification: focused 9/9; mutants 11/11; fresh combined inventory 67; final combined 67/67 after recorded K10-G rerun.
- MET — Public consumer: test includes `EngineRegistry.h`, links K11, includes no cpp, and no TorrentEngine.h exists.

PRODUCER READY — Independent Sol code/oracle review, B-W3E review, Agent 4 acceptance, and feature integration remain open.
