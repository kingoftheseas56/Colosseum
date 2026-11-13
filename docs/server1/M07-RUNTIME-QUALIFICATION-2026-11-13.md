# M07 legacy runtime qualification, 2026-11-13

This record supplements the recovered M07 source checkpoint. It does not assert whole-server parity, Windows qualification, or a production release.

## Source authority and shared preparation

The official v4.21.1 CDN server bundle (embedded version 4.21.0) is 6,676,503 bytes with SHA256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

Module 666 uses the named queue in module 72 for preparation, keyed by source. The queue fans out one result to existing and late-joining callbacks, retaining the first task's options. The native implementation now shares preparation independently of the first HTTP consumer. Canceling one waiter leaves others intact; canceling the last waiter terminates native owned work. This last-consumer resource cleanup is a native ownership guarantee, not a claim that JavaScript exposes equivalent cancellation.

`run_legacy_runtime_oracle.cjs` executes the exact queue source with controlled scheduling. Native lifecycle tests establish shared PID ownership, independent cancellation, caching, error fanout, restart, and HTTP-disconnect isolation. Reentrant callback tests cover sibling cancellation, same-key success/error requests, service destruction, and synchronous missing-executable completion.

## Cold subtitle waiting remains a deliberate existing difference

Exact module 305 schedules a 12,000 ms completion, then reads undeclared `allSubs` when `waitForSubs` is truthy. `evs` is likewise not in that module's lexical scope. Module 666's separate variables are passed to subtitle/playlist modules, not module 305. An isolated execution produces `ReferenceError: allSubs is not defined` before invoking the callback. Executing the already-scheduled timer in isolation populates the cache; a warm cache bypasses the faulty branch.

The oracle regression verifies those facts. Native behavior continues to render using currently received subtitles without waiting. It neither crashes the server to reproduce the defect nor silently implements hypothetical intended waiting. This is explicitly not parity for cold `waitForSubs`; choosing different behavior requires a separate product decision.

## Real production HTTP media

`tests/run_legacy_bundled_runtime.py` starts the production host on an ephemeral loopback port with discovery disabled, generates a seven-second synthetic H.264/AAC MP4, requests real routes, and records response hashes and decoder results. It accepts explicit executables and performs no downloads.

The tested server encoder came from official Stremio service Debian package 0.1.22-1 amd64:

- Package SHA256: `fcbab342fcd8990312b400698ae85f9c87aed0326a0da0168c1ddba18c39a415`.
- Bundled FFmpeg: 4.4.1-static; SHA256 `3ea58083710f63bf920b16c7d5d24ae081e7d731f57a656fed11af0410d4eb48`.
- Package's declared source: https://github.com/Stremio/stremio-service/.

Observed production-route results:

- `/master.m3u8`: 200, 325 bytes.
- `/stream-q-240.m3u8`: 200, 179 bytes.
- `/stream-q-240/0.ts`: 200, 379,008 bytes; H.264/AAC decoded.
- `/dlna`: 200, 376,564 bytes; source-compatible `-vbsf` arguments; DLNA content/transfer headers; H.264/AAC decoded.
- `/thumb.jpg`: 200, 10,704 bytes; MJPEG decoded.
- `/stream-q-o/0.ts` without ffsplit: 500, exact `no ffsplit found` body.

Output inspection/decoding used explicitly selected system FFprobe/FFmpeg 7.1.5. Both bundled 4.4.1 FFprobe and FFmpeg crashed with SIGSEGV when reading the generated MPEG-TS during earlier qualification attempts; those failures were retained. Bundled FFmpeg successfully generated the routed media from MP4. Consequently this qualifies encoder/HTTP output compatibility plus independent decoding, not end-to-end bundled decoder compatibility.

The successful machine-local receipt directory is `server-build/legacy-bundled-jatf467z` beside the build tree. Earlier failed runs are `legacy-bundled-dspwytp0` and `legacy-bundled-_gx1_4a2`. These are generated evidence, not tracked fixtures.

## Concrete ffsplit blocker

The pinned server bundle contains lookup paths for `node_modules/stremio-ffsplit-prebuilt/bin/ffsplit.bin`, `$HOME/hls-segment-splitter/ffsplit.bin`, and a Windows sibling executable. It contains no ffsplit package version or download authority.

On 2026-11-13 the official npm registry endpoint https://registry.npmjs.org/stremio-ffsplit-prebuilt returned HTTP 404. GitHub repository API lookups for `Stremio/hls-segment-splitter`, `Stremio/stremio-ffsplit-prebuilt`, and the corresponding `Ivshti` names also returned HTTP 404. No ffsplit executable is present in the recovered official service package. The official Stremio service source tree at commit `1891799734acba88904f9f62cb8ca491873b36fb` has no ffsplit path.

Therefore no provenance-verifiable compatible ffsplit binary/version was available from the checked official sources. Exact splitter argument parity and the missing-executable route response are tested; real splitting remains unqualified. No substitute executable or guessed implementation was introduced. The real-runtime script supports `--ffsplit` when an authoritative binary becomes available.

Other existing bounds remain: native conversion concurrency is eight; ordinary MP4 metadata retention is capped at 256 MiB. Neither is an unbounded-source parity claim.

## Final focused verification

All eight registered M07 gates passed in the cleanly rebuilt production suite. After adding the final reentrant and synchronous-completion cases, `server1_legacy_runtime_tests` was rebuilt and `M07-runtime-routes-lifecycle` passed again in 25.10 seconds with zero runtime failures. The explicit routed bundled-encoder run also passed separately as described above. This does not change the documented decoder/splitter/subtitle limitations.
