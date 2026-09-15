# P08-T2 successor contract trace

P08-T2 is the smallest public successor to accepted P08-T. It preserves the scheduler-owned request plane and adds only source/open lifecycle types in `TorrentTransport.h`.

The open request is not default constructible. It carries an engine generation, mandatory binary 20-byte v1 identity, one source variant, and save path. Binary identity removes length and case ambiguity at the public boundary. The variant makes bare-infohash, magnet URI, and cached metainfo mutually exclusive. The public action variant owns peer connection requests through `ConnectAction`; no consumer needs a transport implementation downcast.

Metadata readiness is generation-bound and exposes the real libtorrent `torrent_info::info_section()` bytes plus trackers and URL seeds. It is not described as a reconstructed full `.torrent` round-trip. Source failures carry the same generation and binary identity. The public API has no callback surface: production queues readiness, and the consumer receives it only from a later `poll()` call.

`ConnectAction` adds public source control only. Payload requests still require scheduler-issued `RequestOwnership`, and K10's one-shot permit/firewall remains the only path to wire requests.

Merge endpoint: fast-forward `feature/colosseum-server-1.0` only after independent Sol review and Agent 4 acceptance.
