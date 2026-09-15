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

Independent review found that ChokeAction(false) changed `locallyUnchoked_` on the caller
thread before the queued native `send_unchoke()` call. The repair separates desired state
from applied state. Caller submission records only the desired transition; the applied set
changes after the current connection's network-thread send call. Choke, detach, close, and
connection replacement clear desired and applied state conservatively.

The first pending-unchoke oracle mistook libtorrent's initial protocol UNCHOKE for the
requested transition. The raw peer now waits for an explicit adapter CHOKE before submitting
the held-window request. Because libtorrent may reject a request while wire-choked before the
plugin callback, the test also replays the exact callback admission path for the current
private connection identity. That replay makes removal of the applied gate deterministic;
the raw peer independently proves CHOKE -> early REQUEST -> requested UNCHOKE -> retry order.

[Agent 4 (Codex/Sol subagent), K10-H producer]
