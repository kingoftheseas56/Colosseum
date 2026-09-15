# K11 producer self-review

Reviewer: `[Agent 4 (Codex), producer self-review]`. This is the required self-review reflex, not independent K11/B-W3E acceptance.

- MET — Frozen scope: the staged diff contains the two owned production cpp files, test, two K11 manifests, the sole F10-authorized `EngineRegistry.h`, and K11-A artifacts; aggregate CMake and STATE are unchanged.
- MET — Identity/options: K11-01 proves mixed-case canonical reuse, stream-to-torrent alias before every hook, asynchronous/reordered/idempotent continuation, shallow merge, falsy path fallback, and fresh ids.
- MET — Lifecycle ordering: the diff emits create then created on first construction, scoped then global ready/error, and only then completes the create callback.
- MET — Ownership/reuse: K11-01/02 prove one construction, one transport autonomy handoff, resume and option replacement on every create, new-only metadata components, and cached callback deferral to the next dispatch turn.
- MET — Sharing/isolation: K11-02 proves two premetadata creates and two readers share one three-file engine; K11-03 proves a second hash has distinct generation, transport, store mode, settings, and timer identity.
- MET — Nonblocking/accepted seams: continuation only enqueues dispatcher work; construction does not poll or wait; the implementation consumes the accepted transport, scheduler, store, reader, peer-search, and autonomy contracts without signature edits.
- MET — Reader/store lifetime: `TorrentEngine::close()` closes every live reader before transport, peer-search, store, scheduler, and adapter release; K11-02 observes the reader closures and single transport close.
- MET — Generation safety: K11-03 proves G1 removal completes its callback once as Removed, closes G1, creates a fresh G2, and stale G1 metadata/failure cannot affect G2; held requests finish once as Cancelled.
- MET — Source terminality/source quirk: SourceFailure and malformed metadata become terminal error without fake ready; circularBuffer still requires buffer; reuse does not rebuild new-only components.
- MET — Verification: focused K11 is 3/3 including the real production metainfo transport path, and the unchanged predecessor aggregate is 59/59.
- MET — Public consumer: the test includes only `server1/policy/EngineRegistry.h`, links `server1_k11_engine`, includes no cpp, and no `TorrentEngine.h` exists.
- MET — Evidence/gating: WIRING-REQUEST, source parity/divergence trace, commands, receipt, and blob seals are present; all state says candidate only pending independent Sol K11, B-W3E, and Agent 4 acceptance.

APPROVE — The staged K11 candidate meets the frozen producer Definition of Done; independent acceptance gates remain intentionally open.
