# K05-A2 source trace

`SwarmPolicy.h` owns `EngineSwarmRegistry` and its peer lifecycle state. K05-A2 adds `PeerLifecycleCounts{queued, handshaking, ready}` and declares the generation-bound, const, noexcept `peerCounts(infoHash, generation)` query.

The optional result distinguishes absence or a stale generation from a current engine whose peer set is empty. Current peers are counted once in exactly one lifecycle state.

No `SwarmPolicy.cpp` implementation changed in this packet. The next implementation packet must provide the exported method body before calling or linking it.
