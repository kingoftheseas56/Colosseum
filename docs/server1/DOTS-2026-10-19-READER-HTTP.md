# Torrent file reader to HTTP checkpoint

This is an incremental component integration checkpoint, not complete server parity.

The production FileReaderStream adapter connects policy FileReader data and readiness
to async HTTP streams. It retains engine/store dependencies until response retirement,
bounds demand while waiting for data, retains partial chunks, propagates read errors,
and closes reader selections/pending reads on disconnect or server shutdown.

TorrentRange implements the pinned M176 first-valid-range policy, including malformed
or unsatisfiable ranges returning no range (the route will use a full response), suffix
ranges, multiple ranges and JavaScript parseInt semantics.

## Evidence

- Full suite: 108/108 passed, 0 failures, 37.90 seconds. Fixed-interface test profile,
  writable HOME/XDG directories and bundled PowerShell enabled controlled native wire tests.
- Focused real TCP fixture delivers exact reader bytes bcdefghijk for an inclusive
  1..10 range, waits for unavailable data without fabrication, and cancels on disconnect/stop.
- 25 range fixtures passed.
- 524 exact M176 module differential cases passed; parent independently reran this test.
  Oracle SHA256: 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.

The TCP reader fixture uses a controlled FileReaderSource, not real torrent network
transfer. Real peer-to-HTTP transfer, metadata-deferred routes, complete server runtime,
Windows and playback qualification remain open. No release or production merge.

Run the server CTest suite following LINUX-BUILD.md. For exact range differential use
native/colosseum_server_v1/tests/run_torrent_range_oracle.py with --oracle pointing to
the locked bundle, --index to its MODULE-INDEX.json, --executable to
server1_h01_range_test and --node to a qualified Node executable.
