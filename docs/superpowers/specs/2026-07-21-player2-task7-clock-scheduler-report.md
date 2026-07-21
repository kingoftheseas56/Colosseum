# Player 2 Task 7 Clock and Scheduler Report

Date: 2026-07-21

Owner: [Agent 4 (Codex), player]

Production integration state: isolated; `COLOSSEUM_BUILD_PLAYER2=OFF`

## Result

Task 7 passes. Player 2 now uses the WASAPI audio clock as the playback master whenever an audio track is decoded, including while muted, and falls back to a QueryPerformanceCounter clock for video-only input. Video decode waits and bounded late-drop decisions run on the demux worker; the GUI and Qt Quick render threads never sleep for scheduling policy.

The deterministic 30-minute accelerated simulation passed with p95 absolute clock error below 40 ms and no monotonic drift. The final real-media gate passed for 600.04 uninterrupted seconds on the target laptop.

## Final Wire Gate

Fixture: `The Wire - S4E13 - Final Grades - 20260720_211141.mp4`

Hardware: Intel i5-8365U, Intel UHD Graphics 620, Windows 11

Harness: committed `player2_harness.exe`, `--soak-seconds 600`

| Metric | Result |
|---|---:|
| Elapsed | 600.04 s |
| Decode path | d3d11va |
| Input format | P010 |
| Frames generated | 14,241 |
| Frames presented | 14,232 |
| Scheduler late drops | 0 |
| Texture-ring producer starvation | 1 |
| A/V scheduling p95 | 2.497 ms |
| Audio underruns | 5 |
| Maximum audio queue | 1,030.667 ms |
| Final audio queue | 524.667 ms |
| CPU video transfers | 0 |
| D3D device errors | 0 |
| Adapter match/shared fences | true/true |
| Final state | Idle |

The A/V sample is taken after any scheduler deadline wait, once per video scheduling decision. It is the error between the video PTS and the extrapolated WASAPI-derived media clock at submission. It is not a display-photon measurement. An earlier implementation sampled before the deadline wait and incorrectly reported roughly 44 ms; that measurement was rejected and is not promotion evidence.

The five audio underruns were bounded startup/endpoint events. The queue remained under its two-second hard cap and did not grow monotonically. FFmpeg emitted non-fatal probe warnings for PGS subtitle streams with unspecified dimensions; subtitle decode is outside Task 7.

## Architecture Delivered

- `PlaybackClock` maintains a QPC/media epoch, playback rate, pause/resume state and bounded drift correction.
- `FrameScheduler` deterministically returns Present, DropLate, RepeatCurrent or WaitUntilQpc with a configurable early tolerance, late threshold and consecutive-drop ceiling.
- `DemuxSession` replaces the former video-only `steady_clock::sleep_until(PTS)` pacing with audio-master/QPC scheduling on its worker thread.
- `WASAPIAudioSink` maps the first sample written behind current endpoint padding to a media position and pairs it with a QPC timestamp.
- `D3D11VideoPipeline` records one post-wait A/V sample per scheduling decision and exposes per-generation p95 and late-drop diagnostics.

## Known Boundaries for the Next Task

- Task 8 must make seek, flush, EOS and track switching share one generation barrier. Do not create a second timing epoch.
- Actual pause currently changes session state but does not yet suspend decode or the WASAPI endpoint. Task 8 must make the existing clock pause/resume contract operational as part of transport safety.
- The current single demux worker is acceptable for this gate, but audio backpressure can still delay packet traversal. The audio-master scheduler prevents burst presentation; future work should preserve that invariant if decode workers are separated.
- A/V p95 is scheduler error, not PresentMon display latency. Task 16 still owns external present/power instrumentation.
- Cross-substrate timing review was unavailable in this Codex wake and remains an explicit Claude review item.
