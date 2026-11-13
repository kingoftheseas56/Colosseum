# Archive preparation ownership and admission

## Scope and source evidence

This slice ports the preparation scheduler for ZIP, TAR, GZIP/TGZ, RAR and 7zip.
It is not a claim of complete archive, compression-format, transport or server
parity. The reference is the accepted v4.21.1 server bundle, SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

The relevant source modules are:

- M72: named queue, with FIFO admission and an active-task map.
- M514, M561, M546, M562 and M563: RAR, 7zip, ZIP, TAR and GZIP preparations.
  Each constructs its queue with concurrency 10 and sets `task.id = task.query.key`.
- M1234/M1295: completed seekable ZIP/TAR selections are cached by key, outside
  the preparation queue. ZIP local-header lookup occurs after directory selection
  has released its queue slot. Compressed ZIP HEAD and rejected ranges need only
  central-directory metadata.
- M514/M561: completed RAR/7zip selections are cached by the owning task's exact
  `req.url`, inside the preparation queue. A coalesced subscriber's different URL
  does not gain its own cache entry.

`run_archive_scheduling_oracle.cjs` evaluates the actual M72 module with a
controlled immediate-callback lane. It observes duplicate fanout, ten-active
FIFO admission, the saturated duplicate edge and source reentrant callback loss.
It also evaluates the five preparation exports with isolated dependency stubs to
observe their actual queue limits and task IDs. These wrapper checks do not run
the original HTTP routes or original remote archive parsers. The existing ZIP,
TAR/GZIP, source-policy and RAR/7zip differential suites supply separate parser
and range evidence.

## Native ownership

Each mounted archive family owns one registry, a FIFO of weak response
subscribers, and at most ten active preparation owners. Active preparations are
identified only by query key. The first admitted task's source list, URL and
selection options own its result. Concurrent requests for that key inherit that
selection even when their selectors differ. Request method, Range, Origin,
response lifecycle and payload-reader state remain independent.

A preparation owns the parser, source discovery and weak subscribers. It shares
an immutable selected-entry value plus an owned random-access byte source on
completion. ZIP local-header lookups belong to individual requests after
selection; only successful stored-entry resolution publishes the key cache.
Completed caches retain the source coordinates and source object associated with
the selection. No `Request`, entry-reader cursor, inflater or response buffer is
cached or shared. Identical random byte intervals may share a transport request,
but each reader has its own cancellation token.

Cancelling one response removes only that subscriber. Cancelling the final
subscriber retires the preparation, cancels its parser/source work and releases
its slot. Abandoned queued and missing-key subscribers do not retain producers.
Outstanding responses keep their registry and source alive after router teardown.
All admission, completion and cancellation run on the serialized Qt application
lane used by the HTTP contract.

## Source quirks preserved

- Concurrent active requests with different selectors share the first task's
  selected file, rather than being keyed by full URL or selector JSON.
- Only active tasks coalesce. A duplicate queued before initial admission can
  remain behind ten active tasks. If its earlier same-key task completes before
  admission reaches it, it becomes a new task and uses its own selector.
- ZIP/TAR cached requests bypass saturated preparation queues. RAR/7zip cached
  requests still pass through their queue, and their cache identity is the exact
  task URL. Compressed ZIP and GZIP/TGZ have no completed-selection cache.
- ZIP local-header work is outside the queue. A new selector can start another
  same-key preparation while an earlier local-header lookup remains pending.

## Intentional lifecycle differences

- Source M72 retains callbacks and preparation work after client disconnect.
  Native work is cancelled when its final subscriber leaves. A later request
  starts fresh, and stale callbacks cannot resurrect retired tasks.
- M72 deletes the active ID after callback fanout. A callback that synchronously
  pushes another same-ID callback can append it after `forEach` has captured its
  length; that new callback is then lost. Native completion removes the old ID
  before calling subscribers, so reentrant work cannot join a retired callback
  list. It can use a completed cache or queue a fresh preparation.
- M563 hands concurrent consumers one mutable gzip/TAR stream. Native consumers
  share the selected entry but use separate gzip readers. TGZ readers replay and
  skip the prefix in bounded chunks to reach the selected entry. This may repeat
  compressed reads/decompression, but one consumer's cancellation, speed or
  cursor cannot corrupt another consumer. No whole-archive replay cache is added.

Existing invalid-range safeguards, traversal/metadata bounds and unsupported
compression cases remain in force. These differences must not be described as
byte-for-byte reproduction of all concurrent JavaScript stream behavior.

## Verification

`A06-archive-preparation-scheduling` uses synthetic legal archive fixtures,
loopback HTTP byte sources and the pinned queue observations. It covers all five
families' active limit/FIFO and cache admission, selector inheritance, separate
range responses, subscriber cancellation, same-range transport sharing, final
cancellation/retry, queued abandonment, late key creation, failures, cache
identity, ZIP local-header boundaries, compressed HEAD/range shortcuts,
reentrant success/error callbacks, cached RAR/7zip reentrancy, router teardown,
and real downstream-client disconnect. Initial ten TCP arrival events are
normalized as an unordered cohort; queued admissions 11 and 12 remain ordered.

The focused sanitizer build instruments the changed HTTP/archive sources and
regex helper with AddressSanitizer and UndefinedBehaviorSanitizer. Qt, zlib and
liblzma remain external prebuilt libraries. LeakSanitizer cannot run in this
execution environment because its process inspection is blocked by the traced
runtime; leak detection is disabled for the ASan/UBSan runs. This is a test
limitation, not a successful leak check.

Final focused validation on 2026-11-13:

- Fresh Release build: all 11 selected A00/A02/A04/A05/A06 tests passed.
- ASan/UBSan: A06 scheduling/lifecycle harness passed (2,751 checks), existing
  ZIP/TAR/GZIP route harness passed, and RAR/7zip real-socket harness passed
  (1,048 checks). No sanitizer finding was reported with leak detection disabled.
- Independent code review and an independent earlier A06 Release/ASan/UBSan run
  passed. The subsequent expansion changed tests only, not the reviewed route
  implementation.
- Repository-wide compilation/tests and final publication remain the aggregate
  task's responsibility; these focused results do not imply a full-server pass.
