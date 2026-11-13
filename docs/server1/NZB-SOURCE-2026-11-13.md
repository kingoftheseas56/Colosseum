# NZB source and direct-video HTTP qualification

This slice implements native direct-video NZB playback and delegation into mounted archive routes. It is **not a blanket claim of complete NZB parity**.

## Implemented

- `server1::nzb::parse`: XML, first group, numeric segment ordering, sparse slots, duplicate replacement.
- `parseServer`: reference NNTP URL shape and percent-decoded credentials. Errors omit credentials.
- `decodeYenc`: escaped binary data, dot-stuffing, filename, part headers, CRC mismatch diagnostic (not rejection, matching M1115).
- `ArticleClient`: async plaintext NNTP greeting, AUTHINFO USER/PASS, GROUP, ARTICLE, fragmented replies, bounded buffers, cancellation and timeout. Identical requests share subscribers; each server identity has a bounded authenticated socket pool. Canceling one subscriber preserves siblings; canceling the final subscriber aborts that work. Successful sockets are reused and close after 30 idle seconds. Credentials are part of in-memory pool identity and never logged.
- `NzbSource`: first/last decoded-segment geometry, ordered backbone fallback, <=1 MiB random reads, cross-segment slicing, cancellation and bounded decoded-segment cache. Cache entries expire after seven seconds, with a 32 MiB total limit per source.
- Metadata missing first/last segments fail. More than 2% missing metadata fails; up to 2% missing middle metadata is allowed. Unavailable middle articles after backbone fallback are zero-filled as in M1088.
- `mountNzbRoutes`: POST `/nzb/create/:createKey`, ALL `/nzb/create`, GET/HEAD `/nzb/stream/:key/:fileName`, GET/HEAD `/nzb/stream?key=...`.
- NZB HTTP(S) URL acquisition with redirects, candidate URL racing in groups of five and cancellation of losing candidates, direct-video subject selection and yEnc filename discovery, JSON create response, existing-key redirects, compressed query creation with SHA256 keys, deferred stream-key waiting, MIME/range/DLNA headers, HEAD, cancellation and bounded asynchronous socket streaming.
- Archive precedence, ordered RAR/ZIP/7zip/TAR/GZIP part selection, and internal create dispatch into existing mounted archive routes. Redirects are published only after archive key creation succeeds. A trusted listener-port provider builds loopback source URLs, never the incoming Host header.
- Successful NZB range responses deliberately use HTTP **200**, matching M1088 rather than generic HTTP 206 behavior.

## Evidence

Pinned Stremio 4.21.1 bundle SHA256:
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

`tests/run_nzb_oracle.cjs` executes M1115 and M504 from that bundle: five yEnc cases including all 256 byte values and dot stuffing, and three XML cases covering ordering, sparse slots, duplicate replacement and namespaces. `test_nzb_source.cpp` compares native output and tests controlled NNTP fixtures, first/last geometry, ranges, backbone fallback, missing-piece thresholds/zero filling, cache hits, timeouts, cancellation and destruction during open.

`tests/run_nzb_routes_oracle.cjs` executes the **actual pinned M1088/M1089/M1102/M1115 stack** with synthetic loopback HTTP and NNTP servers. Sixteen HTTP observations are compared to native statuses, response bytes and selected headers: compressed create, missing GET input, malformed POST shape, empty candidate URLs, successful POST create, key redirect, full playback, explicit range, suffix range, first byte, HEAD, 416, existing-key redirect, TAR archive create, TAR key redirect and TAR HEAD. Compressed input is regenerated with the pinned compressor for the native fixture's ephemeral ports; derived key locations are normalized accordingly.

`test_nzb_routes.cpp` additionally covers URL fallback and redirects, yEnc filename discovery, archive failure without a trusted port/mounted handler, deferred key requests and failure release, abandoned URL acquisition, abandoned streaming, real HTTP socket GET/range and the 128 KiB HTTP queue bound. Concurrent HTTP readers share an NNTP article; canceling one preserves the other, while final cancellation closes the active socket. A stalled NZB URL loses to a valid sibling and is aborted. Synthetic TAR, ZIP, GZIP, stored RAR4/RAR5 and stored 7zip archives travel through NNTP, NZB range HTTP, mounted archive parsing and final HTTP playback. RAR/7zip assertions use the source-reported length (physical payload minus one), matching the archive route contract rather than claiming corrected archive semantics. No public NNTP server or user credentials are involved.

Standalone Qt 6.11.1 / GCC 14 / zlib 1.3.1 tests pass. ASan/UBSan runs use `detect_leaks=0`; LeakSanitizer cannot operate in the traced execution environment, so no leak-clean claim is made.

## Remaining gaps and deliberate differences

- End-to-end NZB archive fixtures cover single-part stored RAR4/RAR5/7zip plus ZIP/TAR/GZIP. Split archive selection/order is authored, but split NZB-to-archive payloads and compressed archive payload combinations are not all exercised here.
- No TLS implementation: `nntps` fails explicitly. NZB HTTPS acquisition uses Qt defaults without bypassing certificate validation. No security settings changed.
- Pools are capped at 64 connections per server identity; configured smaller `connections` limits are honored. Work is shared across sources/candidates within one NZB router registry, not process-global registries.
- The exact pinned TAR full GET emitted one extra byte beyond Content-Length under Node 24, producing `HPE_INVALID_CONSTANT`. That malformed-framing case is not claimed as a passing differential; native full GET is separately verified with the declared bounded payload.
- Every selected video is measured individually, instead of copying first-file geometry to middle files.
- Sessions remain registered until router teardown; decoded cache entries expire independently. The 30-second pool idle close timer is implemented, but its wall-clock duration is not separately exercised in the fast fixture suite.
- Duplicate concurrent initialization is not coalesced. Waiting stream requests are weak subscriptions and canceled responses cannot retain producers.
- Malformed XML/segment indices, command CR/LF injection, incomplete yEnc, failed GROUP and inverted/malformed read ranges are rejected rather than reproducing source crashes/permissiveness.
- Per-article and NZB XML buffers are capped at 16 MiB; XML metadata at one million segment slots. Timeout is 20 seconds.
- `Decoded` exposes CRC mismatch; higher source/HTTP layers do not aggregate that diagnostic.

## Integration

Include `tests/cmake/Nzb.cmake` after the core library exists and mount `server1::http::mountNzbRoutes(router, localPortProvider)` in composition. The provider returns the actual bound HTTP server port; omitting it allows direct-video playback but rejects archive initialization. The fragment uses existing archive compressed-query decoding from `ArchiveKeys.cpp`, so the archive module must already be linked. Shared CMake/composition wiring is parent-owned. The approved `RemoteByteSource.cpp` compatibility patch accepts 200 only with exact Content-Range, a known total and matching Content-Length; the existing payload-length validation still applies. Eight regressions cover valid 200 and wrong/missing range, unknown total, wrong/missing length, oversized and truncated responses. Existing 206 behavior is preserved.

For direct byte-source use, call `NzbSource::open(servers,file,completion)`, then `size()`, `name()`, `read(offset,count,completion)` and `cancel(token)` on the owning Qt event loop. Completion is `archive::ReadResult` compatible. Destruction cancels pending callbacks. Source objects are single-open; failed opens require a new source.
