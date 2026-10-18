# HTTP binary body and pipeline backpressure repairs

Dedicated Server 1 HTTP component; no complete streaming-server claim.

## Reproductions and changes

1. Non-stream responses converted an explicit byte buffer to a NUL-terminated
   std::string. H00-04 failed on embedded/leading NUL bytes. Construct the string
   using the existing response_body_size API. Native loopback GET/HEAD cases now
   preserve exact lengths and bytes, including empty and binary bodies.
2. While output was pending, readyRead repeatedly drained Qt's bounded socket
   buffer into an unlimited deferredInput buffer. H00-05 observed 12,845,056
   retained input bytes while output was paused. Leave later bytes in Qt's bounded
   buffer instead, then explicitly resume buffered reads after earlier pending
   input and the response have completed. The same fixture retains 3,211,264
   bytes after repair.

The general per-connection pending-input bound is approximately two socket-buffer
caps: bytes already consumed alongside the first parsed request plus the bounded
Qt socket buffer. The single-cap measurement above applies to the controlled
fixture; it is not a universal total-memory claim. Active parser/output state is
separate. The new backlog diagnostic explicitly excludes the active parser.

The initial repaired test expected immediate disconnect observation while both
input backpressure and the artificial output pause were active. That attempt is
retained. The final test resumes the artificial pause after client abort and keeps
the cleanup assertion. It also checks same-write pipelining and a second request
sent separately while the first response is paused, then verifies ordered replies
after resumption. No production timeout was increased.

## Executed evidence

```sh
cmake --build build-server1-linux --target server1_h00_http_contract -j2
ctest --test-dir build-server1-linux -R 'H00-' --output-on-failure --timeout 30
ctest --test-dir build-server1-linux -R 'H00-0[45]' \
  --repeat until-fail:20 --output-on-failure --timeout 30
```

Five native loopback test invocations PASS; both new cases repeated twenty times
each: 40 PASS. Existing parsing, route, stream, output-cap and disconnect assertions
remain intact. Linux GCC 14.2.0 Release, Qt 6.11.1.

Executable SHA256:
`e27d92f8f3ec9bdfb27c76384ec1e2e3c7e9f422798c4de04ef79991791636ba`

These are production HTTP-component tests, not engine-to-HTTP composition or
player/Windows qualification. Deferred route completion and asynchronous reader
readiness are still required before torrent endpoint integration.
