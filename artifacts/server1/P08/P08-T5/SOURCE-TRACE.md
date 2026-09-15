# P08-T5 upload contract trace

Merge endpoint: fast-forward `feature/colosseum-server-1.0` only after independent Sol contract and code approval plus live upload gates. This branch is not an integration claim.

`TorrentTransport.h` owns the public upload mailbox boundary. The contract appends three actions after `PauseAction` and two observations after `AvailablePiecesObservation`; every earlier variant index remains fixed. `PeerPlugin.cpp` and `LibTorrent2Adapter.cpp` are intentionally untouched in this contract packet.

The frozen per-peer cap is four live admitted ownership records. The global cap is twenty per open transport generation: four requests across the donor's five default rechoke/upload slots. Both reset on generation replacement. This is smaller than libtorrent 2.0.11's default `max_allowed_in_request_queue` of 2000. These admission limits do not claim to bound queued response payloads or native send-buffer backlog. The implementation must intercept the custom path before libtorrent's default request queue.

Audit anchors:

- locked libtorrent 2.0.11 `include/libtorrent/extensions.hpp`: returning true from `peer_plugin::on_request` or `on_cancel` swallows the default handler.
- locked libtorrent 2.0.11 `src/peer_connection.cpp`: the default incoming request path otherwise queues disk-backed upload requests; the default cap is not the Server 1.0 cap.
- `docs/research/tankorent2-phase0/02-route-map.md`: donor `rechokeSlots` default is five.
- `PersistentPieceStore::isCommitted`: K11 must assert committed persistent bytes before reading them for an upload response. Circular-cache bytes are excluded because eviction can race advertisement and reads.

Existing `uploadedBytes` and `uploadBytesPerSecond` retain their native libtorrent meaning. `uploadPayloadBytesFramed` counts bytes copied into the native send buffer; it does not replace or synthesize the native measurement. `uploadActionsRejected` covers synchronous submit rejection and accepted mailbox actions that fail network-tick revalidation. `uploadRequestsAborted` is local terminalization; no wire reject is required.
