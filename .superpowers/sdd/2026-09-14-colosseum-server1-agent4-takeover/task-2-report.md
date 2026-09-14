# Task 2 report — Server 1.0 Slice 2 torrent spine

Producer: [Sol (Codex), INT-W3 producer]
Branch: sol/server1-w3-spine-20260914
Frozen base: 4cddbf88c979533b39232e68b56e87d46eed4d84
Source candidate: 2ff8f20617927d151098c11b303477f6ec735628
Evidence checkpoint: 03344148469835f355943ea39d2c33e55f7b043c
Report date: 2026-09-15

## Verdict

Slice 2 is an integration candidate, not an accepted barrier. K02-A, K05-A,
K06-A, K03-A, K07-A, K09-A, K09-B, K04-A, and P08-T were implemented in the
required causal order. Fresh aggregate verification at source candidate
2ff8f206 built 65/65 steps and passed 38/38 tests. A three-run repeat passed
114/114 executions. All six P08-T compile-negative mutations were rejected.

B-W3A, B-W3B, B-W3C, and P08-T-CONTRACT remain test-reported candidates pending
independent Agent 4 review. This producer does not self-accept them.

## Authority and frozen inputs

The implementation used the read-only Preflight plan at:

C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\44-native-stream-server\plans\server1-v2.1-parallel\PARALLEL-WORK-ITEMS.json

The frozen source pack was:

C:\Users\Suprabha\Downloads\Colosseum-Server-1.0-Execution-Plan-Pack-v2.1\Colosseum-Server-1.0-Execution-Plan-v2.1

All nine accepted package hashes matched the control addendum before mutation.
The frozen server.js oracle was 6,676,503 bytes with SHA-256
405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.
The P08-A capability matrix pins libtorrent 2.0.11.0 revision 6e1587799.

Source modules used:

- M851 lines 74856-74884,
  2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041.
- M814 lines 72439-72764,
  05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e.
- M664 lines 62053-62065,
  e770c8eec9cac4a2509806bc6c26288c7188c5fcbcaeee1327199d5eddb46458.
- M818 lines 72812-72975,
  eab958a614c693923c1114bae213dadd73d977a87fd640874402418bbcfa493e.
- M822 lines 73188-73361,
  0ff8f245d5904808629f49a79bb25edb3e2c887ab82dee63a13a9c5ecf039fc5.
- M843 lines 74473-74529,
  520011da2b4a68a3c71f967416cda41badb047ade587672ef031b1edce9efa30.
- M830 lines 73605-73630,
  fe603735d0586d911a767f0cc8996681c3951b1f4a38622eeef630ca55b22a29.
- M844 lines 74529-74645,
  71a56c9f02ee5ed434085228d1e585949cd79c2f7081f270ffe783b4cd27a730.
- M845 lines 74645-74730,
  6aa06288e0ad7a91abc168127b87c05eb071a13a2ec72e2219b500437ecf482a.
- M846 lines 74730-74772,
  fdff76e6c1b5a55a544a9de1d52f7882a63aef8a8d64a3425cd9f0218391c727.
- M172 lines 18110-18500,
  bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486.
- M290 lines 26267-26757,
  3900d32bb67bb1795561f42eb2a222de5e01e7136f1855aed31319819d02ce65.
- M564 lines 46578-46979,
  d02004f3597ab40da72799e55f852428db0946210ee93dba9533800d1b0b0569.
- M612 lines 58255-58309,
  dac17bb43e2d6f2615ba81713bb1f504dfe26bb85f99ad9d0f0a9e0e0ca9d69b.
- M613 lines 58309-58330,
  166a8bfc8dcda3b2e7d3311acd179d388f293a0a95f9fdf414dbbc4442dc3843.
- M624 lines 59001-59019,
  3a1e1620ad371c099676605abf74fb7e4e183ed1ae4df5143655aeefaa97056f.
- M626 lines 59022-59107,
  82e99353539ff19db4729f0dbf5022f0cbea18ea20c1927eb30c2327784a3363.
- M804 lines 71090-71092,
  77e375644fdae102dec312e7d8b99918194b25f5bd7a447d936e938f4ca133bd.

