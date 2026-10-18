# HTTP response ownership for the forthcoming torrent adapter

Parent: 99293cd085cc702aba21638350054faac55002ec.
This adds the response ownership seam needed by K12/H01. It does not yet install
EngineRegistry leases in a production torrent route or make the host streaming-ready.

## Changes

- One owned nonthrowing response-lifetime callback, separate from body reader
  destruction. Failed/duplicate attachment retains caller ownership.
- Buffered responses and HEAD responses remain owned by the connection until
  socket drain, disconnect, error, queue rejection or server stop.
- Stream/body replacement and middleware fall-through retain the logical
  response lifetime. Moving a response transfers its ownership.
- Queue cleanup detaches responses before invoking callbacks. Retirement cannot
  recursively destroy the same response or invalidate the active queue iterator.
- Finalizers can stop/destroy the server. Server destruction invalidates the
  handle and stops I/O immediately; QObject deletion is deferred through the
  owning Qt event loop. Destruction requests are idempotent before stop begins.
  The owner must drain deferred deletes before destroying its event loop.
- QPointer guards protect post-finalizer continuations; stop/drain loops use
  guarded snapshots rather than iterators invalidated by callbacks.

## Reproduction and evidence

    cmake --build build/server1 -j2
    ctest --test-dir build/server1 -R H00 --output-on-failure
    ctest --test-dir build/server1 --output-on-failure --timeout 90

H00-06 originally fails: buffered/HEAD response lifetime ended before drain.
It now checks GET and HEAD, queued ownership, full drain and finish/disconnect
idempotence over real localhost sockets.

H00-07 covers buffered/stream disconnect, stop, stream error, length mismatch,
queue caps, reentrant stop, body replacement and middleware-to-static replacement.

H00-08 uses finalizers that destroy the server on cap rejection, disconnect,
normal drain and stop. AddressSanitizer first reports heap-use-after-free in
handleSocketError after the finalizer deletes the connection. Guarding only our
continuations then exposes a Qt canReadNotification crash because synchronous
socket deletion invalidates Qt's own event stack. Deferred QObject destruction
fixes that actual lifetime issue. Both failed attempts remain recorded.

Detailed totals, executable hashes and original failure excerpts are in
`evidence/dots-2026-10-18/http-lifetime-tests.txt`. The full tests still include
all previous assertions. ASan uses detect_leaks=0; prior LeakSanitizer host failure
is still an explicit qualification gap. Windows is NOT RUN.

## Remaining integration

The torrent adapter must attach a Registry lease after successful file resolution
and redirect exclusion, before HEAD/range handling. The same lease must retire
on this response boundary, never merely on reader EOF. Asynchronous metadata
resolution and FileReader would-block/resume still need an owned HTTP seam.
These tests are real native HTTP component tests with controlled payloads;
they are not a full torrent HTTP or player demonstration.
