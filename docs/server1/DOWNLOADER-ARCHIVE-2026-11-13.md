# Downloader and archive integration, 2026-11-13

This is component and localhost integration evidence, not a full Stremio server
parity or release claim. No production operation or dependency security change
is part of this work.

## Implemented and integrated

- N07 downloader routes and persistent manager are registered in the native build.
  Restore remains unconditional; exposing the keyed HTTP routes is conditional on
  a nonempty downloader API key. The existing lifecycle suite found an actual
  same-origin concurrency failure: one Qt network manager queued downloads after
  six connections. Per-download managers now preserve independent source starts.
  Reply buffering is bounded to 64 KiB.
- Archive key creation, compressed-query decoding, ordering, source descriptors,
  and legacy multipart-range arithmetic are registered with tests.
- ZIP creation/stream routes support deferred key availability, source-compatible
  selection/cache behavior, stored byte ranges, compressed full-body reads,
  HEAD status differences, MIME/DLNA/CORS headers, native DEFLATE, and cancelled
  response cleanup. ZIP64 metadata and disk-relative split-ZIP offsets are native.
- TAR and GZIP/TGZ routes share the bounded source, selection, deferred-response,
  and asynchronous HTTP machinery. TAR uses byte ranges; gzip paths support only
  the source's accepted whole-stream range forms. Plain gzip has unknown decoded
  length; TGZ selects and streams its inner file. Separate native TAR/GZIP parser
  tests characterize their source quirks.
- Each network source pull is at most 1 MiB. Decompression input/output and HTTP
  producer buffers are at most 64 KiB per pull. Archive contents are not extracted
  to disk or eagerly cached in full. HEAD parses metadata without streaming the
  selected media body. Gzip metadata inspection necessarily decompresses earlier
  bytes when selecting an inner TAR member.

## Evidence

Exact JavaScript input: SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`, 6,676,503 bytes.
Oracle runners check this identity and execute extracted source modules; the
candidate remains native C++ and does not embed a JavaScript runtime.

Independently compiled with Qt 6.11.1 and native zlib, then executed:

- Downloader lifecycle: actual localhost transfer, eight concurrent held
  same-origin requests, pause/resume, constructor restore, duplicate filenames,
  file ranges, periodic persistence, errors, removal and teardown.
- Downloader exact-source differential: 19 route cases plus three real localhost
  transfer, duplicate-name and HTTP-error cases.
- Archive policy differential: 150 descriptor/range cases, 11,115 multipart
  mappings, 15 key-normalization cases and five compressed Unicode query cases.
- Remote HTTP byte source: strict ranges, metadata probe, redirects, shared reads,
  cancellation and destruction during callbacks.
- ZIP exact-source directory/range differential: stored, deflated, long-name,
  ZIP64, malformed and truncated archives plus 36 method/range combinations.
- Bounded ZIP pulls: complete stored/deflated output; duplicate/invalid pulls,
  cancellation, teardown, encrypted/invalid/truncated entries and overflow guards.
- HTTP archive integration: key creation/wait, metadata-only HEAD, full and suffix
  ranges, split ZIP disk offsets, DEFLATE, TAR, TGZ and plain GZIP output, seek
  rejection, malformed archive errors and cancelled responses.

ASan and UBSan instrumented builds also passed the downloader lifecycle, ZIP
lifecycle, and archive HTTP integration tests. LeakSanitizer could not run under
this environment's process tracer; those checks were rerun with leak detection
disabled. This is not a leak-clean claim.

Fixtures are generated in disposable test directories. The test registration is
`native/colosseum_server_v1/tests/cmake/N07A02.cmake`; no external archive sample
or machine-local path is required. Exact-source tests additionally require the
pinned oracle. Build-wide Release evidence is tracked by the integration run,
separately from these isolated component checks.

## Explicit boundaries and safety deviations

- No RAR, 7z, FTP, NNTP or NZB endpoint/backend is implemented by this slice.
  The key/descriptor primitives for those archive families are not the codecs.
- Actual remote reads use physical concatenated byte coordinates. M277's
  sum(length - 1) and boundary-padding quirks are reproduced and independently
  tested in the policy helper, but are not allowed to truncate/corrupt archive
  metadata or fabricate a successful seek in the transport.
- Stored ZIP/TAR HTTP output is capped to its advertised Content-Length. Source
  overread quirks remain visible in the range planner/parser tests, but extra bytes
  are not emitted onto a keep-alive connection. Negative/inverted HTTP lengths
  fail rather than generating malformed framing.
- Remote sources must honor bounded HTTP Range requests with matching 206 and
  Content-Range. Oversized bodies, ignored ranges and unknown required source
  lengths fail explicitly. TLS verification is not disabled. Non-HTTP archive
  inputs are rejected rather than falling back to local files or other protocols.
- Malformed/unsupported archives return errors; encrypted ZIP entry streaming
  reproduces the source's unsupported-password error, not a working decryptor.
  Source limits such as its short ZIP end-record search are preserved.
- TAR traversal/metadata limits and nonterminating source selections are bounded
  failures. These deliberate bounds are not exact parity with hangs.
- Source queue timing, cross-request in-flight coalescing, all malformed query
  coercions, public-network/CDN behavior, unusual multi-volume/ZIP64 layouts and
  Windows runtime qualification still need broader differential evidence.