P08-T came from the accepted repository control graph addendum, worker digest
82efdcc83eafb64edae458ed20ddb2f941e380ad63bed6c5ab92290b9fd9cef2.

## Causal commit chain

1. b0a27d5d769cab9b2b46d4f6bf71cfc4ef18112f — K02-A piece buffer policy.
2. d29c8b0b95d1db1f021221683b8ca3e66055f6c0 — K05-A swarm metadata policy.
3. 99122be413287e9ee5f1646423331e69590af4fd — K06-A persistent piece store.
4. fd1216a0faecc14554753ffb0d920de443c91336 — B-W3A integration candidate.
5. 13f53418c96b4922caffc3ea56b1b6e52e09aa40 — B-W3A candidate evidence.
6. ef0934e28e6461499333e098093131a8cf7edb62 — K03-A scheduler selections.
7. fccffda26a74bfee811f28babc30e72602bd00ac — K07-A circular piece store.
8. d41cac8acfb0ab76318d93e3a991dbaac5346157 — K09-A peer discovery.
9. 573c1211da0437607c92c7a108cf30c4943df6f9 — K09-B swarm-cap convergence.
10. de4a76f3824c991ba5497d8a2644ae3d40fc4bcc — B-W3B integration candidate.
11. fc1c49943227614594edad8c3789c9e1aa9ed937 — B-W3B candidate evidence.
12. fb0e1e4cbfca3bb2255e44bca86cdbf6e267bba6 — K04-A scheduler actions.
13. cefb6fe21524c21ccf526d5f2e70904b1ee793aa — B-W3C integration candidate.
14. 0de98ef49f5a546a0e4148d02d75b638eb49db53 — B-W3C candidate evidence.
15. 36a3fa3651022c4d89f9d74b3154983b80e697bc — P08-T contract freeze.
16. 2ff8f20617927d151098c11b303477f6ec735628 — P08-T aggregate integration.
17. 03344148469835f355943ea39d2c33e55f7b043c — P08-T candidate receipt/state.

This ordering proves K06 followed K02, K09-B followed K09-A, K04 followed
B-W3B, and P08-T followed B-W3C.

## Exact implementation and integration files

K02-A:

- native/colosseum_server_v1/include/server1/policy/PieceBuffer.h
- native/colosseum_server_v1/src/policy/PieceBuffer.cpp
- native/colosseum_server_v1/tests/test_piece_buffer.cpp
- native/colosseum_server_v1/cmake/packets/K02.cmake
- native/colosseum_server_v1/tests/cmake/K02.cmake

K05-A:

- native/colosseum_server_v1/src/policy/SwarmPolicy.cpp
- native/colosseum_server_v1/src/policy/MetadataExchange.cpp
- native/colosseum_server_v1/tests/test_swarm_metadata.cpp
- native/colosseum_server_v1/cmake/packets/K05.cmake
- native/colosseum_server_v1/tests/cmake/K05.cmake

K06-A:

- native/colosseum_server_v1/include/server1/policy/PieceStore.h
- native/colosseum_server_v1/src/storage/PersistentPieceStore.cpp
- native/colosseum_server_v1/src/storage/VerificationBitmap.cpp
- native/colosseum_server_v1/tests/test_persistent_store.cpp
- native/colosseum_server_v1/cmake/packets/K06.cmake
- native/colosseum_server_v1/tests/cmake/K06.cmake

K03-A:

- native/colosseum_server_v1/include/server1/policy/Scheduler.h
- native/colosseum_server_v1/src/policy/SchedulerSelections.cpp
- native/colosseum_server_v1/tests/test_scheduler_selections.cpp
- native/colosseum_server_v1/cmake/packets/K03.cmake
- native/colosseum_server_v1/tests/cmake/K03.cmake

K07-A:

- native/colosseum_server_v1/src/storage/CircularPieceStore.cpp
- native/colosseum_server_v1/tests/test_circular_store.cpp
- native/colosseum_server_v1/cmake/packets/K07.cmake
- native/colosseum_server_v1/tests/cmake/K07.cmake

K09-A:

