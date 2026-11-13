# Native YouTube resolver: pinned behavior and compatibility

## Source and scope

The reference is the Stremio service 4.21.1 `server.js` bundle, SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
M564 lines 46671–46701 install the two routes. M935 forwards to M936
(`getInfo`), M470 (metadata/filter/sort), M471 (ID parsing), and M473
(signature action extraction). M469 identifies the bundled dependency as
**ytdl-core 4.9.0**. M937 supplies the legacy format table; M474's cache
lifetime is one second.

The implementation is C++17/Qt Core and Network. It does not execute downloaded
JavaScript, invoke Node in production, embed a JavaScript runtime, invoke
`yt-dlp`, or change which extractor version is being reproduced. The player
script is parsed only for the old finite reverse/swap/slice/splice vocabulary.
The format table and regular expressions are generated from the pinned modules
and checked into small `.inc` files.

Implemented path:

- `/yt/:id.json`: selected format with JSON content type, or 404 with an `err`
  property. The latter is **intentional**: M564 initially sets 403 on an error
  but then overwrites it with 404 because there is no format URL.
- `/yt/:id`: 301 with `Location` for the chosen format; an empty 403 response
  for resolver errors; empty 404 for an absent format without error.
- Watch HTML, old watch JSON, old `get_video_info`, and embed-player discovery.
- Legacy signatureCipher/cipher query parsing and signature actions, including
  the source's non-modulo destination index in its swap operation.
- URL ratebypass/signature parameters, duplicate URL replacement, M937 format
  defaults, codec metadata, format priority and audio-and-video filtering.
- Legacy HLS itag extraction and DASH parsing, including the source's
  last-representation-per-manifest-URL behavior.
- Asynchronous network work and guarded deferred HTTP response ownership;
  coalesced concurrent resolutions and one-second successful-result cache.

## Verification

`tests/run_youtube_oracle.cjs` verifies the bundle hash, loads the actual pinned
M936 dependency graph, substitutes only fetched HTTP response bytes, and calls
the actual M470 selection code. It separately executes the exact two route
callbacks extracted from M564. No network is used by the deterministic suite.

The committed synthetic fixtures cover 23 scenarios: progressive ranking,
content-length preference, audio/video-only rejection, both cipher spellings,
metadata overrides, unknown itags, duplicate URL replacement, URL form encoding,
empty formats, bad player scripts, private/removed videos, invalid/empty/long IDs,
HLS selection and live filtering, DASH behavior, and all three metadata discovery
paths plus embed fallback. Native tests also verify coalescing, successful cache
reuse, consent/age-restriction stops, resolver destruction, and response retirement
before asynchronous completion.

The CMake fragment registers `YT-fetched-page-fixtures` and, when the pinned
oracle and Node are configured, `YT-pinned-resolver-differential`. A separate
Release build against Qt 6.11.1 and the existing HTTP contract library exercises
the same fragment without modifying the parent CMake graph.

## Current public-service result

On 2026-11-13, ordinary TLS-verified HTTP GETs retrieved the public watch page for
`jNQXAC9IVRw` and its publicly referenced player script
`/s/player/fad7e4e9/player_ias.vflset/en_US/base.js`. The watch response had
`playabilityStatus.status = OK`, adaptive formats, and no progressive formats.
The downloaded player was about 3 MB. Feeding those exact fetched bytes to both
the pinned source and native implementation produced the same error:

> Could not extract signature deciphering actions

That fetched-page differential passed, including both route responses. This is
**not evidence that current YouTube playback works**. It establishes that this
particular current player is incompatible with the old pinned extractor and that
the native implementation reports the same failure. A second public watch probe,
`BaW_jenozKc`, returned `ERROR: This video is unavailable`; this was not represented
as playable. The raw public pages are temporary qualification inputs, not tracked
fixtures. Playback media was not downloaded.

To reproduce a fetched-page differential, prepare a local JSON object with
`name`, `id`, and a `pages` map from exact canonical request URLs to fetched UTF-8
bodies. Set `SERVER1_YT_PUBLIC_FIXTURE` to that JSON file when running the oracle
runner with its normal bundle/output/native-test arguments. This mode does not
fetch anything or bypass an access restriction itself.

## Explicit limitations and deliberate safety differences

This slice must not be called complete universal 1:1 ytdl parity:

- Restricted playability statuses, sign-in and consent pages stop immediately.
  It does not try alternate endpoints to evade those restrictions. The old source
  sometimes continues for statuses other than `ERROR` or a private-video marker.
- Production fetching permits only HTTP(S) YouTube/Googlevideo resource hosts,
  with no credentials or nonstandard ports. Redirects are rejected for review;
  SSL errors are never ignored. Responses are limited to 16 MiB and each request
  to 30 seconds. These are deliberate bounds absent from the old callback surface.
- It does not implement cookie/identity-token flows, the old watch-JSON identity
  headers, or miniget's exact retry/backoff/redirect transport behavior. It does
  not accept cookies or authenticate the user.
- The successful final-result cache is bounded and starts its one-second expiry
  at completion. The source has separate promise/watch/player caches whose
  insertion-based timers and mutation semantics are not reproduced exactly.
- Only the data consumed by these routes is reproduced. Related videos, chapters,
  author metadata, download streaming, and arbitrary `chooseFormat` options are
  not implemented as public native APIs.
- Errors from malformed JSON/XML, pathological signatures/URLs, and native
  network failures may differ in wording from Node's engine/parser errors.
  Signature swaps above the bounded native allocation limit are not reproduced.
- Current YouTube `n` transformations, server-ABR playback, and newer player
  algorithms are not retrofitted into the pinned 4.9.0 implementation.

## Parent integration

1. Include `tests/cmake/YouTube.cmake` after `colosseum_server_v1` exists and call
   `server1_add_youtube_sources()` once.
2. Call `server1_register_youtube_tests()` inside the test-enabled block.
3. Own a `youtube::YouTubeResolver` in `ServerComposition`, with lifetime spanning
   installed routes and active requests.
4. Install `http::installYouTubeRoutes(router, resolver)` before catchall routes.
   Installation owns only guarded route contexts; it does not own the resolver.
5. Rerun the two registered tests and composition build after wiring. No shared
   composition or top-level CMake changes are part of this worker's slice.
