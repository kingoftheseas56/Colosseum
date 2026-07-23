# Codex handoff — Player 2: finish the roadmap + fix the audio/video read-ahead regression

> Hemanth's call (2026-07-23): hand the remaining Player 2 video development — the rest of the initial
> 16-task plan plus the current audio/video issues — to Codex, on his remaining Codex quota. When the
> 16 tasks are done and the audio/video is solid, control returns to Claude/Agent 4 for **Task 17+
> (production integration)**. Paste this doc (and follow its links) into a fresh Codex session, high
> reasoning. Everything is on branch `agent4/player2-task8-seek` (backup `agent4/player2-readahead`)
> in worktree `Colosseum/.worktrees/player2-task1-isolation`. **Live app untouched; branch-only.**

## Mission
1. **PRIORITY — fix the read-ahead audio/video regression** (below). It blocks a shippable Player 2.
2. **Complete the remaining Player 2 tasks** in the plan `docs/superpowers/plans/2026-07-21-colosseum-player2.md`
   (16-task plan; Tasks 1-8 done; audit git log for the true high-water mark before assuming a task
   is open). Do NOT cross into Task 17 (production integration / linking `player2_core` into
   `colosseum`) — that returns to Agent 4.
3. Keep the **Global Constraints** in that plan (production must not link `player2_core` before Task 17;
   post additive `native/CMakeLists.txt` lines to Brotherhood `agents/chat.md`; commit by explicit
   pathspec; cross-substrate review before merges).

## PRIORITY BUG — read-ahead: deadlock FIXED, video now runs ahead of the audio clock

### What was wrong originally (confirmed by Codex's own REQUEST-CHANGES)
The read-ahead engine deadlocked: video consumed slightly below real time → its 8 s video `PacketQueue`
filled → the demux blocked pushing video → and audio was decoded INLINE on the demux thread, so audio
starved, its clock stalled, video paced to the stalled clock, the queue never drained. Freeze at ~60 s,
1853 underruns (100 s Full-EBU soak, The Wire S4E10). The "17 fps GPU wall" was refuted as a benchmark
artifact (the honest benchmark, committed `0d7bb96`, anchors fps at the first valid audio clock).

### What is now FIXED (committed on the branch)
The audio-paced 3-worker is built and integrated (commits `b8dc654` drop-oldest primitive · `e628c02`
underrun-clock · `b7dc589` interruptible push · `3c0df40` AudioWorker class · `5d83ac0` DemuxSession
integration). Audio decode runs ENTIRELY on `AudioWorker` (`native/player2/core/AudioWorker.{h,cpp}`);
the demux only reads + routes and paces itself by blocking (`PacketQueue::pushInterruptible`) on a small
audio queue. A Pause/Seek/Cancel interrupts a blocked push (`m_audioQueueForInterrupt`), so the command
loop is never trapped. Seek = generation flush in the worker; track switch = posted reconfigure applied
at the generation boundary; normalization = polled; EOF = `setEndOfStream` + worker drains its tail. The
demux NEVER touches the audio decoder. **Measured: audio buffer healthy the whole 100 s (~1.9-2.0 s),
underruns down to 30-76, NO freeze. The circular deadlock is structurally solved. Unit tests green
(`player2_demux_session_test`, `player2_audio_pipeline_test`).**

### The REMAINING bug — video runs SYSTEMATICALLY AHEAD of the audio clock
Ears/eyes-on: video stutters extremely. Metrics (honest benchmark, `windowFps` + `avDrift*`):
- audio-queue 1.5 s → `decodedFps` ~6, `avDriftMean` +40 ms, `avDriftMax` +1502 ms, 76 underruns.
- audio-queue 4.0 s → `decodedFps` ~0.3, `avDriftMean` +2534 ms, `avDriftMax` +3722 ms, 30 underruns.

`avDrift = videoPts - audioMasterUs` (positive = video ahead). **Enlarging the read-ahead pushed video
FURTHER ahead and lowered fps** — so this is NOT packet starvation and NOT queue sizing (both proven by
measurement). Video is decoded/presented far ahead of the audio-master clock, so the `FrameScheduler`
makes it `WaitUntilQpc` for seconds per frame → decode+present (one loop) collapses to <1 fps. The lead
scales with read-ahead depth, which points at the **clock reference being wrong**, i.e. a clock-mastering
regression introduced by moving audio decode to the worker.

