# K10-F source trace

`PeerPlugin.cpp` now attaches its monotonic `ConnectionIdentity` to HAVE, peer-state, write authorization, sent-request framing, and piece-receive callbacks. `LibTorrent2Adapter.cpp` accepts those identities only when they match the current peer binding. HAVE, authorize, and framed reject under the adapter lock; state rejects before reading the native connection and rechecks before statistics; receive rejects before its barrier and rechecks before delivery.

Initial bitfield handling requires the current handshake identity, clears `advertised_[peer]`, rebuilds it from the bitfield, and emits a full snapshot. The existing set provides sorted and deduplicated output. A new HAVE inserts once and emits the complete updated set. A current-identity detach erases the set and emits an empty snapshot. Handshake replacement also clears the prior set; stale disconnect returns before erase or emission.

The internal test seam replays the last detached identity through all five non-detach callback lanes against a live replacement and a live pending request. It is not part of the public port. Payload authority remains the scheduler-issued ownership path and the one-shot native write firewall.
