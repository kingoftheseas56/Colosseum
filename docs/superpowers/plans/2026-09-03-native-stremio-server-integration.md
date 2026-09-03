# Native Stremio Server Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver an embedded C++ implementation of the frozen Stremio Server 4.20.17 playback and route surface, with real torrent bytes proven through W06, W07, W01, and the Colosseum application.

**Architecture:** Preserve W01–W10 behavioral ownership and add explicit adapters at every boundary. W06 remains the scheduler authority while a libtorrent-owned-thread transport executes peer-specific blocks; W07 produces route decisions and a FileStream bridge delivers them through W01’s shared HTTP response. A runtime composition owner controls all service lifetimes and replaces the external `stremio-runtime server.js` process in the application.

**Tech Stack:** C++20, Qt 6.11.1 Core/Network/Test, MSVC 19.44, libtorrent 2.0, Boost, OpenSSL, CMake/Ninja, QTcpServer/QSslServer, and the exact Stremio 4.20.17 Wave-0 oracle.

**Spec:** `docs/superpowers/specs/2026-09-03-stremio-server-native-integration-design.md`

## Global Constraints

- Authority is the exact Stremio Server 4.20.17 specimen with SHA-256 `567A397BB11B788571BF1750FD05DD78927F97BEC0C9DDEAA6D9CC1ECCEE3922`.
- W06 remains the sole authority for selection, reservation, critical pieces, hotswap, choke pressure, and request pressure.
- A fixed `Content-Length` response may deliver data incrementally, but its body bytes are raw media bytes and never HTTP chunk frames.
- The HTTP listener binds loopback and retries ports 11470 through 11474; HTTPS uses port 12470 when enabled.
- Existing unrelated dirty work is preserved; no reset, clean, stash, merge, or push is allowed.
- Every production behavior change follows RED test, expected failure, minimal GREEN implementation, regression run, and an explicit commit.
- Unit fakes do not count as production integration; final acceptance requires real bytes and externally observable wire evidence.

---

### Task 1: Repair W01 fixed-length progressive framing

**Files:**
- Modify: `native/colosseum_server/core/HttpRouter.cpp:284-347`
- Modify: `native/colosseum_server/core/HttpConnection.cpp:249-290`
- Test: `docs/research/aqueduct-integration-audit/http-fixed-length-repro/http_fixed_length_repro.cpp`
- Test: `native/colosseum_server/core/tests/tst_colosseum_server_core.cpp`

**Interfaces:**
- `HttpResponse::write()` continues to deliver incremental data.
- `HttpConnection::sendHead`, `sendData`, and `sendEnd` receive a framing flag that is true only for chunked transfer encoding.
- A predeclared `content-length` forces raw writes and suppresses `transfer-encoding: chunked`.

- [ ] **Step 1: Run the existing fixed-length reproducer and record RED.**

Run from a Visual Studio developer prompt:

```text
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%PATH%
C:\b\arc44-lane-c-http-repro\arc44_http_fixed_length_repro.exe
```

Expected: exit `1` because the body is not exactly `alphabeta` under a fixed `Content-Length: 9` response.

- [ ] **Step 2: Add a named W01 regression assertion for fixed-length multi-write responses.**

The test must issue a real loopback request, assert `content-length: 9`, assert no `transfer-encoding: chunked`, and compare the exact body to `alphabeta`. Keep the existing chunked-without-length coverage unchanged.

- [ ] **Step 3: Run the new test before production changes.**

Expected: the test fails at the body/framing assertion, proving it exercises the original defect.

- [ ] **Step 4: Make framing depend on headers, not on incremental delivery.**

Compute `chunked = streaming && !headers.contains("content-length")` at header emission and use that same value for every subsequent data/end operation. When a fixed length exists, pass `false` to `sendData` and `sendEnd` while retaining progressive writes.

- [ ] **Step 5: Run the reproducer, W01 core test, and Lane A smoke build.**

Expected: the reproducer exits `0`, the core test passes, and `ninja -C C:\b\arc44-integration-smoke` succeeds.

- [ ] **Step 6: Commit only the W01 implementation and regression test.**

```text
git add native/colosseum_server/core/HttpRouter.cpp native/colosseum_server/core/HttpConnection.cpp native/colosseum_server/core/tests/tst_colosseum_server_core.cpp docs/research/aqueduct-integration-audit/http-fixed-length-repro/http_fixed_length_repro.cpp
git commit -m "fix(server): preserve raw bytes for fixed-length streams"
```

### Task 2: Close scheduler transport cancellation and hotswap lifecycle

**Files:**
- Modify: `native/colosseum_server/integration/SchedulerTransportBridge.cpp:86-99`
- Modify: `native/colosseum_server/integration/PieceSourceBlockTransport.cpp:27-65`
- Test: `native/colosseum_server/tests/transport_bridge_test.cpp`
- Test: `native/colosseum_server/tests/piece_source_transport_test.cpp`

