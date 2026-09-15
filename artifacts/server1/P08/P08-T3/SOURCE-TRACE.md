# P08-T3 peer availability contract trace

`AvailablePiecesObservation` is an additive public observation in `TorrentTransport.h`. It carries the adapter's `EngineGeneration`, the stable `PeerHandle`, and a full vector snapshot of the pieces currently advertised by that peer.

The contract defines empty as a clear and requires producers to publish sorted, deduplicated snapshots. K10-F owns the wire-to-snapshot behavior; P08-T3 owns only the public type, variant membership, compile consumer, and mutation proof.

Payload requests remain scheduler-owned. This amendment adds no request authority, callback, transport downcast, or implementation header.

Merge endpoint: fast-forward `feature/colosseum-server-1.0` only after independent Sol review and Agent 4 acceptance.
