# Event-driven HTTP streams and active-read ownership

Parent a184d25c4b4f45b22eb6551418e506c3948e8891.

Opt-in asynchronous streams can return SERVER1_HTTP_WOULD_BLOCK (-2). Legacy
streams still treat negative reads as errors. A caller-owned weak wake handle
resumes a parked stream on the response's serialized app lane. Notifications
coalesce by connection response identity; a blocked stream does not schedule a
polling loop. Early and synchronous notifications are retained, and stale wake
handles return 0 after response retirement or stream replacement.

Connection code rechecks its lifetime, closed/released state and queue identity
after invoking arbitrary producer code. Response stream context and response
lifetime callbacks stay owned through an active read, even if that read destroys
the server. Stream context closes before response leases retire. Wake invalidation
is immediate, not delayed until the active callback returns.

## Reproduced defects and qualification

The legacy read ABI reproducer in tools/server_lab/repros/http-read-lifetime
builds against the exact parent HTTP components and produces an AddressSanitizer
heap-use-after-free when a read destroys its server then accesses its own heap
context. The same reproducer passes against this candidate. The retained failing
and passing outputs are in the accompanying evidence directory.

H00-10 exercises nine actual TCP scenarios: delayed fixed-length data, synchronous
wake during read, disconnect, stop, destroy during read, legacy negative error,
HEAD/no body read, wake before subscription, and chunked delayed data. It checks
100 notifications coalesce to one extra read while no data is available. A tenth
actual TCP scenario uses a heap-owned context and checks stream and response
ownership survive through the active read and retire exactly once afterward.
Existing H00 assertions remain unchanged.

- Full recovered Release suite: 106/106 PASS, 0 FAIL, 0 skipped, 74.65s.
- Fresh matching Qt6.11.1 RelWithDebInfo-ASan HTTP suite: 10/10 PASS, 2.40s,
  detect_leaks=0, QT_FATAL_WARNINGS=1.
- Independent exact-parent legacy-read reproducer: ASan heap-use-after-free,
  exit1; same fixture against candidate: PASS, exit0.

## Reproduction

Use docs/server1/LINUX-BUILD.md for the static dependency build. This recovery
used GCC14.2, Qt6.11.1, Boost1.83.0, unmodified static libtorrent2.0.11 and
OpenSSL3.5.7. Official SDK/archive identities are recorded in the receipt.

    cmake --build build/server1 -j2
    ctest --test-dir build/server1 --output-on-failure --timeout 90

The full suite uses SERVER1_TEST_FIXED_INTERFACE=1 and portable PowerShell7.6.7
on PATH with writable XDG cache/config/data directories. Supply the exact locked
SERVER1_K12_ORACLE and Node to register the source differential tests.

For the independent legacy-read reproducer, configure its CMakeLists with ROOT
pointing to the desired exact native/colosseum_server_v1 source directory and
CMAKE_PREFIX_PATH pointing to Qt6.11.1. Use -fsanitize=address
-fno-omit-frame-pointer, RelWithDebInfo and ASAN_OPTIONS=detect_leaks=0.

## Recovery and limits

At23:57UTC on18October the prior server workspace was absent from the current
filesystem; the cause and cleanup schedule are not established. The published
105-test parent was recovered with Git. Unpublished edits were reapplied and
independently rebuilt/tested; this is fresh evidence, not reuse of missing logs or
an assertion that reconstructed files are byte-identical to the lost candidate.
The exact oracle archive was recovered and its SHA256 independently reverified.

This qualifies HTTP/component ownership and controlled localhost byte transport.
It does not yet connect Registry/FileReader to the torrent HTTP route or implement
owned asynchronous metadata resolution. Full locked Node server startup remains
blocked by uv_interface_addresses errno1 in this environment. Windows,
LeakSanitizer, HTTP-to-player playback and complete Stremio parity remain open.
