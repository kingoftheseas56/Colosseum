# M06 local HLSv2 service qualification

Implemented the exact-source converter registry and HTTP routes, not the old HLS implementation. Production dependencies are the native Probe, sample indexers, HlsPlaylist and TrackConverter.

## Source identity

Locked bundle: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
Factories 853 (routes), 865 (master converter), 432 (probe normalization), 437 and dependencies (mutex), 914 (master playlist) are executed by `tests/run_hlsv2_oracle.cjs`; only process, router callback registration, source retrieval, and track-construction boundaries are substituted for the factory tests. Real FFmpeg/process/media/router/HTTP tests are separate and use no substitute production converter.

## Tests

- RED: initial test compilation rejected missing HlsV2Service.h before implementation.
- 13 exact factory query/validation cases, including JavaScript number serialization and codec query arrays.
- 24 converter action comparisons: first-video filtering, audio/subtitle enumeration, ignored bitmap subtitle codecs, live media and missing AV errors, error caching, status and destroyed behavior.
- 9 exact route-factory results: create, playlist/init/media/burn/status errors, omitted PROBE_FAILED code, destroy leaving the registry entry.
- Virtual clock strict 120-second cleanup threshold, source LRU, per-track-type eviction, simultaneous acquire identity, pending-probe interruption, late callbacks, reentrant eviction and abandoned responses.
- Real tiny.mp4: master, legacy-shaped alias with forced two audio channels, video media playlist, init fragment, media fragment, audio playlist, real loopback HTTP probe.

## Build and execution

Isolated build only: `server-parity/build-m06`, never the shared integration build. The external scratch CMake links native media sources, HTTP router/parser/connection, Qt Core/Network, Boost headers and zlib. Commands and exits are recorded in receipt.json. Test executable requires the fixture JSON and real-media file. `tests/cmake/M06.cmake` supplies the integration CTest registration.

## Remaining qualification boundaries

This is a local Linux lane, not full G-MEDIA-V2 approval. Windows process-fixture/native-media runs, broader playback corpus and production composition remain receiving/integration responsibilities. HLSV2_REMOTE streaming/backpressure is not connected by the current M05/ProcessDriver constructor; do not mark remote media qualification complete. Subtitle burn and subtitle-media rendering are owned by M05; M06 routes and error mapping exist, but this receipt does not claim an additional real subtitle playback test. No commits, pushes, releases, visible local user app or user computer changes were made.

ASan+UBSan passed with leak detection disabled. LeakSanitizer itself failed in the ptrace execution environment, so this is not a leak-clean claim.
