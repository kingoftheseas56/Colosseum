# K12 statistics checkpoint, 2026-10-19

Status: source projection and measured live statistics implemented; complete PWS connection lifecycle parity remains open.

Oracle: locked execution-v2.1/evidence-pack/oracle/stremio-service-v4.21.1-server-bundle/server.js, SHA256 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f, M172 getStatistics lines 18294–18339. The other server.js bundle was not used.

## Implementation
- EngineRegistry::statistics(hash, Value index = missing) returns null for unknown/removed engines.
- TorrentEngine::statistics(index) projects actual engine state and native transport counters through EngineStatisticsSnapshot/projectEngineStatistics.
- Source-specific uploadSpeed=downloadSpeed is preserved. Present index, even invalid, yields wires:null; absent index yields unchoked wire records. Missing peerSearch keys serialize absent; pre-metadata name/files serialize null.
- Source file progress counts intersecting committed virtual pieces divided by ceil(file.length/pieceLength), deliberately allowing progress >1 and nonfinite zero-length values that JSON serializes null.
- Source selections omit internal verification-group selections, callback functions and artificial native IDs; readFrom/selectTo appear only for buffered readers.
- Source speedometers use a registry-shared 250ms delivered tick in production, injectable tick in tests. Native rates remain available separately and are not substituted. Cumulative wire bytes include late/duplicate payload, separate from accepted owned delivery; uploads use measured native totals.
- Native wire snapshots now carry actual endpoint, choke/interested/seeder state and owned request gauge. Requests/choke state are refreshed coherently under the transport mutex.
- Native connection attempts, queued admission and known endpoints are measured rather than manufactured constants.
- EngineRegistry::prewarmStream uses the existing cache-file selection path only for ready valid engines.

## Verified
- test_engine_statistics: PASS. Covers unknown/removal, native counts versus accepted-owned bytes, source versus native rate, idle decay, wire meters, null/omitted values and shared timer retirement.
- check_engine_statistics.py: 13 source-executed differential cases PASS (missing, 0, 1, 2/zero length, out of range, negative, '0', '1', '01', invalid, null, false, empty string).
- Both existing real localhost wire statistics suites PASS with SERVER1_TEST_FIXED_INTERFACE=1, including new endpoint/connection metadata assertions. Default interface watcher prevents connection establishment in this execution environment; fixed-interface is the existing fixture-only supported option.
- All 22/22 registry regression cases PASS after the discovery owner updated the source-correct duplicate delivery diagnostic assertion. Both real-wire statistics suites PASS. K10-G pause/generation test PASS in a separate rerun after supplying pwsh PATH and writable XDG dirs. No test failure is hidden: earlier raw failing logs are retained as superseded evidence. The endpoint-owner binding regression introduced while counting paused peers was corrected by computing the known-endpoint union without mutating the paused transport admission state.

## Remaining parity gaps (not concealed by projection)
1. Native connection admission counters are not yet equivalent to PWS queue, retained-peer map, active connection and retry lifecycle. In particular, TorrentEngine discoveredAddresses never removes departed peers. PWS removes failed unestablished peers, allows rediscovery, and retries previously established peers on its own timeout sequence. Full lifecycle translation is the next work slice.
2. Native handshake-complete peer count is not evidence for every PWS in-progress connection. The current swarmConnections field shares the measured connected-peer count, not a validated PWS dial/close lifecycle count.
3. Source speeds are fed from transport snapshots during owner polling; exact within-quarter-second event placement at interval boundaries still needs live differential qualification.
4. Current source per-source Date values map monotonic timestamps to wall time at engine creation. Clock discontinuities are not yet differential-qualified.

## Reproduction
Scratch driver: /tmp/engine-stats-driver/CMakeLists.txt; build: /tmp/engine-stats-build.
CMake uses Qt 6.11.1, qualified static libtorrent 2.0.11 and boost_1_83_0, with -j2.
Run /tmp/engine-stats-build/statistics_check.
Run python tools/server_lab/check_engine_statistics.py --oracle <exact locked path above> --candidate /tmp/engine-stats-build/statistics_check.
Run SERVER1_TEST_FIXED_INTERFACE=1 /tmp/server-build-tools/cmake/data/bin/ctest --test-dir /tmp/engine-stats-build/server --output-on-failure -R 'engine-registry|K12-(peer|wire)-statistics-real-wire'.

No commits or pushes performed by this worker.
