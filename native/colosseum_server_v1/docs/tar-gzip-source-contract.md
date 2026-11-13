# Custom TAR and gzip source contract

Authority: Stremio Service v4.21.1 server.js SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
The controlled oracle evaluates modules M1297 and M1304 through the repository
module extractor, supplying only Node's util/events/stream builtins. M563 uses
Node Gunzip for gzip decoding; the native codec is zlib with gzip framing,
trailer integrity verification, concatenated members, and trailing zero padding.

## Preserved observations

- USTAR and GNU format checks, unsigned checksum, UTF-8 names, USTAR prefixes,
  GNU long names/links, PAX path/linkpath/size overrides, and zero-block skipping.
- Global PAX data is applied only when a subsequent local PAX header occurs.
- A file header ending in `/` retains type `file`: the source adjusts typeflag
  after it has already computed the returned type.
- M1297 uses its relative header offset as an absolute seek position when
  advancing past a nonempty file. A three-file archive can enumerate a,b,b,
  rather than a,b,c. The native random-access reader retains this behavior.
- M1297's dataOffset is streamStart + twice the relative header position + 512.
  Long metadata or zero headers can therefore yield offsets past the physical
  file data. This is retained, rather than silently replaced by normal TAR
  parsing. M1304 reports the same doubled metadata offset but consumes actual
  payload sequentially; the native entry keeps the physical streamOffset too.
- M1297's inclusive underlying range yields size+1 bytes for an ordinary full
  entry stream. TarEntryReader preserves this; HTTP framing may independently
  cap the transmitted body to the declared response interval.
- M1304 entry data is consumed forward. GzipReader's forward adapter decompresses
  skipped data in bounded chunks; it does not retain whole archives or bodies.

## Explicit safety differences / remaining parity scope

The JavaScript reader can hang indefinitely on no match, truncated input,
zero-width ranges, out-of-source entry positions, or its repeated seek loop.
The native implementation returns errors/end instead: metadata traversal is
limited to 10,000 headers, individual extended metadata to 1 MiB, and every
public read/pull to 1 MiB. A compressed input request is at most 64 KiB. Invalid
negative/overflowing sizes are rejected. These are intentional bounded failure
paths, not claims of identical broken-source timing or event behavior.

The forward adapter disallows rewinds, and cancellation invalidates pending
callbacks before aborting the upstream request. A gzip reader must be recreated
for a new request. These readers do not write/extract archive contents to disk.

The checked differential corpus covers 26 custom TAR cases plus five Gunzip
cases, including a concatenated-member boundary exactly at 64 KiB compressed
input, and the first trailing zero terminating subsequent members. The C++ harness also covers selected bytes, integrity/truncation failure,
forward skip policy, lazy reads, and cancellation. It is component evidence;
it is not a claim that every M562/M563 queue, malformed input or network timing
case has 1:1 parity.

Run `tests/run_tar_gzip_tests.py --candidate <test_tar_gzip> --oracle <server.js>`.