- native/colosseum_server_v1/src/discovery/PeerSearch.cpp
- native/colosseum_server_v1/src/discovery/TrackerSource.cpp
- native/colosseum_server_v1/src/discovery/DhtSource.cpp

K09-B:

- native/colosseum_server_v1/src/policy/SwarmCaps.cpp
- native/colosseum_server_v1/tests/test_peer_search.cpp
- native/colosseum_server_v1/cmake/packets/K09.cmake
- native/colosseum_server_v1/tests/cmake/K09.cmake

K04-A:

- native/colosseum_server_v1/src/policy/SchedulerRequests.cpp
- native/colosseum_server_v1/src/policy/SchedulerHotswap.cpp
- native/colosseum_server_v1/tests/test_scheduler_requests.cpp
- native/colosseum_server_v1/cmake/packets/K04.cmake
- native/colosseum_server_v1/tests/cmake/K04.cmake

P08-T:

- native/colosseum_server_v1/include/server1/ports/TorrentTransport.h
- native/colosseum_server_v1/tests/test_torrent_transport_contract.cpp

INT-W3-only aggregate files:

- native/colosseum_server_v1/CMakeLists.txt
- native/colosseum_server_v1/tests/CMakeLists.txt
- docs/server1/STATE.json

The complete non-evidence ownership audit found 42 changed paths, 42 paths in
the union of frozen worker ownership, and zero unexpected paths.

## Exact evidence files

Each packet directory below contains PACKET-RECEIPT.json, SOURCE-TRACE.md,
TDD-RED.txt, TDD-GREEN.txt, and WIRING-REQUEST.json:

- artifacts/server1/K02/K02-A/
- artifacts/server1/K05/K05-A/
- artifacts/server1/K06/K06-A/
- artifacts/server1/K03/K03-A/
- artifacts/server1/K07/K07-A/
- artifacts/server1/K09/K09-A/
- artifacts/server1/K09/K09-B/
- artifacts/server1/K04/K04-A/
- artifacts/server1/P08/P08-T/

K09-A additionally contains TDD-DRIVER.cpp. Integration receipts are:

- artifacts/server1/INT-W3/B-W3A.json
- artifacts/server1/INT-W3/B-W3B.json
- artifacts/server1/INT-W3/B-W3C.json
- artifacts/server1/INT-W3/P08-T-CONTRACT.json

This report is:

- .superpowers/sdd/2026-09-14-colosseum-server1-agent4-takeover/task-2-report.md

Generated build trees and compiler scratch products remain ignored and are not
committed evidence.

## TDD and per-case evidence

K02-A RED: MSVC exit 2 because PieceBuffer.h/PieceBuffer.cpp did not exist.
GREEN: compile exit 0; K02-01, K02-02, and K02-03 each exited 0. Assertions
cover 16 KiB plus one-byte tail geometry, exhausted reservation, LIFO cancel
reuse, duplicate delivery, terminal flush, post-flush rejection, and stale
generation rejection.

K05-A RED: MSVC exit 2 because MetadataExchange.cpp and SwarmPolicy.cpp did
not exist. GREEN: compile exit 0; K05-01, K05-02, and K05-03 each exited 0;
both standalone production objects compiled. Assertions cover negative and
out-of-order metadata pieces, hash mismatch, the 4 MiB cap, choke/rechoke
timing, seeded/nonseeded ordering, and per-infohash isolation.

K06-A RED: MSVC exit 2 because PieceStore.h did not exist. GREEN: compile
exit 0; K06-01, K06-02, and K06-03 each exited 0. Assertions cover cross-file
writes and tails, absent/replaced destinations, assembled versus verified
state, corrupt-group reset, serialized close/write errors, stale bitmap
invalidation, and source-compatible N+1 callback multiplicity.

K03-A RED: MSVC exit 1 with C1083 because Scheduler.h did not exist. GREEN:
compile exit 0; K03-01, K03-02, and K03-03 each exited 0. Assertions cover
stable equal-priority ordering, true/false/numeric priority, overlapping
windows, reverse fresh-peer selection, normal forward selection, exact
completion notifications, and transition-only idle collection.

