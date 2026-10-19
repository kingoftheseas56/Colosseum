# K09 controlled network acquisition milestone

Authoritative oracle: execution-v2.1/evidence-pack/oracle/stremio-service-v4.21.1-server-bundle/server.js, SHA256 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.

Implemented:
- TrackerNetwork: one client per source run; HTTP(S)/UDP announce using oracle fixed peer ID, port 6881, started/update semantics, binary hash encoding, numwant 50, unknown UDP left; compact IPv4/IPv6 and dictionary peers; response intervals/tracker ID retention; source pause does not stop tracker clients; close cancels all clients.
- DhtLookup: persistent service and lookup IDs; detailed source mapping and residual audit supplied by DHT worker separately.
- DiscoveryNetwork + QtDiscoveryNetwork: explicit owned network executor with per-source queues, persistent DHT construction/bootstrap before delayed lookup, individual delayed abort handles, full close.
- PeerSearch: optional executor and explicit swarm hash, constructor-relative interval phase retained while paused, all source peer observations reach swarm admission while only numFoundUniq deduplicates.
- Registry config factory: app-lane creation, carried through Wiring into engine and actual source hash, default null for policy-only contexts.

Test-first evidence:
- Stub tracker failed actual HTTP acquisition, then green.
- UDP fixture exposed invalid inherited HTTP proxy; explicit direct UDP socket proxy type fixed it.
- Tracker ID retention and interval=0/min interval fallback failed then passed.
- Destroy-on-peer produced ASan heap-use-after-free, then callback copies/QPointer guards fixed it.
- Source network dispatch, DHT construction before delay, construction-relative clock and paused interval phase failed before fixes.
- Registry factory regression R4 failed before plumbing, then passed.
- Duplicate trace executes actual SHA-pinned M612 function and matches native 4-add sequence and stats [[2,1],[2,1]].

Isolated checks:
- cmake --build /tmp/tracker-build; ctest --test-dir /tmp/tracker-build --output-on-failure -j2: 9/9 pass, /tmp/tracker-ctest.log.
- ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/tracker-asan --output-on-failure -j2: 9/9 pass, /tmp/tracker-asan-ctest.log. LeakSanitizer unavailable under runner ptrace; no leak-pass claim. Latest timer/factory changes receive separate rebuild below.
- /tmp/discovery-registry-build/p03-tests/server1_k11_engine_registry_test R4: PASS, verifies app lane and source hash.
- Driver /tmp/tracker-driver/CMakeLists.txt contains all isolated target source lists; Qt 6.11.1 Network/Core, no moc needed.

Do not use /tmp/discovery-surrounding-ctest.log as stable suite evidence: that run incorrectly overlapped integration's shared build and lacked its required test environment. It showed 17 failures/not-run and is retained only for transparency; integration owns stable full-suite result.

No public swarm success, Windows qualification or full-server parity claimed. Decoder bounds (1MiB HTTP and bounded bencode depth/nodes), HTTP TLS downgrade rejection and currently stronger tracker UDP source validation are safety/robustness constraints, not evidence of byte-for-byte malformed-input behavior. Production composition and final cross-module qualification belong to integration owner. No commits/pushes performed by this worker.

## Final isolated validation
Both normal and ASan+UBSan complete discovery suites are now 9/9 PASS, including exact pinned DHT differential. Commands/log locations above unchanged. ASAN_OPTIONS=detect_leaks=0 remains necessary. The 22-field DHT differential output is also /tmp/dht-source-differential.log.

Pinned source mapping:
- M613 lines58314–58326: persistent wrapper, concurrency10, 1500ms start/abort.
- M618 lines58564–58565: backgroundConcurrency2, k20.
- M619 line58718: timeout2000; lines58755–58761 timeout/4 tick; lines58795–58796 TTL4.
- M290 lines26274–26277: tables1000/maxAge300000, values1000, peers10000; lines26291–26294 secret rotation300000.
- M290 lines26726–26729: current/previous secret tokens; lines26747–26755 sample cap100.
- M290 lines26373–26382: ping retry outer-error capture quirk; lines26524–26554 cached callback ignoring lookup abort; lines26647–26659 unsupported mutable default verifier.
- M292 lines26762–26784: compact IPv4/IPv6 parser; M183 line18803 IPv6 toString compresses first zero run, including single group. Tracker fixture now proves exact IPv4-mapped and noncanonical first-zero behavior, not Qt's RFC5952 rendering.

## Frozen final files and commands

New production headers: include/server1/discovery/{DiscoveryNetwork,TrackerNetwork,DhtLookup,QtDiscoveryNetwork}.h.
New production sources: src/discovery/{TrackerNetwork,DhtLookup,QtDiscoveryNetwork}.cpp.
All paths above relative to native/colosseum_server_v1.
Policy changes PeerSearch.h/.cpp and registry/engine factory wiring were separately checkpointed by parent at 98be19d.
Tests: test_tracker_network.cpp, test_dht_network.cpp, test_dht_service.cpp, test_discovery_network.cpp, test_peer_search.cpp, compare_peer_search_duplicates.cjs, run_dht_differential.py, dht_oracle_probe.cjs. Registry R4 app-lane factory check is in test_engine_registry.cpp.

Main build integration: compile the three new source files into server1_k09_peer_search, link Qt6::Network PUBLIC, add the four new C++ test executables linked to server1_k09_peer_search. No moc is required. Production composition sets EngineRegistryConfig.discoveryNetworkFactory to construct one QtDiscoveryNetwork on the app lane. Integration owner retains that work.

From native/colosseum_server_v1/tests:
- python run_dht_differential.py --oracle <pinned-server.js> --candidate <build>/test_dht_service
- node compare_peer_search_duplicates.cjs <pinned-server.js> <build>/test_peer_search
Both verify the exact oracle SHA256 before evaluating extracted source.

Frozen final normal and ASan/UBSan suites: 9/9 each. DHT exact-source differential: 22/22 observed fields, including access-LRU retention/eviction of 1,000 immutable values and 10,000 peers. Final file hashes are in SHA256SUMS.txt. Final logs are native-ctest.log, asan-ubsan-ctest.log, dht-differential.log and peer-search-differential.log alongside this receipt.

## Explicit remaining qualification limits

- Bounded strict bencode differs on malformed/noncanonical inputs, duplicate keys, numeric overflow and non-20-byte DHT query targets.
- Qt DNS/event-loop ordering and overlapping lookup/probe scheduling are not exhaustively compared to Node socket slot/listener ordering, including oversized custom bootstrap sets.
- Public library APIs not exercised by the actual wrapper are not exposed: outbound announce/get/put, optional verifier/custom hash, explicit listen/stale-bucket maintenance. Default inbound mutable rejection is source-matched.
- Token-table maxAge/max1000 are configured but exhaustive token-table age/capacity is not independently exercised; peer/value capacities are.
- HTTP decoder bounds and TLS downgrade rejection, and tracker UDP endpoint correlation, remain intentional robustness constraints rather than malformed-input parity claims.
- No Windows build, public-DHT soak, or public tracker swarm performance claim. IPv4 DHT matches the pinned UDP4 default.
- Full server composition, native transfer from tracker-discovered peer without manual injection, Windows/platform and release gates remain with parent integration.
