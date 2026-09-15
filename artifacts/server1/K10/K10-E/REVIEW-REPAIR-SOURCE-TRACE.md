# K10-E independent-review repair source trace

The repair is limited to the accepted public transport header, libtorrent adapter, native
transport test, and additive K10-E evidence.

- `MetadataReadyObservation` now defines tracker and URL-seed vectors as lexicographically
  sorted, deduplicated snapshots of active values at readiness.
- Both `add_torrent_alert` and `metadata_received_alert` pass their live torrent handle into
  ready construction.
- Ready construction snapshots `torrent_handle::trackers()` and
  `torrent_handle::url_seeds()` from locked libtorrent 2.0.11.
- Immutable `torrent_info::trackers()` and `torrent_info::url_seeds()` remain fallback inputs.
- `std::set` provides deterministic sort and dedup semantics across live and fallback values.
- An invalidated handle is caught locally; close/stale suppression still occurs under the
  existing observation lock and no error escapes the factory or poll API.
- The real magnet test now supplies distinct `tr` and `ws` parameters and asserts the exact
  public values. It would fail if ready construction reverted to torrent-info-only state.

No request scheduling, ownership permit, native write firewall, or peer connection path changed.
