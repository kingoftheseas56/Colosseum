# K08-A source trace

Worker: K08-A
Oracle: `C:/b/Colosseum-Server-1.0-Planning-Pack/oracle/stremio-service-v4.21.1-server-bundle/server.js`
Whole-oracle SHA-256 observed before implementation: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

The accepted frozen worker record names these source authorities:

- M180 lines 18647-18683, recorded SHA-256 `a3b12006e483336df682632e2fa4b9bd27f3e6f3b25a33f018aec1dd923154f`: end-of-stream and premature-close behavior.
- M201 lines 21473-21475, recorded SHA-256 `9f5cb2453acb0171fbc698ce252373fe1d4743d0840a4a1ea06c5b06e8371c92`: bounded bagpipe dependency used by FileStream.
- M846 lines 74730-74772, recorded SHA-256 `fdff76e6c1b5a55a544a9de1d52f7882a63aef8a8d64a3425cd9f0218391c727`: FileStream range geometry, two-read queue, lock lifecycle, missing-piece wait/notify/critical/refresh ordering, clipping, window movement, EOF, and destroy.
- M847 lines 74772-74780, recorded SHA-256 `690450006b7c248bdc87c9a70f9c9330ba9c8b598d44e5e71acc0387981f838f`: readable-stream boundary.

Candidate mapping:

- `FileReader` retains inclusive start/end and maps file-relative positions through `TorrentFile::offset`.
- `FileReaderSource` is the explicit async store boundary. It owns actual piece availability/read/cancel operations; the reader owns tokens, generation, ordering, locks, clipping, and cancellation.
- Consumer `request(bytes)` is the demand signal. The reader admits at most two piece reads and does not advance merely because a socket or source exists.
- Missing pieces mark the source-derived critical span, invoke refresh once, and resume only after an availability notification.
- Out-of-order completions are held until the read head can deliver them. Closed/destroyed readers cancel their tokens; late completions are inert.
- Buffer pieces are `floor(bufferBytes / pieceLength)`. At 512 KiB pieces, 15 MiB is 30 pieces and the 1 MiB critical span is two pieces.

No production `.cpp` is included by the test. The packet CMake builds `server1_k08_file_reader` and the test links that production library plus the accepted K03/K00 libraries.
