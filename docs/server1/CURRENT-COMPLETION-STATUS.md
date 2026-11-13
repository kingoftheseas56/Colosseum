# Native server current completion status

Updated 2026-11-13. This is a live implementation and qualification ledger for
`feature/colosseum-server-1.0`, not a replacement for frozen historical evidence.
**The server is not fully equivalent to server.js and is not release-ready.**
Passing registered tests does not establish untested surfaces or platforms.

The pinned oracle is Stremio service v4.21.1, embedded server version 4.21.0,
SHA256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
Production is native C/C++; Node22.16.0 is a test oracle, not a production
dependency or proof of identical Stremio-runtime behavior.

Status meanings:
- **Qualified scope:** implemented, with the specific component/loopback/application
  evidence linked below. This never means every source behavior is equivalent.
- **Missing:** production behavior is absent, even if a primitive or prototype exists.
- **Unqualified:** implementation or authored coverage does not yet have the
  required platform, external-service, device or runtime evidence.

## Surface ledger

| Surface | Current implementation and evidence | Remaining boundary |
| --- | --- | --- |
| Torrent engine, discovery, storage, scheduling and HTTP | Native engine/registry, pinned libtorrent adapter, tracker/DHT policy, piece storage, file readers, statistics and ranged HTTP are composed. Controlled real peers, tracker discovery, verified transfer, HTTP and HLS decode are qualified. [Core continuation](CONTINUATION-2026-11-13.md), [composition](../../native/colosseum_server_v1/src/ServerComposition.cpp). | Public-peer byte transfer remains unqualified. The bounded public probe obtained metadata but zero peer bytes. Fixed-interface fixtures do not qualify live interface changes. |
| HTTP ownership, parsing and CORS | Native request snapshots, deferred completion, bounded asynchronous response production, disconnect handling and middleware are implemented. [HTTP async stream](DOTS-2026-10-19-HTTP-ASYNC-STREAM.md), [router](../../native/colosseum_server_v1/src/http/Router.cpp). | Source middleware/profile and addon SDK method/error details are not universally equivalent. Defensive parser and framing limits remain intentional. No CORS/security expansion is part of current work. |
| Settings, cache, root and network information | Persistence, nested forms, engine/media settings, cache trimming, root redirect, heartbeat, network information, executable discovery and application-path selection are integrated. [Root routes](../../native/colosseum_server_v1/src/http/RootRoutes.cpp), [integration](CONTINUATION-2026-11-13.md). | The listener/base URL deliberately remain loopback-only. Windows drive discovery and full startup profiles need platform qualification. |
| HLS V2 | Real local-file and controlled-torrent HLS generation and independent decode are qualified, including actual application startup and first playback. [Indexing/runtime checkpoint](INDEXING-RUNTIME-CHECKPOINT-2026-11-13.md), [HLS implementation](../../native/colosseum_server_v1/src/media/HlsV2Service.cpp). | Real GPU success and broader media/platform compatibility remain unqualified. Generated sample defaults are qualified below. |
| Legacy HLS, DLNA stream, thumbnail, probe and tracks | Native preparation/indexing, render selection, process ownership, routes and Linux real-media output are implemented and qualified in the documented scope. [M07 qualification](M07-RUNTIME-QUALIFICATION-2026-11-13.md). | Windows bounded process stdout is missing. Actual ffsplit output is unqualified because no provenance-verifiable compatible executable was found. Bundled decoder crashes are not erased by independent decoder success. Cold subtitle waiting differs deliberately. |
| Subtitle tracks, hash and rendering | Native retrieval, decoding, charset handling, hash calculation and subtitle routes are composed and covered by focused/differential tests. [Subtitle routes](../../native/colosseum_server_v1/src/media/SubtitleRoutes.cpp), [media integration](DOTS-2026-10-19-NETWORK-MEDIA-INTEGRATION.md). | Bounded input/cancellation behavior is not an unlimited-source equivalence claim; full platform and arbitrary provider/media coverage remain unqualified. |
| HTTP proxy | Native proxy service and routes are composed with focused network/streaming evidence. [Proxy service](../../native/colosseum_server_v1/src/network/ProxyService.cpp), [media integration](DOTS-2026-10-19-NETWORK-MEDIA-INTEGRATION.md). | Passing controlled HTTP fixtures does not qualify every public origin, redirect, TLS or disconnect behavior. |
| IPC downloader | Key-gated native downloader routes and concurrency repair are integrated. [Downloader/archive qualification](DOWNLOADER-ARCHIVE-2026-11-13.md). | Qualification is for the documented controlled transfer/lifecycle scope, not all remote servers or platforms. |
| ZIP, TAR, gzip, RAR and 7zip | Native parsing, stored payloads, ZIP inflate, gzip decoding, multipart/range routes and real loopback delivery are implemented. [ZIP/TAR/gzip](DOWNLOADER-ARCHIVE-2026-11-13.md), [RAR/7zip](RAR-SEVENZIP-PARITY.md). Shared selection/preparation, source concurrency-10 admission and consumer ownership are now qualified in the [resumed checkpoint](RESUMED-CHECKPOINT-2026-11-13.md). | Split/compressor/malformed-input coverage is not exhaustive. Source also rejects compressed RAR/7zip entry payloads. |
| FTP | Native plaintext metadata, passive transfer, seek/nonseek ranges, cancellation and persistent sequential reads are qualified against loopback FTP and the pinned transport. [FTP contract](../../native/colosseum_server_v1/docs/FTP-SOURCE-PARITY.md). | FTPS is not integrated. Duplicate compressed-key initialization, uncommon server listings and all authentication/disconnect modes are not fully equivalent or qualified. |
| NZB/NNTP and nested archives | Native XML/yEnc, plaintext authenticated pools, request sharing, range delivery, URL acquisition and archive delegation are integrated and loopback-qualified. [NZB qualification](NZB-SOURCE-2026-11-13.md). | NNTPS is not integrated. Split NZB-to-archive combinations, duplicate initialization and complete pool timing remain qualification/implementation work. No public NNTP or user credentials were used. |
| YouTube JSON and redirect | Native implementation reproduces the tested pinned ytdl-core 4.9.0 resolver/route behavior. [YouTube contract](YOUTUBE-PARITY-2026-11-13.md). | Current public playback is not working/qualified: fetched player bytes failed decipher extraction in both pinned source and native. Modern extractor support would be new compatibility work. |
| Local-addon manifest, catalog, meta and streams | Native projections, filename/torrent indexing, optional providers, append/meta-cache persistence and uncached torrent lookup are integrated. Explicit synthetic roots and database-only reload are qualified. [Local runtime](LOCAL-RUNTIME-INTEGRATION.md), [indexing](LOCAL-INDEXING-2026-11-13.md). Startup catalog advertisement follows settings truthiness; bounded, cancellable explicit-root discovery replaces synchronous enumeration. Both are qualified in the [resumed checkpoint](RESUMED-CHECKPOINT-2026-11-13.md). | No implicit HOME scan, symlink traversal or automatic provider use. SDK OPTIONS differs; platform discovery and exhaustive recognition queue timing remain incomplete. |
| Samples, device information and hardware profiling | Startup/device profiling is integrated, with private conversion ownership preventing playback eviction. Original synthetic default samples and their functional media tests are integrated and qualified in the [resumed checkpoint](RESUMED-CHECKPOINT-2026-11-13.md). [Generated samples](GENERATED-PROFILER-SAMPLES.md), [profiling integration](LOCAL-RUNTIME-INTEGRATION.md). | Generated bytes are deliberately not source-identical. Software decode/conversion is not real GPU success. Upstream copied sample payloads are not part of this change. |
| Casting inventory, protocol and conversion | Passive inventory, source command/state functions, SOAP primitive, Cast framing and Linux transcoding/subtitle conversion are implemented and fixture-qualified. [Casting qualification](CASTING-NATIVE-QUALIFICATION.md). | Active discovery, production device control, full DLNA orchestration/events and external-player discovery/launch are missing from production. Staged provider work is not integrated or approved. No physical-device success is claimed. |
| HTTPS and certificate endpoint | No production HTTPS listener or `/get-https` certificate workflow is integrated. | **Missing and approval-required.** Staged secure transport work does not supply production TLS capability or authorize trust/listener changes. |
| Environment profiles and lifecycle | The native application supports explicit opt-in embedding and owns listener shutdown. [Linux/application build](LINUX-BUILD.md). Some individual environment flags are read. | Full TV/Unity/iOS/Android composition is missing: source flag mapping, conditional mounts, iOS stop/restart bridge and browser-open behavior are not complete. Current settings construction assumes non-Android. Do not infer profile completion from source matrices. |

