# Incoming engine capability checkpoint

Pinned oracle remains SHA256405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.

Native incoming peers now receive handles/plugins after torrent association, even when no outbound endpoint binding exists. Actual callback events feed the same generation/identity-owned lifecycle. Incoming handshakes add connections but not outbound retained unique endpoints or connectionTries; incoming closes retire them. Pausing outbound admission does not prevent incoming acceptance.

M818's incoming wire.peerAddress uses connection.address(), which in Node is the local listener endpoint. Native PeerTransferStatistics.address stays the remote endpoint; new sourceAddress separately carries the source projected address. Outgoing addresses remain remote. Projection tests distinguish both fields.

Tests:
- Actual loopback TCP incoming handshake while paused: PASS. Checks Started/Ready with no invented outgoing Connected callback, zero actual outgoing attempts, both address fields and native disconnect retirement.
- Engine snapshot lifecycle and sourceAddress projection: PASS.
- Pinned M818 differential expanded25→30 states, including incoming connections versus retained outbound endpoints, paused queue behavior, incoming close and resume. Also checks actual source incoming local address assignment.
- Existing two real-wire statistics and pause/generation tests:3/3 PASS.

Limits: this qualifies engine capability, not production listener reachability. M818 creates a listener only when swarm.listen is called; M814 exports engine.listen, but M172 contains no direct call. The native adapter already had an ephemeral loopback listener before this change. Listener-default equivalence remains to be decided from exact call graph. Incoming timeout before torrent association is still native; the app's source handshake timer cannot yet see such a socket. Raw peer address validation, alias identity and asynchronous DNS remain pending. No blanket PWS parity claim.
