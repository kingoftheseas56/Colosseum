# Native RAR and 7zip source backend

## Source evidence

The source oracle is Stremio service 4.21.1 server.js, SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

- RAR entry/API and HTTP route: M1120, M1121; selection M514; parser M1124.
- 7zip entry/API and HTTP route: M1286, M1287; selection M561; parser M1288.
- M1289 performs LZMA header decoding, not entry-payload decompression.

Both source parsers reject selected compressed payloads with `Decompression is
not implemented`. Supporting compressed RAR or 7zip payloads would expand the
source contract rather than complete this parity slice. This backend implements
RAR4/RAR5 metadata and stored payloads, and 7zip metadata with COPY, LZMA, and the
source's restricted LZMA2 encoded-header handling. liblzma is only the raw LZMA
primitive, not a replacement archive parser.

## API and integration

Add `src/archive/RarSevenZipSource.cpp` to the archive library and link
`LibLZMA::LibLZMA`. Existing ArchiveKeys and MultipartSource are required.

1. Apply `orderArchiveStoredUrls(family, urls)` before opening ArchiveByteSource.
2. Construct RarSevenZipSource with physical size and the source read/cancel
   callbacks; pass physical part lengths to setVolumeLengths.
3. Call find with a predicate `(name, index)`. This returns ArchiveStoredEntry
   containing name, public length, and physical byte chunks.
4. HEAD/GET metadata uses the returned public length. ArchiveStoredReader accepts
   inclusive start/end; pull returns ZipChunk. Use the route's advertised-length
   cap rather than assuming all returned body bytes fit Content-Length.
5. Destroy readers/parser before destroying ArchiveByteSource; destruction and
   cancel detach completion before cancelling the pending upstream request.

The route registry owns req.url-keyed successful selection caching, key waiters,
query predicates, MIME, status/header serialization, and HTTP disconnect cleanup.
The backend alone does not prove those real-socket behaviors.

## Intentionally retained source behavior

- Public length is the sum of physical chunk lengths minus one per chunk.
- Chunk maps use inclusive physical bounds despite that reported length. Full
  raw reads can produce additional bytes; HTTP advertises the shorter length.
- The special range end adjustment and boundary mapping use M277 arithmetic.
- RAR per-volume selection indexes restart at zero. Equal-sized continuation
  chunks after the second volume use the source's header-skipping heuristic.
- RAR4 names are decoded as UTF-8, including names marked with special-name flags.
- 7zip ignores the within-folder offset when selecting a later solid entry.
- 7zip volume chunk construction uses the first volume length in its offset
  calculation and the source's shortened subsequent-volume calculation.
- LZMA2 control 2 is rejected; compressed packets use fresh LZMA decoders and
  consume the property byte on every packet, as the source does.

## Verification and remaining boundaries

`tests/run_rar_sevenzip_oracle.cjs <bundle> <output.json>` creates entirely
synthetic archives and evaluates the pinned modules. `test_rar_sevenzip` consumes
the resulting oracle observations. The current suite has 23 source cases and 742 native assertions, and covers RAR4/RAR5 first and
later entries, UTF-8 names, two-volume joins, continuation cloning, plain and
COPY/LZMA/LZMA2 encoded 7zip headers, multiple folders, the solid-offset defect,
two-volume shortened chunks, invalid signatures, compressed payload rejection,
full and partial reads, invalid range ends, ordering/filtering, lazy reads and
cancellation. The fixtures are generated test data, not downloaded archives.

Known differences, not claimed as closed parity:

- Source metadata reads frequently fetch an extra byte, use a 281-byte RAR4
  window, and stop at a physical volume boundary. Native metadata acquisition
  uses exact requested lengths, reads in 64 KiB chunks, and can join metadata
  spanning physical volumes. Payload selection/range quirks are preserved.
- Malformed inputs have explicit bounds, integer and traversal validation rather
  than source Buffer exceptions, undefined values, or unbounded loops. Metadata
  is limited to 16 MiB, each count/traversal to 10,000, and raw LZMA decoder memory
  to 256 MiB. Some malformed inputs consequently have different internal errors.
- liblzma and the source's JavaScript LZMA decoder agree on the generated header
  fixtures, not yet on an exhaustive malformed/compressor corpus.
- Bundle sorting is stable for equal names/order keys, while the source's
  comparator returns -1 for both comparisons between two main `.rar` or `.7z`
  files. Such duplicate-main ordering has not been qualified.
- M514/M561 use a shared selection queue with concurrency 10. The native HTTP
  route currently starts independent per-request selections; identical pending
  selections are not coalesced or admitted through that source queue.
- Native metadata requests are serialized and bounded rather than source RAR's
  parallel URL probing. Request timing and byte-request traces are not equal.
- Linux real-socket route integration is qualified below. Windows liblzma
  dependency qualification and Windows runtime tests remain separate work.

## Route integration qualification

The ArchiveRoutes extension has been applied after the validated core commit
and compiled against the core libraries. `test_rar_sevenzip_routes.cpp` has passed
1,048 assertions over actual loopback HTTP sockets with generated upstream
volumes and the native downstream listener. It checks POST/create, HEAD 204,
full bodies, closed/suffix/open-ended ranges, parser failures, content-length
metadata, request-URL cache separation, queue bounds, and cancellation during
held metadata and payload requests. The complete shared phase-2 CMake build
passed. All ten registered A00/A02/A04/A05 gates passed, including both A05 differential and real-socket tests.
The backend suite also passed a standalone undefined-behavior sanitizer run.

The native route returns a bounded 500 response for invalid/inverted ranges
where source createReadStream throws after setting headers. It does not claim
the source's unhandled rejection or potentially hung HTTP exchange as safe
behavior. Valid-range response bytes retain the source advertised-length cap.