K07-A RED: MSVC exit 1 with C1083 because CircularPieceStore.cpp did not
exist. GREEN: compile exit 0; K07-01, K07-02, and K07-03 each exited 0; the
standalone packet source compiled. Assertions cover source-visible full
capacity, selected/locked/uncommitted exclusions, exact oldest victim,
memory/fs modes, stale/canceled spill completion, close, tails, and zero
capacity.

K09-A RED: MSVC exit 1 because TrackerSource.cpp did not exist. The RED driver
initially used five parent segments; it was corrected to four before GREEN.
That was a test-harness path correction and did not alter production behavior.
GREEN: focused driver compile and run exited 0; PeerSearch.cpp,
TrackerSource.cpp, and DhtSource.cpp each compiled standalone. Assertions cover
strict min/max hysteresis, paused search, coordinator-wide uniqueness, the
1500 ms DHT delay, canceled waits, and disabled internal discovery.

K09-B RED: MSVC exit 1 with C1083 because SwarmCaps.cpp did not exist. GREEN:
compile exit 0; K09-01, K09-02, and K09-03 each exited 0; SwarmCaps.cpp
compiled standalone. Assertions cover announce override, no-source and failed
tracker paths, DHT timeout/teardown, strict speed/buffer/min-peer equality,
zero windows, and no duplicate native discovery.

K04-A RED: MSVC exit 1 because SchedulerRequests.cpp did not exist. The first
implementation compile exposed missing standard cmath declarations; adding the
required include produced GREEN without changing the planned algorithm.
GREEN: compile exit 0; K04-01, K04-02, and K04-03 each exited 0; both
production objects compiled standalone. Assertions cover request budgets for
0, 1, 2, 15, 30, and 100 unchoked peers; exact 16/48 KiB/s and two-times
hotswap boundaries; canceled reservations; one terminal outcome; corrupt
groups; pulse threshold; and 500 ms debounce.

P08-T RED: MSVC exit 1 with C1083 because TorrentTransport.h did not exist.
GREEN: compile and run exited 0 with P08-T PASS. Six compile-negative builds
each exited 1 for the intended missing member: request ownership, generation,
exact block/tail length, cancellation, typed observation, and honest
statistics.

## Aggregate configure, build, and CTest

Environment:

- Generator: Ninja.
- Compiler: MSVC 19.44.35227.0 x64.
- Language standard: C++17.
- Runtime: MultiThreadedDLL.
- Qt: 6.11.1 msvc2022_64.
- CMake:
  C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
- CTest:
  C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe

Fresh source-candidate commands:

    cmake -S native/colosseum_server_v1 -B artifacts/server1/INT-W3/P08-T-2ff8f206 -G Ninja -DQt6_DIR=C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6
    cmake --build artifacts/server1/INT-W3/P08-T-2ff8f206
    ctest --test-dir artifacts/server1/INT-W3/P08-T-2ff8f206 --output-on-failure

Results at 2ff8f206:

- Configure: exit 0.
- Build: exit 0, 65/65 steps.
- CTest: exit 0, 38/38 tests, 0 failed.
- Retained W0-W2 inventory: 16/16.
- B-W3A inventory: 25/25.
- B-W3B inventory: 34/34.
- B-W3C inventory: 37/37.
- P08-T aggregate compile consumer: 1/1.

Repeat command:

    ctest --test-dir artifacts/server1/INT-W3/P08-T-2ff8f206 --output-on-failure --repeat until-fail:3

Repeat result: exit 0, 38 tests repeated three times, 114/114 executions,
0 failed, 13.17 seconds.

Barrier-specific fresh results:

- B-W3A at fd1216a0: build 43/43; CTest 25/25; W0-W2 16/16.
- B-W3B at de4a76f3: build 58/58; CTest 34/34; predecessor 25/25.
- B-W3C at cefb6fe2: build 63/63; CTest 37/37; predecessor 34/34.
- P08-T at 2ff8f206: build 65/65; CTest 38/38; predecessor 37/37;
  repeat 114/114; compile-negative controls 6/6 rejected.

B-W3A diagnostic history is preserved: the first aggregate build completed
43/43, but K05/K06 processes exited 0xc0000135. Generated CTest metadata showed
that those six tests lacked the Qt runtime PATH property already used by H00
and M00. Adding the same target-derived Qt6::Core PATH property made those
cases pass. A new committed-tree build then passed 25/25.

