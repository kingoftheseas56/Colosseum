# M03 receiving receipt

Implemented and isolated-verified. Not a declaration of full-server acceptance.

## Source mapping

Pinned oracle SHA-256: 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.

- 440: MP4/Matroska sample dispatch represented by two explicit native APIs.
- 877: MP4 track indexes, edit lists, composition offsets, decode timestamps, average sample durations and sync-sample flags. Preserve physical track index, unsigned version-1 edit mediaTime, source DTS propagation and missing-stss all-key behavior.
- 900/911: MP4 stream opens use Range bytes=offset-, at most four seeks, and continue reading an incomplete moov from the same stream. Header parsing accepts 64-bit box lengths, while the separate mux.js-compatible moov inspection intentionally retains its 32-bit inspection behavior.
- 868/870: SeekHead-selected Info/Tracks/Cues, sorted cue times, only the first CueTrackPositions, timecode-scale conversion and optional default duration. Initial HTTP request omits Range; source skip threshold is 32000 bytes. Partial EOF preserves already available track metadata. Empty present Cues errors, absent Cues yields empty samples, and oversized eight-byte VINT rejects like the source.
- 918: source keyframe grouping, short-video extra segments, audio numbering and prediction handoff. Prediction itself is M04-owned.
- 934: all original bundled codec fixture metadata and base64 payloads retained exactly.

The source sample results contain timing, not sample byte offsets. No invented byte-offset table was added.

## Verification

- Isolated target /tmp/m03-build/test_samples builds successfully with Qt 6.11.1 Core/Network; no shared build touched.
- 40/40 exact factory comparisons pass: generated H264/AAC fast-start/tail/fragmented MP4, generated MKV, filtered tracks, missing boxes, signed composition times, empty sync tables, large moov, sorted/before/after cues, missing scale/duration, out-of-file seek, truncated containers, segment boundaries, nullable duration and original bundled codec payloads.
- 11/11 controlled HTTP result-and-range comparisons pass. Virtual 4GiB mdat issues the identical second seek at 4294967424 and reads only 7846 bytes with the controlled 4096-byte first chunk.
- 2/2 cancellation checks pass with delayed HTTP responses. Deleting the returned QObject suppresses completion and destroys pending I/O.
- 3 bounded fallback checks pass. Two are documented deviations, not exact parity: the oracle leaves empty/absent-moov promises unsettled at EOF; the candidate returns unavailable/max seeks exceeded after four opens. Receiving approval is required for this liveness choice.

Additional resource limits are explicit deviations: metadata 256MiB, each expanded sample table 10 million entries, and selected EBML traversal 1 million iterations. First-read chunk boundaries are transport-dependent in both implementations; exact range claims apply to the controlled corpus, not arbitrary network packetization.

## Reproduce and integrate

Source/test inventory and frozen hashes: candidate-hashes.json. Machine receipt: receipt.json. Inputs, fixture hashes, raw oracle output, candidate comparisons, HTTP requests and logs are retained alongside this file. Initial red build is test-first.log.

From native/colosseum_server_v1, run each of tests/run_samples_differential.py, tests/run_samples_ranges.py, and tests/run_samples_fallback.py with:

    --candidate /tmp/m03-build/test_samples --oracle <pinned server.js> --out <artifacts/server1/M03>

Integration snippet: tests/cmake/M03.cmake. Source dependencies: Qt6::Core and Qt6::Network only. Keep unavailable results distinct from available-but-empty timing and retain converter fallback rather than selecting a new codec/player. The parent/integration worker owns shared build wiring and full predecessor/converter regressions. No commits, pushes, merges or production integration were performed here.