**Interfaces:**
- `IBlockTransport::cancelBlock()` may cancel without invoking the completion callback.
- Every cancelled scheduler request is terminal through `SchedulerSpine::failRequest()` exactly once.
- A late transport callback is ignored after the request leaves `active_`.

- [ ] **Step 1: Add a RED hotswap test whose fake transport never calls a cancellation callback.**

Create slow and fast peers, trigger W06 hotswap through `SchedulerTransportBridge`, cancel the slow requests, pump again, and assert that the slow peer’s active request count returns to zero and the fast peer can continue requesting.

- [ ] **Step 2: Run only `scheduler_transport_bridge_test` and confirm RED.**

Expected: the stale slow requests remain active because the current hotswap path only marks the transport request cancelled.

- [ ] **Step 3: Make bridge cancellation fail the scheduler request immediately.**

On hotswap, mark the active record cancelled, call `transport_.cancelBlock()`, then call `scheduler_.failRequest(id)`. Preserve the existing late-callback guard.

- [ ] **Step 4: Make piece-source cancellation erase pending waits without retaining callbacks.**

Ensure cancellation is idempotent and that a wait completion cannot read or invoke a completion after its pending entry is removed.

- [ ] **Step 5: Run both transport tests and the scheduler regression harness.**

Expected: all existing tests and the new no-callback cancellation test pass.

- [ ] **Step 6: Commit the lifecycle fix.**

### Task 3: Implement the production libtorrent peer/block transport

**Files:**
- Create: `native/colosseum_server/integration/LibtorrentBlockTransport.h`
- Create: `native/colosseum_server/integration/LibtorrentBlockTransport.cpp`
- Modify: `native/torrent/engine/TorrentEngine.h`
- Modify: `native/torrent/engine/TorrentEngine.cpp`
- Modify: `native/colosseum_server/CMakeLists.txt`
- Test: `native/colosseum_server/tests/libtorrent_block_transport_test.cpp`

**Interfaces:**
- `LibtorrentBlockTransport` implements `IBlockTransport`.
- The constructor receives a `TorrentEngine`/session command adapter and a concrete torrent identity.
- `requestBlock(peerHint, WireBlock, Completion)` dispatches a peer-specific request through the installed libtorrent extension command path.
- `cancelBlock(peerHint, WireBlock)` cancels the matching wire request and guarantees one terminal completion or immediate scheduler failure.

- [ ] **Step 1: Add a RED production transport test using a loopback libtorrent session fixture.**

The fixture must create a deterministic one-piece torrent, connect two local peers, make the source peer own the requested piece, request one 16 KiB block by peer identity, and assert the exact bytes and callback identity. Add cancellation and peer-disconnect cases.

- [ ] **Step 2: Verify the RED failure is at the absent production transport seam.**

Expected: the test cannot construct or dispatch `LibtorrentBlockTransport` because only fake `IBlockTransport` implementations exist.

- [ ] **Step 3: Install a libtorrent extension/factory command seam.**

Use `torrent_handle::add_extension()` to create a plugin in libtorrent context. The plugin owns access to live `torrent`/`peer_connection` objects and accepts queued request/cancel commands. It must use `peer_connection::add_request()` for requests, `cancel_request()` for cancellation, and `send_block_requests()` when the installed API requires an explicit flush.

- [ ] **Step 4: Add a thread-safe command/result queue between the server worker and libtorrent context.**

Each command carries torrent identity, peer identity, piece, offset, length, request id, and completion. The plugin resolves the peer, rejects stale/disconnected peers, and returns bytes only after the corresponding block callback has delivered them.

- [ ] **Step 5: Run the real fixture and inspect exact block bytes, cancellation, and teardown.**

Expected: the production transport test passes without using the fake transport, and no callback remains live after the fixture shuts down.

- [ ] **Step 6: Commit the production transport and its fixture.**

### Task 4: Connect W06 FileStream to real piece storage and verification

**Files:**
- Create: `native/colosseum_server/integration/TorrentPieceSource.h`
- Create: `native/colosseum_server/integration/TorrentPieceSource.cpp`
- Modify: `native/colosseum_server/scheduler/FileStream.h`
- Modify: `native/colosseum_server/scheduler/FileStream.cpp`
- Modify: `native/colosseum_server/torrent/Storage.cpp`
- Test: `native/colosseum_server/tests/torrent_piece_source_test.cpp`

**Interfaces:**
- `TorrentPieceSource` implements `IPieceSource` and `IFilePieceStore` against one real torrent.
- `hasPiece`, `makeUrgent`, `waitForPiece`, `cancelWait`, and `readBlock` share one torrent identity and one cancellation domain.
- FileStream destruction cancels pending piece reads and releases every scheduler lock/selection.

