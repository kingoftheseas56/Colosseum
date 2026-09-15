# K10-H review repair 1

Repair base: `7abe3d1ab26529ca6200e010a2a9c3ad2ca50e90`.

The reviewed defect was a caller/network-thread state mismatch. Submitting
`ChokeAction{peer, false}` marked the peer locally unchoked before the queued native
`send_unchoke()` call ran. An incoming request in that window could receive ownership while
the requested wire UNCHOKE had not been sent.

The additive repair keeps desired and applied local-unchoke state separate. Caller submission
only records desired unchoke. The network thread records applied unchoke after calling
`send_unchoke()` for the current connection identity. A newer choke, detach, close, or
replacement clears the state; an older queued unchoke cannot reapply it. Choke submission
still terminalizes current upload ownership conservatively before its wire control dispatch.

The live regression proves the sequence with raw BitTorrent frames: explicit CHOKE, request
while unchoke dispatch is held, no ownership, requested UNCHOKE, then a same-block retry with
a new nonzero ownership. The compiled mutation deleting the applied-state admission gate is
rejected by early ownership admission.

Fresh repair verification: live scenarios 4/4 with three consecutive full-run repetitions;
behavioral mutations 7/7; K10/E/G/H 19/19; P08-T5 19/19; legacy P08/T4 19/19; fresh Release
build 77/77; aggregate 64/64; INT-W3 spine combined-link PASS; public path guard PASS.

This remains unintegrated and pending independent Sol re-review.

[Agent 4 (Codex/Sol subagent), K10-H repair producer]
