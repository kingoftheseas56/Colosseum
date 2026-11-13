# Native protocol checkpoint, 2026-11-13

This extends the tested core checkpoint with native FTP, NZB/NNTP, RAR/7zip,
legacy YouTube resolution, local-addon HTTP/metadata components and casting
protocol/transcoding components. It is an opt-in development checkpoint, not a
release or a claim of complete server.js parity.

## Integrated and tested

The production composition mounts FTP/NZB/archive/YouTube/casting/local-addon
routes before the torrent catch-all. Local-addon catalog support is exposed,
but automatic scanning and online recognition are not enabled. Passive casting
returns an empty list until explicit discovery data is supplied.

A clean independent Release rebuild passed **197/197 tests**, with zero skips,
in 170.85 seconds under the Node 24.22.1 oracle profile. Frozen input manifest
SHA256: `d4c09ee4be8d90b3cb9d9358aaecb0de8a928d351a51ce912b925b3ebfe33f84`.
CTest log SHA256: `78f6e7e512858401660a2486d8fcbbbcd1f2051128b910707aad68552848587b`.
The test-host binary SHA256 is
`70a536415581709d1aa4706ac39aa4438c15cd062013e7e6086121bc1c8c80d4`.

Real loopback servers verified FTP metadata, persistent byte production, range
requests and cancellation; NNTP article decoding, request coalescing, pools,
shared cancellation, cache, direct HTTP and nested archive delivery; and
RAR/7zip range and HEAD delivery. Production-host smoke tests verified FTP and
NNTP direct/ZIP ranges and shutdown while requests were pending.

The real application was rebuilt with these routes and tested offscreen using
a disposable profile. Heartbeat, settings, passive casting, local-addon manifest
and catalog, local-file HLS output, and independent FFmpeg decode passed. This
run did not disable browser sandboxing or scan real user media.

A callback-lifetime review found that a YouTube callback could destroy its
network reply before cleanup. The old source reproduced an AddressSanitizer
failure. Scheduling reply cleanup before invoking the callback passed the
same regression ten consecutive times; a deterministic lifetime test is now
registered in the aggregate suite.

## Separately recorded runtime profile

The official Node 22.16.0 executable was verified against its official SHA
manifest. The unchanged native binaries passed 33/36 registered oracle commands
under that profile. Three differences remain in this checkpoint:

- The newer standalone regex backend recognizes Unicode 17 additions and
  duplicate named groups that the pinned Node 22 runtime does not.
- System-statistics comparison rejects the native libuv 1.52.1 versus Node 22
  libuv 1.49.2 mismatch rather than silently claiming parity.
- Guess-file-index has a Node 24 runner guard. Its separately supported portable
  Node 22 mode passed all 21,080 cases without changing native code.

The passing Node 24 aggregate is not substituted for Node 22 qualification.
These runtime-profile differences require a subsequent compatibility change.

## Scope limitations

- Public P2P transfer remains unqualified; the core's controlled real peer test
  passed, but its bounded public probe transferred no bytes.
- FTP TLS and NNTP TLS, complete split-archive combinations, Windows packaging,
  GPU success paths and full active LAN casting require further work or tests.
- Current public YouTube markup produced the same legacy decipher-extraction
  error in both the pinned source and native resolver. Current YouTube playback
  is not advertised as working.
- Casting primitives and FFmpeg output are tested, but production Chromecast
  sessions, complete DLNA orchestration and launching external players are not
  implemented or qualified by these passive routes.
- Local indexing/persistence integration and hardware profiling are subsequent
  work; no copied source sample media is included in this checkpoint.
- Defensive input, memory, cancellation and framing limits remain intentional
  documented differences rather than reproducing source hangs or overreads.

Detailed contracts: [FTP](../../native/colosseum_server_v1/docs/FTP-SOURCE-PARITY.md),
[NZB](NZB-SOURCE-2026-11-13.md), [RAR/7zip](RAR-SEVENZIP-PARITY.md),
[YouTube](YOUTUBE-PARITY-2026-11-13.md),
[casting](CASTING-NATIVE-QUALIFICATION.md), and
[local addon](LOCAL-ADDON-2026-11-13.md).
