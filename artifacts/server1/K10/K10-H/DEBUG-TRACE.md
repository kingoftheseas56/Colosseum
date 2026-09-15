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
the held-window request. The first repair used a synthetic callback replay because libtorrent
might have rejected the request before the plugin. Independent review correctly required the
live proof: the final test waits for the raw request to increment the callback-derived rejection
counter before releasing dispatch, and uses distinct piece 1 after raw UNCHOKE so only the
post-wire request can create ownership.

The first repair still dequeued control actions outside the adapter mutex. A newer accepted
choke could clear desired/applied state, yet the already-dequeued old unchoke would still call
the native send function before declining to mark applied state. The final network-thread path
waits at the test barrier, then locks and atomically validates current native identity plus
desired state, calls `send_unchoke()`, and records applied state. Caller submission uses the same
mutex, so either the unchoke send/applied update wins first or the newer choke wins and the stale
wire send is skipped.

[Agent 4 (Codex/Sol subagent), K10-H producer]
