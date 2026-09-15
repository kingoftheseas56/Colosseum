# K10-F producer self-review

Written DoD: additive full peer-availability snapshots with generation; initial bitfield replacement including empty; full HAVE update; current detach clear; reconnect isolation; stale disconnect and all old-connection callback isolation; preserved wire firewall/lifetime; focused and predecessor gates; bounded ownership/evidence.

- MET — Public observation carries generation, peer, and piece vector in the separately committed P08-T3 contract.
- MET — Adapter clears and rebuilds the set on every current-identity bitfield, then emits the set-backed sorted/deduplicated full snapshot.
- MET — New HAVE emits the full updated set; duplicate HAVE is intentionally suppressed by the set insertion result.
- MET — Current detach erases availability and emits an empty snapshot after identity validation.
- MET — Replacement handshake clears inherited availability; real reconnect with an empty bitfield remains empty.
- MET — Stale disconnect returns before availability/state removal; the real replacement stays connected and emits no stale clear.
- MET — PeerPlugin and Adapter carry/check identity for HAVE, state, authorize, framed, and payload callbacks; the test replays all five old-identity lanes against a live replacement.
- MET — Generation 404 is asserted on initial, detach, and replacement snapshots.
- MET — Scheduler ownership and one-shot wire authorization remain intact; legacy K10 passes 10/10 including wire, lifecycle, and thread guard.
- MET — K10-E passes 7/7, prior aggregate passes 59/59, K10-F passes twice, and public guards pass.
- MET — Diff is limited to the P08-T3 commit, K10-owned adapter/plugin/test, packet-local CMake, and new packet evidence; no aggregate, STATE, K11, or old receipt changed.

APPROVE — The producer diff meets the written K10-F DoD and is ready for independent Sol review and Agent 4 acceptance.

[Agent 4 (Codex/Sol subagent), P08-T3/K10-F producer self-review]
