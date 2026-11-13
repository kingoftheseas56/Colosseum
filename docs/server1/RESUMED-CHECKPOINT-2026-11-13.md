# Native server resumed-work checkpoint

This development checkpoint adds owned default profiler samples, shared archive
preparation, source-compatible catalog advertisement and responsive explicit local
discovery. It does not enable secure transport, active casting, automatic media
scanning, public listener exposure or a release. Full server.js parity remains
incomplete; see the [current surface ledger](CURRENT-COMPLETION-STATUS.md).

## Integrated behavior

- Default sample routes serve original generated black frames and silence. The
  codec/container/channel assumptions are retained, while payload bytes and audio
  durations deliberately differ from M934. Upstream media is not bundled.
  Explicit sample directories retain pinned checks and never silently fall back.
  See [sample provenance and tests](GENERATED-PROFILER-SAMPLES.md).
- Each archive family admits ten active preparations and coalesces active tasks
  by query key. Per-client headers, readers, cursors and cancellation remain
  independent. ZIP directory selection releases its slot before local-header
  reads. Cache identities and saturated duplicate behavior follow the source;
  cancellation, reentrancy and independent gzip replay deliberately avoid source
  lifecycle failures. See [archive contract](ARCHIVE-PREPARATION-PARITY.md).
- The local addon snapshots saved `localAddonEnabled` JavaScript truthiness at
  startup. A settings POST persists immediately but changes the manifest after
  restart. Roots and providers remain separately explicit. Twenty production-host
  cases cover absent/falsy/truthy settings and persisted restart behavior. The
  prior binary fails this regression. See [runtime integration](LOCAL-RUNTIME-INTEGRATION.md).
- Explicit-root enumeration yields between bounded work turns and can be cancelled
  or destroyed during discovery. Existing filters, depth and accepted-entry limits
  remain. Shared callable ownership preserves mutable provider state and survives
  synchronous owner deletion during indexing and metadata callbacks. The previous
  implementation fails deterministic responsiveness and cancellation controls.
  See [discovery contract and limits](LOCAL-DISCOVERY-SCHEDULING.md).

## Integrated qualification

A fresh independent **337-step Release build** passed **210/210 registered tests**,
with no failures or skips, in **187.16 seconds**. The oracle was official
Node22.16.0 and native statistics used libuv1.49.2. Production remains C/C++.

- Frozen 2,698-input manifest SHA256:
  `1f197e948fd304ae0bd5232249c44bf0f8febfb8fda607ae60e8e2caba7122aa`.
- Aggregate CTest log SHA256:
  `5d3a74d8a1a42d15480d7f31de9c2c0a39e1aaa57ff9519dd644cd9f215b67b9`.
- Tested standalone host SHA256:
  `5d4802694a94a58422bcd62ec8b5954d369ff1e48f0649e3522746aeb4c1baf2`.

Six critical tests then passed three consecutive runs each: controlled torrent
HLS, production media composition, archive preparation, responsive discovery,
startup catalog settings and profiler/playback isolation.

Production-host smoke checks also passed FTP ranges, NNTP-to-HTTP and
NNTP-to-ZIP-to-HTTP delivery, generated sample hashes, explicit synthetic indexing,
database-only reload, uncached metadata without discovery and pending-request
shutdown. These use synthetic data and loopback services, not user media or
public-provider credentials.

The actual application was rebuilt and run offscreen with a disposable profile.
First playback, HLS initialization/segment retrieval and independent FFmpeg decode
passed before requesting device information. All three default sample routes
returned the expected MIME types and SHA256 values. Hardware result was false;
no physical GPU success is claimed. Application binary SHA256:
`cd8cbdb1a80630e0d05ec0d68c5fee12332c4f4d3c1d2abb9e1898980a35cae6`.

Independent review and focused ASan/UBSan runs passed archive and local-addon
ownership tests. The final archive harness performed 2,751 checks. External
Qt/zlib/liblzma were not rebuilt with sanitizers, and LeakSanitizer could not run
under the traced execution environment. This is not whole-server sanitizer,
leak, Windows, interactive UI or device qualification.

## Remaining boundaries

- FTPS/NNTPS and active casting drafts remain isolated and unpublished. HTTPS
  listener/certificate workflow, LAN exposure and device trust are not enabled.
- Windows bounded process output is absent. Full TV/Unity/iOS/Android profile
  composition, SDK OPTIONS parity and additional FTP/NZB timing/split coverage
  remain work, as detailed in the ledger.
- Public torrent peer-byte transfer, physical GPUs/casting devices, authoritative
  ffsplit behavior and the intended Stremio-runtime profile remain unqualified.
- Discovery still uses linear candidate-path memory. A single filesystem call can
  exceed the soft time budget, and equal-fold filename ties now use iterator order
  rather than Qt's unspecified unstable tie permutation.
- The prior branch's desktop CI stopped Windows before compilation at libmpv
  preparation, and reproduced an unrelated existing background-coordinator
  lost-wakeup failure on Linux. Neither desktop issue was changed in this server
  checkpoint. New-head CI must be checked separately from these local results.

The frozen manifest records tested input documents. This checkpoint report and
final ledger status were written after qualification; production/test code was
unchanged. Earlier checkpoints retain their historical sample and scope statements.
