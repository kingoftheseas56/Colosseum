# P08-T4 source trace

`TorrentTransport.h` owns the public transport action and statistics boundary. P08-T4 appends `PauseAction{generation, paused}` to `TorrentAction`, leaving the five prior variant alternatives at their existing indices, and appends `TransportStatistics::paused` with a false default.

The public contract is scheduler backpressure, not torrent suspension. Only current-generation state changes are accepted. Equal-state submissions are idempotent. Pausing defers only new outbound connects; existing peers, requests, metadata work, and incoming connections remain active. Resume drains deferred connects. Implementations may not call `torrent_handle::pause()` for this action.

No transport implementation changed in this packet.
