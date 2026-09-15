# K10-E source trace

Production implementation is confined to
`native/colosseum_server_v1/src/transport/LibTorrent2Adapter.cpp`.

- `sourceParams` translates the public source union into libtorrent parameters, validates
  non-zero generation, save path, parse success, and exact binary v1 hash consistency.
- `openTorrentTransport` is `noexcept`; validation or construction errors return a live failed
  transport which exposes one source-owned failure through `poll()`.
- `LibTorrent2Adapter` uses `async_add_torrent`. It handles both `add_torrent_alert` (including
  cached metadata) and `metadata_received_alert`.
- Ready payload is copied immediately from `torrent_info::info_section()`; trackers and URL
  seeds are copied separately. It is not represented as reconstructed full metainfo.
- Source observations have no callback path. Construction only queues work; observations are
  visible to a consumer on a later `poll()` and are deduplicated exactly once.
- `ConnectAction` is generation checked and submitted through the public control seam. The
  legacy helper delegates to that seam and no longer downcasts for connection control.
- Pending connects wait for the async torrent handle. Close clears them, removes queued source
  effects, serializes session teardown against alert collection, and leaves one close event.
- Piece payload requests remain `RequestAction` values with `RequestOwnership`; the existing
  native request permit/firewall path is unchanged. K10 legacy wire proof remains 10/10 green.

Tests are confined to `native/colosseum_server_v1/tests/test_native_transport.cpp` and packet
registration to `native/colosseum_server_v1/tests/cmake/K10.cmake`.