## Resumed checkpoint

1. Archive preparation ownership, source-key coalescing and concurrency-10 admission.
   Preserve per-family completed-cache behavior and first-active-task selection
   semantics; independently cancel HTTP consumers and payload readers. See
   [preparation ownership and qualification](ARCHIVE-PREPARATION-PARITY.md).
2. Generated default profiler samples. Preserve explicit external pinned-sample
   validation, missing-sample tests, real conversion checks and playback isolation.
3. Local-addon startup manifest selection from `localAddonEnabled`. A settings POST
   must not silently activate scanning/providers or change source startup-only
   manifest behavior.
4. Bounded, cancellable explicit-root discovery. Yield between enumeration batches,
   stop promptly on cancellation or owner teardown, and preserve the existing
   root, extension, exclusion, depth, symlink and provider-access boundaries. See
   [discovery scheduling](LOCAL-DISCOVERY-SCHEDULING.md): the per-turn budget is
   soft, total memory and individual filesystem-call latency remain unbounded,
   and equal-fold filename ties may differ from the old unstable Qt sort.

All four items passed final integrated verification: a fresh Release build,
210/210 registered tests with no failures/skips, production protocol/indexing
smokes and rebuilt-application first playback/decode. Exact inputs, intentional
differences and qualification limits are recorded in the
[resumed checkpoint](RESUMED-CHECKPOINT-2026-11-13.md).

