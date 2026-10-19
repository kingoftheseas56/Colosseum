# N00 streaming proxy checkpoint

Owned production files (relative to source/):
- native/colosseum_server_v1/include/server1/network/ProxyService.h
- native/colosseum_server_v1/src/network/ProxyService.cpp
- native/colosseum_server_v1/src/network/ProxyRoutes.cpp

Owned test files:
- native/colosseum_server_v1/tests/test_proxy_streaming.cpp
- native/colosseum_server_v1/tests/test_proxy_differential.cjs

Integration: call server1::network::registerProxyRoutes(router) before the generic torrent route. Router owns QNetworkAccessManager. Link source files against existing HTTP contract implementation, Qt6::Core, Qt6::Network, ZLIB::ZLIB and brotlidec. No edits to shared HTTP files, composition, or main CMake were made by this worker.

## Commands

Scratch standalone CMake: /tmp/proxy-build-src/CMakeLists.txt

Normal native fixture:
/tmp/proxy-build/proxy_test

Pinned oracle differential (working directory source/):
../deps/node22/node-v22.16.0-linux-x64/bin/node native/colosseum_server_v1/tests/test_proxy_differential.cjs ../locked-inputs/execution-v2.1/evidence-pack/oracle/stremio-service-v4.21.1-server-bundle/server.js /tmp/proxy-build/proxy_test

The differential harness verifies SHA256 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f and loads unmodified M803 and bundled dependencies through the existing module loader. Fixtures use temporary TLS certificates explicitly trusted solely by the native test process. No system trust changes or certificate-verification bypass is used.

Sanitized native fixture:
ASAN_OPTIONS=detect_leaks=0 /tmp/proxy-build-asan/proxy_test

Sanitized differential: same Node invocation, replacing executable with /tmp/proxy-build-asan/proxy_test and setting ASAN_OPTIONS=detect_leaks=0.

## Evidence

native.log: delayed binary bytes, range/status/header forwarding, response-before-upstream cancellation, reentrant metadata retirement, service destruction during streaming, shutdown of deferred codec stall and published codec stall.

differential.log: 28 PASS lines. Includes exact pinned source HTTP protocol rejection, HTTPS-to-HTTP redirect rejection, connection error text, cookie isolation, request defaults/allowlist/overrides, response overrides, range, 404, HEAD, repeated option ordering, percent path decoding, Unicode/absolute/root-relative/URI playlist references, missing-query null concatenation, explicit-port duplication, 180KB lines spilled to disk, gzip/zlib/raw-deflate/Brotli, redirect cycles, relative redirects, gzip concatenation/truncation/empty input, and six stalled codec cases.

Stalled cases establish identical prefix/header visibility and no completion during a one-second observation, then cancel the clients. They do not claim an infinite hang. The source does not propagate decoder failure to the downstream response. Native preserves this cancelable bounded state and releases it on shutdown.

asan-native.log and asan-differential.log are the same test paths with address/undefined sanitizers. LeakSanitizer cannot run under this executor's ptrace environment; an attempted leak-enabled run reported that limitation, not a code leak.

## Bounds and deliberate deviations

- Upstream Qt read buffer: 64KiB; compressed input and decoded production batches: 16KiB; in-memory outgoing queue: capped around 64KiB, with oversized rewritten output spooled. Logical playlist lines above 64KiB spill to an automatically removed temporary file. Output spools drain before further upstream reads. Disk exhaustion terminates the response rather than fabricating content.
- TLS certificate verification remains enabled. Source disables validation. This is an explicit security deviation, not a claim of exact parity.
- A URL authority exceeding 64KiB within a spilled playlist line is rejected to bound parsing memory. The source has no equivalent bound. This remains an explicit parity exception, including pathological oversized userinfo.
- All statements of parity are scoped to exercised fixtures; malformed URL/header/query grammar and arbitrary platform-specific network error text are not exhaustively qualified.
- No full-main integration was done by this worker. Integration owner must register and run the tests in the authoritative build.

## Non-qualifying broad-suite log

full-suite.log is NOT an acceptance run. It was accidentally run against a shared build without the required SERVER1_TEST_FIXED_INTERFACE and HOME/XDG test profile. It contains known transport listen EPERM/not-run failures. Parent owns correctly profiled full-suite verification; this worker did not rerun it. Do not attribute those failures to proxy changes or present this log as a qualified baseline.