One attempted repeat command used the stale path
C:\Program Files\CMake\bin\ctest.exe and failed before test execution because
that executable does not exist. CMakeCache.txt identified the active Visual
Studio-bundled CTest path above; the rerun then passed 114/114. This is an
environment-command correction, not a product or test failure.

## Differential and source-parity evidence

Every packet source trace pins the oracle SHA and exact authoritative module
range/hash. The focused tests assert source-derived inputs, thresholds,
ordering, state transitions, callback multiplicity, and failure behavior. The
candidate outputs for all named case IDs match those frozen expected traces.
K09 convergence additionally verifies that internal engine DHT/tracker
discovery remains disabled.

This slice did not execute the JavaScript oracle as a second live runtime.
Therefore the differential claim is limited to frozen byte/hash provenance and
source-trace-to-native-test comparison. It is not a live source-versus-native
network differential and remains subject to Agent 4 review.

P08-T is grounded in accepted P08-A real-wire feasibility evidence, but this
slice tests only the public compile contract. It does not implement or run the
future libtorrent adapter.

## Interface drift and collision review

- Scheduler.h was authored only by K03-A at ef0934e2. K04 consumes the
  predeclared SchedulerActionContract; no later commit changes Scheduler.h.
- TorrentTransport.h was authored only by P08-T at 36a3fa36. The aggregate
  integration commit changes only tests/CMakeLists.txt and STATE.json.
- TorrentTransport.h exposes no libtorrent type. It carries requestId,
  selectionId, generation, peer, piece, offset, and exact length through
  request/cancel/block/failure paths; typed interest/choke actions,
  observations, statistics, and close are present.
- Aggregate CMake, aggregate test CMake, and STATE changes appear only in the
  INT-W3 integration/evidence commits fd1216a0, de4a76f3, cefb6fe2,
  2ff8f206, and 03344148.
- git diff --check from 4cddbf88 through 03344148 passed.
- Changed non-evidence paths matched frozen ownership 42/42 with zero
  unexpected paths.
- Search for K08, K10, K11, K12, H01, H02, Runtime.cpp,
  ServerComposition.cpp, RootRoutes.cpp, streamserver, TorrentEngine, and
  workflow changes found zero paths.

## Scope and risks

Implemented scope is limited to K02, K03, K04, K05, K06, K07, K09, P08-T,
their packet-local CMake/test surfaces, and INT-W3 aggregate/state evidence.

Not implemented: K08, K10, K11, K12, H01, H02, any application integration,
packaging, full composition, runtime playback, real-device proof, or release
readiness.

Review risks:

- K07 and K09 have no frozen public-header ownership. Their focused tests
  include the owned implementation cpp files to exercise internal packet
  contracts. Production objects also compile standalone, but Agent 4 should
  review this seam choice before acceptance.
- K09-A RED evidence includes the disclosed driver-relative-path correction.
  The corrected driver still failed for the intended absent production file
  before implementation and passed afterward.
- The source-parity evidence is static trace-to-test comparison, not a
  dual-runtime differential.
- P08-T is a contract freeze only. No claim is made that K10 or any real
  libtorrent adapter exists.
- No barrier or gate in this report is self-accepted.

## Candidate receipts and review boundary

- B-W3A candidate: fd1216a0faecc14554753ffb0d920de443c91336.
- B-W3B candidate: de4a76f3824c991ba5497d8a2644ae3d40fc4bcc.
- B-W3C candidate: cefb6fe21524c21ccf526d5f2e70904b1ee793aa.
- P08-T contract: 36a3fa3651022c4d89f9d74b3154983b80e697bc.
- P08-T aggregate candidate: 2ff8f20617927d151098c11b303477f6ec735628.
- Candidate receipt/state checkpoint:
  03344148469835f355943ea39d2c33e55f7b043c.

Independent reviewer: Agent 4.
Independent verdict: pending.

The final branch push and remote-ref comparison are performed after this
report is committed. The immutable final HEAD and remote equality proof are
returned in the producer handoff because a committed report cannot contain its
own future commit hash.
