# K10-H review repair 2

Repair base: `6dfb4838a5b7c2d326e46ba92cde7499f46555f5`.

Two Important findings are repaired additively.

First, dequeuing an unchoke did not linearize its native send against a newer caller choke.
The network thread now waits at the deterministic test barrier, acquires the adapter mutex,
validates the current native connection and desired-unchoke state, calls `send_unchoke()`, and
records applied state before releasing that mutex. Caller choke acceptance uses the same mutex.
Therefore the unchoke send and applied update win first, or the newer choke wins and the stale
wire UNCHOKE is skipped.

Second, the pending-unchoke proof no longer uses synthetic callback replay. A raw peer sends
piece 0 while unchoke dispatch is held; the test waits for that real plugin callback to increment
`uploadRequestsRejected` before release and proves no ownership. After the peer observes the
requested raw UNCHOKE, it sends distinct piece 1, which alone creates one nonzero ownership.

The same raw peer proves the opposite ordering: after a later unchoke is dequeued into the
pre-send barrier, a newer choke is accepted before release. No stale raw UNCHOKE appears and
upload ownership/accounting remains unchanged. Compiled mutations remove the pre-send desired
check and raw rejection accounting independently; both are rejected on their exact signals.

Fresh verification: live scenarios 4/4 with three consecutive full-run repetitions;
behavioral mutations 9/9; K10/E/G/H 19/19; P08-T5 19/19; legacy P08/T4 19/19; fresh Release
build 77/77; K10-F real-wire 2/2 consecutive; aggregate 64/64; INT-W3 spine combined-link
PASS; public path guard PASS.

This remains unintegrated and pending independent Sol re-review.

[Agent 4 (Codex/Sol subagent), K10-H repair 2 producer]
