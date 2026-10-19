# M05 local conversion qualification

Implemented exact locked M916 per-track policy and lifecycle, M932 FFmpeg argv, M933 bitrate selection, and M193/M809 profile application. Profiles are deterministic argument data, not a claim that hardware is available.

## Verified

- 227 exact factory differential cases: copy/transcode decisions, missing keyframes, audio rates including nearest-rate ties and clamping, channel caps, all nine hardware profile recipes plus software, seek/fragment offsets, bitrates, embedded/external subtitle filters, blank retry filter.
- Exact M916 lifecycle differential, using the locked real mutex factories and a controlled process/MP4 boundary: initial process replacement, cache/current reuse, displaced work cancellation, one normal-EOF blank retry, failed retry without looping, active destruction cancellation.
- Additional native lifecycle assertions: initialization serialization, sticky init failure, subtitle burn retry/reset, old generation rejection, destroyed init waiters, active/queued conversion destruction, exactly-once completion.
- Real generated H264 input: first converted segment has 4.000000 seconds duration and decodes cleanly. Real PCM input: converted AAC is mono 48 kHz, first segment 4.096000 seconds, decodes cleanly. Media assertions are timeline/decode equivalence, not cross-encoder byte equivalence.
- Linux owned-pipe backpressure: real FFmpeg produces 11,520,000 PCM bytes after resume. Delivery stays fixed while paused; callback chunks and retained diagnostic stdout are at most 65,536 bytes. The actual OS pipe, rather than QProcess's unbounded internal stdout buffer, blocks the producer. Converter holds one current read/segment and its source-defined cache; no unbounded background stdout spool.
- Paused exited child drains before EOF/finish; cancel and owner deletion inside output callback; bounded-process descendant teardown; driver cancellation of an exited-but-paused stream.
- M00 and M01 regressions pass. ASan/UBSan pass with LeakSanitizer disabled. LeakSanitizer itself cannot run under this execution environment's ptrace supervisor; this is not a leak-free claim.

## Explicit boundaries

- Local Linux runtime qualified. Windows opt-in bounded output returns an explicit unsupported error; a Windows native bounded pipe implementation remains required. Existing ordinary ProcessPort users remain unchanged.
- Native TrackConverter adapter rejects nonempty HLSV2_REMOTE explicitly. The injectable TrackConversionBackend is the integration seam, but the existing remote ProcessDriver lacks continuous streaming/backpressure, so remote conversion is not qualified.
- Hardware profile argv are oracle-tested only. No hardware discovery or accelerated runtime claims.
- This is not whole-server or whole-M05 cross-platform acceptance. M06 composition and final integrated acceptance remain with the parent.
- TrackConverter APIs use their owning QObject/event-loop thread, matching the source's single-threaded promise/mutex model.

## Build integration

Add src/media/FfmpegArguments.cpp and src/media/TrackConverter.cpp to the media target. They consume M03 KeyframeIndex and M04 SegmentLayout, Mp4Timeline, HlsPlaylist, VttTimeline plus existing ProcessDriver (Qt Core/Network).

Tests:
- test_track_converter.cpp takes tests/fixtures/track-converter.json.
- test_track_converter_lifecycle.cpp accepts an optional generated MP4/WAV path and emits the native lifecycle JSON on a single line. Compare that JSON with tests/fixtures/track-converter-lifecycle.json.
- test_track_converter_backpressure.cpp exercises the Linux owned-pipe lane.

Oracle generators take the exact server.js path then output JSON path. Their bundle hash is checked before loading factories. raw/isolated-CMakeLists.txt records the actual independent qualification build; paths there describe this lane, not production configuration.

No commits, pushes, production merges, or shared CMake edits were performed by this worker.
