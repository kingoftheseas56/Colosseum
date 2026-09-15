# K10-H debug trace

The first feasibility listener accepted TCP but saw no adapter frames. The raw fixture's
peer ID was 19 bytes, so libtorrent consumed one byte of the next message as handshake data.
The fixture now pads and truncates every peer ID to exactly 20 bytes.

The first multi-peer cap run lost one connection because every fixture used the same peer ID.
Unique peer IDs removed libtorrent's duplicate-peer defense from the oracle.

The sixth-peer replay check initially raced delayed rejection accounting from the first five
peers. The test now saturates peer 401 before fan-out, stabilizes the rejection baseline, and
requires six connected peers plus a new rejection. This also made the per-peer cap mutation
deterministic instead of allowing the global cap to mask it.

Self-review found that the adapter's early closed/uncontrolled gate rejected late upload
actions without incrementing `uploadActionsRejected`. The gate now counts advertise,
response, and abort rejection there, and lifecycle verifies a late post-close abort.

[Agent 4 (Codex/Sol subagent), K10-H producer]
