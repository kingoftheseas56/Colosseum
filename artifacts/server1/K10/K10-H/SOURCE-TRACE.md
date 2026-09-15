# K10-H source trace

- `native/colosseum_server_v1/src/transport/PeerPlugin.cpp`: private connection identity,
  independently preserved remote choke/interest state, and swallowed inbound request/cancel.
- `native/colosseum_server_v1/src/transport/LibTorrent2Adapter.cpp`: exact upload ownership,
  admission caps, advertisement replay, caller mailbox, network-tick revalidation, raw
  HAVE/PIECE framing, lifecycle terminalization, and truthful counters.
- `native/colosseum_server_v1/tests/test_native_transport.cpp`: final-tail, cap, replay,
  duplicate, stale mailbox, choke, detach, close, and counter assertions.
- `native/colosseum_server_v1/tests/cmake/K10.cmake`: registered K10-H real-wire gate.
- `artifacts/server1/K10/K10-H/upload_peer.py`: exact HAVE/PIECE/cancel feasibility oracle.
- `artifacts/server1/K10/K10-H/upload_burst_peer.py`: multi-peer admission/replay oracle.
- `artifacts/server1/K10/K10-H/upload_lifecycle_peer.py`: choke/detach/close wire oracle.
- `artifacts/server1/K10/K10-H/run_behavioral_mutations.ps1`: six compiled live mutations.

K10-H never reads persistent or circular storage. AdvertisePieceAction is only an upstream
commit assertion. K11 remains responsible for `PersistentPieceStore::isCommitted` before
reading persistent bytes; circular-cache upload remains forbidden.

[Agent 4 (Codex/Sol subagent), K10-H producer]
