# FileReader readiness seam for event-driven HTTP

Parent f386b80deeab954df52afcb255d58b47555ce24d.

FileReader::setReadable adds same-lane notification for ordered buffered data,
EOF and terminal read failure. The existing Refresh callback retains its separate
missing-piece demand meaning. Register before request(); a later registration
also observes already buffered/terminal state. Explicit close disables all future
notification, including close after failure, without discarding the failure reason.

Notifications run after pump invariants settle. A retained shared callable slot
preserves mutable callback-local state while allowing replacement, close or reader
destruction during delivery. A guarded drain prevents recursive delivery when an
immediate source completes newly requested chunks inside a callback. Exceptions
reset the delivery guard before propagation. Callback replacement during a current
notification affects subsequent state changes rather than replaying that event.

Public request, notifyPiece and takeData retain a local shared state through
callbacks. This lets synchronous readiness consumption/destruction return without
accessing the destroyed reader object's member storage. Existing demand, token,
selection, sibling-reader and queued-byte assertions are preserved.

## Qualification

K08-ready contains twelve controlled component scenarios: ordered/out-of-order
completion and EOF; missing-piece demand separation; terminal errors; callback
reader destruction; mutable callable state; close after failure; synchronous
consume/destruction; late canceled completion; late subscription; 256 synchronous
chunks with maximum callback depth one; throwing callback recovery; and callback
replacement without replay. These are actual FileReader/Scheduler components with
controlled sources, not an HTTP or real-torrent journey.

A no-op notification implementation failed the ordered readiness assertion.
The first uncommitted implementation then failed explicit close-after-error
rearming. Both failures are retained. Review also identified callable-copy state
and nested-delivery issues; the final regressions cover their repaired behavior.

    cmake --build build/server1 -j2
    ctest --test-dir build/server1 -R K08- --output-on-failure
    ctest --test-dir build/server1 --output-on-failure --timeout 90

Exact results/hash are in evidence/dots-2026-10-18/reader-readiness-tests.txt.
Matching RelWithDebInfo ASan uses detect_leaks=0; LeakSanitizer and Windows remain
unqualified. HTTP would-block/resume, deferred metadata ownership and complete
HTTP-to-player streaming remain separate unfinished gates.
