# W4-A Handoff - Playback Window + Recording-Time Model

AGENT: Agent-1

TASK: T-1 / W4-A Playback Window + Recording-Time Model

STATE: DONE - locally compiled, tested, and real-libmpv verified as a Preflight reference prototype. Production Colosseum adoption is not claimed.

COMPLETED:
- Added validated optional `PlaybackWindow { startMs, endMs? }` to `PlaybackLoadInput`.
- Added `PlaybackTimeTransform` separating physical-media time, track-relative playback time, and optional recording-relative time.
- Canonical BA2-B seek/position/duration surfaces now use integer milliseconds; frozen BA2-A double-seconds storage is contained behind compatibility overloads.
- Added `PlaybackSeekCommand` so the exact BA2 seek identity and integer-ms logical target travel together.
- Extended `ResolvedTrackSource` with optional playback window, optional signed `recordingTimeOffsetMs`, optional resolution timestamp, and optional expiry timestamp.
- Added strict freshness validation: known expiry requires known non-negative resolution time and `expiresAt > resolvedAt`.
- Added direct BB3 exact-CUE-slice -> bounded `ResolvedTrackSource` -> `PlaybackLoadInput` handoff without changing BB3.
- Added concrete mpv time state: load window -> per-file `start/end`, logical seek -> physical seek, physical observation -> track-relative ms, bounded physical duration -> logical duration.
- Recording-relative resume/source-switch conversion is invertible when offsets are known; unknown offset remains unknown.
- W4-A contains no source ranking, provider selection, retry, or fallback policy.

VERIFIED:
- BA2-B configure/build: PASS.
- BA2-B CTest: 1/1 PASS.
- BB4 configure/build: PASS.
- BB4 CTest: 3/3 PASS, including `vinyl_w4a_cue_window_handoff` and real-libmpv local resource load.
- BA2-D configure/build: PASS.
- BA2-D CTest: 5/5 PASS.
- Real `vinyl_w4a_bounded_mpv_probe`: PASS against read-only `tests/lanista_capture/assets/Colosseum.mp4`; bounded load starts at its physical start, exact seeks are offset, observations are track-relative, and END_FILE arrives at the logical bound.
- Existing raw playlist-entry and seek-completion probes remain green.
- BA2-C compatibility build: PASS; direct coordinator harness exits 0. Its CTest wrapper still lacks Qt PATH and therefore launches with historical Windows 0xc0000135 unless Qt is injected externally; no Slice 2 file was changed.
- Prohibited source-policy scan across W4-A source: zero ranking/fallback hits.
- Production Colosseum `native/player/mpvitem.h`, `native/player/mpvitem.cpp`, and read-only fixtures show no Git changes.

CHANGED:
- `BA2B-ENGINE-ADAPTER-CONTRACT.md`
- `ba2-engine-adapter-prototype/PlaybackEngineAdapter.h`
- `ba2-engine-adapter-prototype/PlaybackEngineAdapter.cpp`
- `ba2-engine-adapter-prototype/test_playback_engine_adapter.cpp`
- `ba2-engine-adapter-prototype/CMakeLists.txt`
- `BA2D-MPV-ADOPTION-CONTRACT.md`
- `ba2d-mpv-adoption-prototype/ColosseumMpvBridge.h`
- `ba2d-mpv-adoption-prototype/ColosseumMpvBridge.cpp`
- `ba2d-mpv-adoption-prototype/test_colosseum_mpv_bridge.cpp`
- `ba2d-mpv-adoption-prototype/mpv_bounded_window_probe.cpp`
- `ba2d-mpv-adoption-prototype/CMakeLists.txt`
- `BB4-MPV-PLAYBACK-ADAPTER-CONTRACT.md`
- `bb4-mpv-playback-adapter-prototype/ResolvedPlaybackSource.h`
- `bb4-mpv-playback-adapter-prototype/ResolvedPlaybackSource.cpp`
- `bb4-mpv-playback-adapter-prototype/test_mpv_playback_adapter.cpp`
- `bb4-mpv-playback-adapter-prototype/test_cue_window_handoff.cpp`
- `bb4-mpv-playback-adapter-prototype/CMakeLists.txt`
- `WAVE4-AGENT-ROOM.md` append-only claim/public-interface/final messages
- this handoff.

UNRESOLVED:
- No W4-A acceptance blocker remains.
- Source selection, ordered fallback, expiry-driven re-resolution, and failure recovery are intentionally owned by W4-C, not W4-A.
- Queue-safe same-occurrence restart is owned by W4-B.
- Timed-text consumption of `recordingTimeOffsetMs` is owned by W4-F.
- Production Colosseum integration remains a later adoption state and was not performed.

NEXT:
- Lead may mark T-1 complete after independent review.
- T-3 may consume the posted W4-A interface once T-2 posts its coordinator interface.
- W4-H should reuse the bounded libmpv probe/reference mapping in the composed Play journey.

IMPORTANT EVIDENCE:
- `w4a-build-ba2b`: 1/1 CTest PASS.
- `w4a-build-bb4`: 3/3 CTest PASS.
- `w4a-build-ba2d`: 5/5 CTest PASS.
- `w4a-build-ba2c-compat`: compile/link PASS; coordinator harness direct exit 0 with Qt PATH.
- Public interface checkpoint: `MSG-AGENT-1-PUBLIC-INTERFACE` in `WAVE4-AGENT-ROOM.md`.