## Concrete residuals and deliberately different behavior

- **Windows output is missing, not merely untested.**
  [ProcessDriver](../../native/colosseum_server_v1/src/media/ProcessDriver.cpp)
  rejects `backpressuredOutput` on non-Unix before launching. Legacy media and
  casting consumers request this mode. Implement a genuinely bounded Windows
  pipe path; a QProcess buffering fallback is not equivalent. The relevant
  [M07 runtime tests](../../native/colosseum_server_v1/tests/cmake/M07.cmake) and
  [casting media tests](../../native/colosseum_server_v1/tests/cmake/Casting.cmake)
  currently run only on Unix.
- **Addon OPTIONS mismatch is reproduced.** With no Origin header, native
  `/local-addon/manifest.json` OPTIONS returns 200 and body `GET`; actual pinned
  M1022/M1023 under Node22.16.0 returns 204 and an empty body with SDK CORS headers.
  Native [addon routes](../../native/colosseum_server_v1/src/local/LocalAddon.cpp)
  omit the M1047 prefix middleware. Review CORS scope before changing it.
- **Source failures are not completion tasks to imitate blindly.** Cold
  `waitForSubs` throws in the pinned source; native uses available subtitles.
  Archive bounds, cancellation and valid HTTP framing deliberately replace source
  hangs/overreads. Private profiling ownership deliberately repairs interference
  with first playback. See the linked contracts for exact differences.
- **Permission boundaries remain.** Secure transport and active casting prototypes
  are not integrated; their staged artifacts are excluded from this publication.
  Device control, executable launch and device trust remain separate approval
  boundaries. A remote receiver cannot
  fetch a loopback media URL. No network exposure, certificate bypass, credential
  persistence or implicit personal-file/provider access is authorized by this ledger.

## Remaining sequence

1. Keep the resumed checkpoint's focused, aggregate and actual-application
   regressions when changing subsequent surfaces. Preserve the documented
   source/runtime differences and qualification limits.
2. Implement Windows bounded output and portable process fixtures. Run real
   Windows Qt/MSVC tests for handle inheritance, bounded reads, pause/resume,
   child-exit/EOF ordering, cancellation, teardown and media decode.
3. Close remaining archive/NZB concurrency and split-payload coverage; qualify
   addon SDK/profile differences without
   expanding roots, provider use or security settings.
4. Resolve the explicitly held TLS/casting/listener scope before integration.
   Active LAN/device success requires authorized exposure/trust and real devices.
5. Obtain external qualification: actual public-peer bytes, real GPUs, required
   platform/profile packaging, and authoritative ffsplit/Stremio-runtime inputs
   where available. Preserve blockers when an input or environment is unavailable.
6. Reconcile the final surface ledger with those results. A release or literal
   full-parity claim requires more than a successful default Linux test suite;
   documented intentional differences must remain explicit.

Historical [STATE](STATE.json), [runtime surface matrix](RUNTIME-SURFACE-MATRIX.json)
and [native capability matrix](NATIVE-CAPABILITY-MATRIX.json) retain their original
scope. In particular, the capability matrix proves a pinned libtorrent control
seam, not full server completeness; historical source-profile labels are not
current native runtime observations.