- [ ] **Step 1: Add RED tests for missing-piece wait, exact block read, final short piece, and destroy-before-completion.**
- [ ] **Step 2: Run the tests and confirm the production adapter is absent.**
- [ ] **Step 3: Implement piece notifications from `TorrentEngine::pieceFinished` with queued delivery.**
- [ ] **Step 4: Implement file-span mapping, piece reads, and real storage verification/commit.**
- [ ] **Step 5: Add explicit cancellation to the FileStream/store boundary and reject late reads after destruction.**
- [ ] **Step 6: Run W04/W05/W06 tests plus the new real-source fixture.**
- [ ] **Step 7: Commit the real piece-source path.**

### Task 5: Adapt W07 replies into W01 progressive HTTP

**Files:**
- Create: `native/colosseum_server/integration/TorrentHttpRouteAdapter.h`
- Create: `native/colosseum_server/integration/TorrentHttpRouteAdapter.cpp`
- Modify: `native/colosseum_server/torrent_http/TorrentHttpSurface.h`
- Modify: `native/colosseum_server/torrent_http/TorrentHttpSurface.cpp`
- Test: `native/colosseum_server/tests/torrent_http_wire_integration_test.cpp`

**Interfaces:**
- `mountTorrentRoutes(HttpRouter&, TorrentHttpSurface&, TorrentStreamFactory&)` mounts `/create`, hash create, stats, remove, removeAll, favicon, and media routes.
- The adapter converts W01 requests into `TorrentHttpRequest` and replies into W01 status/headers.
- `TorrentStreamFactory::open(plan, cancellation, callbacks)` returns a cancellable FileStream session.

- [ ] **Step 1: Add RED loopback tests for a real fixed range written in multiple chunks.**
- [ ] **Step 2: Add RED tests for GET, HEAD, invalid range fallback, open-ended range, disconnect cancellation, and lease teardown.**
- [ ] **Step 3: Implement request conversion and response header/status conversion.**
- [ ] **Step 4: Start FileStream only for GET, write chunks through W01, end exactly once, and connect cancellation to `destroy()`.**
- [ ] **Step 5: Verify exact `Content-Length`, raw byte count/hash, `Content-Range`, DLNA headers, and stream counters.**
- [ ] **Step 6: Run W07 harness plus W01/W07 real-byte integration.**
- [ ] **Step 7: Commit the W07/W01 adapter.**

### Task 6: Complete shared W08–W10 route composition

**Files:**
- Modify: `native/colosseum_server/integration/FeatureRouteComposition.cpp`
- Modify: `native/colosseum_server/integration/FeatureRouteComposition.h`
- Modify: `tests/colosseum_server_route_composition/route_composition_tests.cpp`
- Modify: `tests/colosseum_server_route_composition/CMakeLists.txt`
- Create: `native/colosseum_server/integration/AsyncMediaExecutor.h`
- Create: `native/colosseum_server/integration/AsyncMediaExecutor.cpp`
- Test: `tests/colosseum_server_route_composition/route_composition_tests.cpp`

**Interfaces:**
- `mountFeatureRoutes(HttpRouter&, const FeatureRouteDependencies&)` mounts the exact module 564 route order and W10 prefixes.
- `AsyncMediaExecutor` runs blocking media work outside W01 and posts one terminal result to the response owner.
- Route adapters preserve `AppResponse`/`RemoteArchive::Response` headers, status, body, and cancellation.

- [ ] **Step 1: Make the current adapter compile RED by adding it to the test target.**
- [ ] **Step 2: Replace `QVERIFY(true)` with at least ten route cases covering heartbeat, root, settings, network-info, device-info, proxy, YouTube, local addon, archive, FTP/NZB, and one media route handoff.**
- [ ] **Step 3: Implement the route registrations in exact upstream order, returning `false` only for unmatched families.**
- [ ] **Step 4: Add asynchronous media execution and cancellation-aware process termination.**
- [ ] **Step 5: Run the focused route composition target and all W08–W10 harnesses.**
- [ ] **Step 6: Commit the route composition.**

### Task 7: Repair remaining parity and resource hazards

**Files:**
- Modify: `native/colosseum_server/torrent/Storage.cpp`
- Modify: `native/colosseum_server/torrent/tests/W05StorageDiscoveryHarness.cpp`
- Modify: `native/colosseum_server/network_app/NetworkAppServices.cpp`
- Modify: `native/colosseum_server/remote_archive/RemoteArchive.cpp`
- Modify: `native/colosseum_server/remote_archive/RemoteServices.cpp`
- Modify: `native/colosseum_server/network_app/NetworkAppServices.h`
- Test: `native/colosseum_server/torrent/tests/W05StorageDiscoveryHarness.cpp`
- Test: `native/colosseum_server/network_app/tests/tst_proxy.cpp`
- Test: `native/colosseum_server/network_app/tests/tst_network.cpp`
- Test: `native/colosseum_server/remote_archive/tests/remote_archive_tests.cpp`
- Test: new large ranged archive fixtures

