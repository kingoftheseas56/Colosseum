# H01 prerequisite: native HTTP request fidelity

Parent c7493fdcb2da0b0a09c47f1b36c8cac22ae14978.

The actual TCP connection previously dispatched only C-string method/target/body.
Router Request had no headers, so Range and enginefs-prio were unavailable to
future torrent handlers. Embedded NUL truncated a binary request body.

The new completed-parser dispatch copies ordered duplicate header name/value
pairs and the explicit body length before entering synchronous handlers. Existing
method/target/query/parameter behavior and the legacy dispatch ABI remain intact.
New request accessors perform case-insensitive header lookup without combining
or silently dropping duplicates. This is raw request fidelity, not the eventual
Node/Express duplicate-header normalization policy.

The real TCP H00-09 regression sends two differently cased Range headers,
enginefs-prio, and a three-byte a/NUL/b body. It failed against the previous
Connection dispatch and passes after switching to the completed-parser path.

Review exposed another actual parser defect: control bytes were accepted in
header values and could be truncated by C-string consumers. An actual-wire NUL
case failed before the parser fix. Header values now reject bytes below 32 except
HTAB, and DEL, with HTTP400 before invoking a handler. Tests cover NUL,1,31,127
and preserve valid tab/obs-text(128) bytes. An independent Node22.16.0 localhost
HTTP probe confirms those six acceptance/rejection outcomes. That probe is not
execution of the entire locked Stremio bundle.

## Reproduction and limits

    cmake --build build/server1 -j2
    build/server1/p03-tests/server1_h00_http_contract H00-09
    ctest --test-dir build/server1 -R H00- --output-on-failure
    ctest --test-dir build/server1 --output-on-failure --timeout 90

Matching RelWithDebInfo-ASan uses detect_leaks=0. Results and executable identity
are recorded in evidence/dots-2026-10-18/http-request-snapshot-tests.txt.
Original missing-header/body and malformed-control failures are preserved.

Deferred request ownership, metadata waits, FileReader would-block/resume and
actual torrent HTTP routing remain unfinished. No HTTP-to-player or complete
Stremio parity claim. Windows and LeakSanitizer remain unqualified.
