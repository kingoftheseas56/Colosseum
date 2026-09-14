# K10-A source trace

Worker: K10-A. Parent: K10. Base: `98d1bca9a0cb4045c3839dc3ee4dc9fde48283a5`.

The accepted oracle is SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
Inspected transport tuples are M814 lines 72439-72764 SHA-256
`05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`, M818 lines
72812-72975 SHA-256 `eab958a614c693923c1114bae213dadd73d977a87fd640874402418bbcfa493e`,
M843 lines 74473-74529 SHA-256 `520011da2b4a68a3c71f967416cda41badb047ade587672ef031b1edce9efa30`,
and M851 lines 74856-74884 SHA-256
`2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041`.

The production adapter is bound to libtorrent 2.0.11.0 revision `6e1587799`. The linked archive is
`C:/tools/libtorrent-2.0-msvc/lib/torrent-rasterbar.lib`, 320838716 bytes, SHA-256
`a2d67f24710303750aaf068d1945380d95434e4791ad611b68b0b3b055d89a30`.

The scheduler-owned transaction runs inside the selected peer's libtorrent network-thread callback.
All pieces remain `dont_download`. After both controlled peers advertise, the adapter validates the
selected peer, exact block, active ownership, connection state, remote choke state, and empty native
request/download queues. It installs one exact permit, creates `libtorrent::cork`, calls
`add_request(piece_block)`, requires/logs `queued=1`, then calls `send_block_requests_impl()` while the
cork lives. `write_request` consumes the permit atomically as an assertion firewall. `sent_request`
only records framing. No deferred `send_block_requests()` call is used.

K10-01 maps to exact production request framing and controlled-peer socket comparison. K10-02 maps
to multi-peer ledger order, failed-head retry, exact cancellation, endpoint rebinding, independent
infohash adapters, stale/late disconnect handling, and deterministic stop while `on_piece` is active.
K10-03 maps to rejection/accounting of forbidden caller-thread native access.

`close_redundant_connections=false` is confined to this scheduler-owned session because locked
libtorrent otherwise closes zero-priority controlled peers before the readiness barrier. DHT, LSD,
UPnP, NAT-PMP, auto-management, and encryption negotiation remain disabled for this bounded session.