### Where to look (ranked)
1. **Video-thread clock seeding / readiness barrier** vs the worker-fed sink clock:
   `native/player2/core/DemuxSession.cpp` video thread `drainDecodedFrames` (~`:600-780`), the
   audio-master readiness barrier (holds video until the first valid sink clock, then hard-resets), and
   the `audioPathEpoch` / `audioMasterResync` handshake. The barrier was tuned when audio decode was
   INLINE; the worker changes WHEN the first valid clock appears and how far the demux has read ahead.
2. **`AudioClockSnapshot` has no epoch.** The AudioWorker design doc
   (`docs/superpowers/specs/2026-07-23-colosseum-player2-audioworker-design.md`, item 8) calls for a
   generation/epoch on `AudioClockSnapshot` so the readiness barrier + `decideClockResync` accept a
   clock ONLY when its epoch matches the active audio path. Not yet implemented — a strong suspect for
   video pacing to a stale/mis-epoched clock.
3. **`decideClockResync`** (`PlaybackClock.cpp`, committed `e628c02`) hard-resets the video master on
   recovery from an underrun. With 30-76 underruns, repeated hard-resets could be jerking the clock.
4. The A/V read-vs-play offset: audio packets sit in the queue (1.5-4 s) THEN loudnorm (~3 s) THEN sink
   (~2 s) before the clock reflects them; verify the video thread paces to the SINK clock (what is
   audible), not to how far the demux has read.

Do NOT keep tuning the queue size — it is a symptom knob, not the fix. Fix the clock reference.

## Build / test / measure
- Build: `cmd /c C:\Users\Suprabha\AppData\Local\Temp\p2build.bat <targets>` (vcvars + cmake --build
  native/build-player2). **Kill any running `player2_harness.exe` first — it locks the .exe (LNK1104).**
- Tests: PATH needs `C:\Qt\6.11.1\msvc2022_64\bin` + `C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin`
  + `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`; exes in `native/build-player2/`. Green suite:
  `player2_playback_metrics_test`, `player2_packet_queue_test`, `player2_clock_scheduler_test`,
  `player2_demux_session_test <native/build-player2/player2-fixtures>`, `player2_audio_pipeline_test`.
- Soak: `player2_harness.exe --file "<clip>" --report <json> --soak-seconds 100 --normalization full`.
  Real clip: `C:\Users\Suprabha\Downloads\Colosseum\The Wire - S4E10 - Misgivings - ...mp4`. READ
  `sustainedFps` + `windowFps` + `minAudioQueueMs` + `avDrift*` + `audioUnderruns` — NOT `elapsedSeconds`.
  The DoD: audio healthy (underruns low, buffer full) AND video holds ~24 fps with `avDrift` near 0 over
  100 s, then **Hemanth eyes/ears-on** (Qt is uncapturable; the JSON "passed" is not the gate — a report
  read "healthy" while playback was broken before; trust ears/eyes).

## Reference
- Plan: `docs/superpowers/plans/2026-07-21-colosseum-player2.md` (the 16 tasks + gates).
- AudioWorker design (post-review contract): `docs/superpowers/specs/2026-07-23-colosseum-player2-audioworker-design.md`.
- Codex's own design review: `docs/superpowers/specs/2026-07-23-player2-audio-paced-3worker-codex-review-request.md`
  and recap `~/.claude/recaps/agent-0/brother-agent-0-2026-07-23-player2-audio-paced-design-review.md`.
- Read-ahead engine design + measurements: `docs/superpowers/specs/2026-07-23-colosseum-player2-audio-readahead-engine-design.md`.
- Parity ledger (must-match behaviour vs the current mpv player): `docs/superpowers/specs/player2-parity-ledger.md`.
- Doctrine: report metrics ≠ ears/eyes; QML paints / C++ decides; commit by explicit pathspec;
  `player2_core` stays out of production until Task 17.
