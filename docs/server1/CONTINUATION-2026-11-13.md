# Native server continuation, 2026-11-13

This checkpoint restores the previously delivered M07 work and integrates more
native services into the actual application. It is not a declaration of complete
server.js parity, Windows qualification, or a release.

## Recovery and authority

- Branch: `feature/colosseum-server-1.0`, based on
  `f32f84c963cec05e0b0c253ac5829f410dfe7854`.
- Recovered M07 source archive SHA256:
  `a6c04442b70728d31370fae0d73597a878b15bd10275aefe224e40c006aa7b06`.
  Its package verifier checked all 395 entries and the 18 changed-source hashes
  before the original working patch was restored.
- Exact oracle remains the official v4.21.1 desktop server bundle, embedded
  version 4.21.0, 6,676,503 bytes, SHA256
  `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
  Differential tests execute its pinned modules; the shipped candidate is C++.

## Integrated behavior

- Settings persistence, root redirect, heartbeat and network-info routes;
  source-compatible nested form parsing and runtime engine defaults.
- New-engine defaults and cache paths read current settings. Existing engines
  retain their storage paths. Engine inactivity retirement, idle pausing and
  cache cleanup are owned by the composition's Qt application lane.
- HLS concurrency and conversion settings update on settings POST. New tracks
  see updated settings; running conversions retain their option snapshots.
- Recovered legacy media indexing/rendering plus shared preparation ownership,
  independent consumer cancellation, reentrant completion and teardown tests.
- Downloader concurrency repair and mounted ZIP/TAR/GZIP routes with bounded
  asynchronous byte production and cancellation.
- Opt-in application integration through `COLOSSEUM_ENABLE_SERVER1=ON` at build
  time and `COLOSSEUM_SERVER1=1` at runtime. The native server owns an ephemeral
  loopback listener; the existing Stremio adoption/child path remains default.
- Startup queues play/prefetch calls made synchronously from `readyChanged`
  until the tuning POST settles. Stale tuning replies cannot flush another
  startup's queue. Both success and failure permit subsequent playback.
- Owning JSON values replace references into destroyed temporary objects in
  FFmpeg encoder/decoder selection and archive length parsing. Independent
  optimized validation exposed the same error in the downloader test: the
  original test crashed five out of five times; materializing its status value
  passed ten out of ten, without changing downloader behavior or expectations.

## Practical execution evidence

An independent frozen-source Release rebuild passed all 175 registered tests,
with zero failed or skipped tests, in 145.69 seconds. The source inventory's
SHA256 is `46291fc2710a40ced17e15906f0c9f737b56633f58752208186567e47108ab6c`.
The inventory includes the exact-source extraction helper required by the
oracle runners. Earlier incomplete harness packaging and the failing JSON
reference test were retained separately; they were not relabeled as passes.

The real application was built with Qt 6.11.1, GCC 14.2 and static libtorrent
2.0.11. A disposable-profile offscreen run started the in-process server and
verified heartbeat, settings, local-media HLS master/playlist/init/segment and
independent FFmpeg decoding. Browser sandboxing was not disabled. This is not
an interactive desktop or GPU qualification.

Controlled lawful fixtures also verified tracker discovery, actual peer wire
transfer, verified torrent piece delivery, ranged HTTP, HLS generation and
decode. The application bridge's test verifies owned-listener shutdown.

A separate public-network probe requested only 65,536 bytes of the official
Ubuntu 26.04.1 desktop ISO torrent with a 75-second bound. Metadata became
available and the HTTP response reached 206, but all 24 telemetry samples had
zero peers, wires and downloaded bytes. A separate HTTPS tracker request
returned 503. The native server's controlled transfer works; that public probe
does not prove that public peers are reachable here, nor that all cloud P2P is
blocked. It did not download the ISO.

## Remaining work and explicit differences

The isolated FTP/RAR/7z/NZB work is not part of this core checkpoint until its
production mounting and socket tests are recorded. Samples/device information,
hardware profiling, casting, YouTube, local-addon/indexing, HTTPS and all required
platform profiles must be accounted for before a complete parity claim.

The following known limits must not be hidden by successful component tests:

- Cold `waitForSubs` in the pinned source throws a ReferenceError. Native code
  currently uses available subtitles without waiting; this is a documented
  behavior difference, not reproduction of that source crash.
- No provenance-verifiable compatible ffsplit executable was found. Its missing
  executable error is tested; actual splitter output remains unqualified.
- Bundled FFmpeg successfully encoded routed output, but the bundled FFmpeg and
  FFprobe decoders crashed on generated MPEG-TS. Independent system decoding
  passed. See the M07 qualification for the precise distinction.
- Archive malformed-input bounds and valid keep-alive framing deliberately do
  not reproduce source hangs or advertised-length overreads. See each archive
  contract for these differences and transport limits.
- Linux fixed-interface fixture runs do not qualify live interface changes;
  Windows packaging/runtime and public-peer reachability are still unproven.
- Network exposure, certificate validation and security settings were not
  expanded to make restricted-environment tests pass.

Reproduction details: [Linux build](LINUX-BUILD.md),
[legacy media qualification](M07-RUNTIME-QUALIFICATION-2026-11-13.md), and
[downloader/archive qualification](DOWNLOADER-ARCHIVE-2026-11-13.md).
