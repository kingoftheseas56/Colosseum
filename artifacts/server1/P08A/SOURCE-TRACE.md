# P08A source trace

## Behavioral oracle

The locked oracle remains the authenticated Stremio Service CDN v4.21.1 `server.js`
(6,676,503 bytes, SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`).

P08A is deliberately narrower than scheduler parity. It asks whether the P01A-frozen
libtorrent 2.0 substrate can support the source ownership model at the peer/block seam.

The plan names M814 (plan coordinates L72439-L72764) and M851
(plan coordinates L74856-L74884). Content re-location in the parsed authenticated bundle
finds the same anchors at:

- M814 request path around parsed lines 73415-73425: `PieceBuffer.reserve()` selects a
  reservation, derives offset and size, records the selected `wire`, then calls that wire's
  `request(index, offset, size, cb)`.
- M814 hotswap around parsed lines 73382-73409: reservations held by a slower wire are
  reclaimed back into PieceBuffer without an explicit `wire.cancel()` in that module.
- M851 PieceBuffer around parsed lines 75723-75749: block size is 16,384 bytes, the final
  block uses the remainder, reservations are monotonic, and cancellations are reused LIFO.

These content anchors, not line-number coincidence, define the P08A behavior.

## Frozen libtorrent seam

P01A accepted lane-specific libtorrent 2.0 builds:

- Windows: libtorrent 2.0.11.0, revision `6e1587799`, ABI 3.
- Linux: libtorrent 2.0.10.0, revision `dacf64c50`, ABI 1.

Upstream 2.0.10 and 2.0.11 expose the same relevant shape:

1. `peer_connection_handle` is the supported plugin-facing peer handle and exposes
   `send_buffer()` plus a low-level `native_handle()`.
2. `peer_connection::no_download(bool)` is an inline low-level switch.
3. The ordinary picker entrypoint `request_a_block()` returns immediately when
   `c.no_download()` is true.
4. `peer_plugin::on_piece()` can consume an incoming piece before the default handler.

The spike intentionally does **not** call `peer_connection::add_request()` or
`send_block_requests()`. Those members live on a `TORRENT_EXTRA_EXPORT` class and normal
binary packages are not guaranteed to export extra test symbols unless
`TORRENT_EXPORT_EXTRA` was used. P08A must not mutate or rebuild the frozen dependency just
to make the gate pass.

Instead, the probe uses:

- supported plugin callback context for thread ownership;
- supported `peer_connection_handle::send_buffer()` to emit the exact BitTorrent request
  on one chosen peer;
- exact-version `native_handle()->no_download(true)` only to suppress libtorrent's own
  picker on that peer;
- `peer_plugin::on_piece()` to consume the owned response.

Seam classification before execution: **public plugin API plus narrow exact-version
internal-header state access; no dependency patch**.

## Adversarial control

Both scripted peers advertise one 32 KiB piece, which contains two eligible 16 KiB blocks.
The workflow first runs `P08A-CONTROL` with picker suppression disabled and requires
ordinary libtorrent request traffic. Only then do P08A-01/02 enable owned mode. This prevents
a false PASS caused by having no eligible picker work.

## Acceptance

The spike remains test-authored until CI wire evidence proves all of:

- P08A-01: exactly one `(piece=0, offset=0, length=16384)` request reaches peer B and none
  reaches peer A.
- P08A-02: the control proves ordinary picker work exists, but owned mode produces zero
  unowned requests during the observation window while the B response is received.
- P08A-03: the same owned request may remain outstanding while the torrent/session is
  destroyed; both sockets close, the process exits cleanly, and no replay request appears.

The probe is disposable evidence and must not be promoted into production source.