- [ ] **Step 1: Add RED source-faithful circular-store eviction tests matching module 847 truthy `new Date(0)` behavior.**
- [ ] **Step 2: Implement the smallest storage-state correction and run W05.**
- [ ] **Step 3: Add a RED production Qt proxy test for plain HTTP and reproduce the frozen 500 response shape.**
- [ ] **Step 4: Add bounded/cancellable RAR/7z ranged fixtures and remove whole-volume materialization from the accepted path.**
- [ ] **Step 5: Add HTTPS listener certificate and shared-router tests on port 12470.**
- [ ] **Step 6: Run the Lane C static audit and require F-01–F-06 signatures to disappear or be explicitly recorded as approved deviations.**
- [ ] **Step 7: Commit parity/resource repairs.**

### Task 8: Create the production runtime composition owner

**Files:**
- Create: `native/colosseum_server/runtime/ColosseumServerRuntime.h`
- Create: `native/colosseum_server/runtime/ColosseumServerRuntime.cpp`
- Create: `native/colosseum_server/runtime/ProductionTorrentBackend.h`
- Create: `native/colosseum_server/runtime/ProductionTorrentBackend.cpp`
- Modify: `native/colosseum_server/CMakeLists.txt`
- Test: `native/colosseum_server/tests/runtime_composition_test.cpp`

**Interfaces:**
- `ColosseumServerRuntime::start()` constructs settings, cache, services, concrete torrent backend, route adapters, and listeners.
- `ColosseumServerRuntime::stop()` cancels requests, closes streams, removes engines, and stops HTTP/HTTPS listeners deterministically.
- `ProductionTorrentBackend` implements both EngineFS and W07 backend contracts using the same torrent identity map.

- [ ] **Step 1: Add RED composition tests that require `/heartbeat`, `/settings`, `/stats.json`, `/create`, and one media route on a real started `ColosseumServer`.**
- [ ] **Step 2: Implement owned lifetimes and dependency construction.**
- [ ] **Step 3: Mount W07 and W08–W10 routes into the shared router.**
- [ ] **Step 4: Add listener retry, bound URL publication, and shutdown cancellation.**
- [ ] **Step 5: Run the composition test and all previously passing packet tests.**
- [ ] **Step 6: Commit the runtime composition.**

### Task 9: Cut the embedded server into the application

**Files:**
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp:1242-1254`
- Modify: `native/player/streamserver.h`
- Modify: `native/player/streamserver.cpp`
- Test: application startup/playback integration target

- [ ] **Step 1: Add a RED application test proving the current path launches `stremio-runtime` and does not expose the embedded runtime.**
- [ ] **Step 2: Link `colosseum_server_native` and its required Qt/libtorrent/OpenSSL dependencies into the real application target.**
- [ ] **Step 3: Replace child-process startup with `ColosseumServerRuntime` ownership while preserving `Stream.play`, `prefetch`, `streamReady`, `fetchReady`, `streamError`, `watchStats`, and `unwatchStats`.**
- [ ] **Step 4: Remove external-runtime launch from the accepted application path and retain no executable override.**
- [ ] **Step 5: Build the application and run a real local playback fixture.**
- [ ] **Step 6: Commit the application cutover.**

### Task 10: Final adversarial verification and release gate

**Files:**
- Create: `tests/arc44_real_torrent_http_e2e.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/research/aqueduct-integration-audit/AUDIT.md` with current evidence only

- [ ] **Step 1: Run exact oracle hash and Wave-0 source-port completeness checks.**
- [ ] **Step 2: Run real torrent bytes through create, metadata, W06, libtorrent transport, storage verification, FileStream, W07, W01, and consumer.**
- [ ] **Step 3: Verify bounded/open-ended/invalid Range, HEAD, cancellation, hotswap, peer failure, late callback, and shutdown.**
- [ ] **Step 4: Verify all 108 Wave-0 route/internal rows through the shared router or document an explicit approved deviation for each excluded behavior.**
- [ ] **Step 5: Run W37/W38 lifecycle, W39 route parity, and W40 playback regression targets.**
- [ ] **Step 6: Run `scripts/validate-directives.ps1` when present.**
- [ ] **Step 7: Run `git diff --check`, inspect the changed-file set, and record compiled/tested/runtime-verified/committed states separately.**
- [ ] **Step 8: Commit only after all required evidence is green; do not merge or push without explicit direction.**
