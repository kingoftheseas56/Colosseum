# Player 2 Codex low-quota handoff — A/V regression, Tasks 13-16

**Author:** `[Agent 4 (Codex), player]`  
**Stopped:** 2026-07-24, immediately after the confirmed Full-normalizer PTS fix reached GREEN  
**Reason for stopping:** Hemanth reported Codex quota below 10% and requested a natural stop plus a
complete handoff. No commit was made.  
**Workspace:** `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\player2-task1-isolation`  
**Branch:** `agent4/player2-task8-seek`  
**HEAD:** `5d83ac01b7816aae07cac661ea9db066e73d3210`  
**Live app / production:** untouched. Task 17 integration remains forbidden.

## Executive state

The worktree contains one coherent but **uncommitted** Player 2 A/V repair across 14 tracked files.
Three distinct defects were found with evidence and each has a RED-to-GREEN test:

1. **Stale audio-clock generation acceptance.** A render callback could republish an old generation
   as valid after a seek/flush. The clock snapshot now carries generation and DemuxSession accepts
   only the active generation.
2. **Unconditional video drop-oldest feedback loop.** A full video queue was treated as distress even
   when its oldest packet was on time or in the future. It repeatedly jumped to future GOPs and
   collapsed presentation throughput. Admission is now adaptive: on-time fullness backpressures
   interruptibly; only clock-proven lateness permits a whole-GOP drop.
3. **The real ears/eyes A/V mismatch: Full-normalizer PTS units were wrong.**
   `AudioNormalizer::pullOutputs()` treated FFmpeg filter-output PTS as microseconds, but the PTS is
   in the filter sink link time base (for the observed stream, `1/48000`). Internal A/V metrics were
   self-referential because video followed that mislabeled clock. The output PTS is now rescaled to
   microseconds using `av_buffersink_get_time_base()`.

The clean stopping point is important:

- The fresh `player2_audio_normalizer_test.exe` built at 01:06 and passes with the PTS-unit fix.
- `player2_harness.exe` is older (00:40). It **does not contain the PTS-unit fix**.
- Therefore Hemanth has not yet heard/seen the corrected binary.
- Do not commit or claim sync until the harness is relinked, its mtime advances, and Hemanth accepts
  the fresh binary by ears/eyes.

## Authoritative source documents

- Handoff mission:
  `docs/superpowers/specs/2026-07-23-player2-codex-handoff-tasks9-16-and-audio-video.md`
- Sixteen-task plan:
  `docs/superpowers/plans/2026-07-21-colosseum-player2.md`
- AudioWorker corrected contract:
  `docs/superpowers/specs/2026-07-23-colosseum-player2-audioworker-design.md`
  (this file exists in the main Colosseum checkout if absent in the worktree)
- Original Codex design review:
  `docs/superpowers/specs/2026-07-23-player2-audio-paced-3worker-codex-review-request.md`
- Read-ahead design/history:
  `docs/superpowers/specs/2026-07-23-colosseum-player2-audio-readahead-engine-design.md`
- Parity ledger:
  `docs/superpowers/specs/player2-parity-ledger.md`
- Durable SDD state:
  `.superpowers/sdd/progress.md`

## Starting state and baseline

The branch already contained the audio-paced three-worker architecture:

- `b7dc589`: interruptible `PacketQueue` push
- `3c0df40`: `AudioWorker`
- `5d83ac0`: DemuxSession integration

That architecture fixed the original circular deadlock:

`video queue full -> demux blocks -> inline audio starves -> audio clock stalls -> video stops ->
video queue never drains`

At the start of this Codex wake, the following focused baseline was built and passed:

- `player2_playback_metrics_test`
- `player2_packet_queue_test`
- `player2_clock_scheduler_test`
- `player2_audio_pipeline_test`
- `player2_demux_session_test <build>/player2-fixtures`

All five exited 0. The worktree then had no tracked changes and only pre-existing untracked
`artifacts/`.

## Finding 1 — stale/mis-epoched sink clock

### Root cause

Before this patch, `AudioClockSnapshot` contained only:

```cpp
qint64 mediaPositionUs;
qint64 qpcTimestamp;
bool valid;
```

`WASAPIAudioSink::flush()` advanced the sink generation and invalidated the clock, but the render
callback sampled audio data and later queried queue metadata/published `valid=true` through separate
operations. An in-flight callback could therefore republish a valid old-generation clock after the
flush. The readiness barrier and `decideClockResync` accepted any `valid` clock.

### RED evidence captured before production edits

- `player2_clock_scheduler_test.exe` exited 1:

  `FAIL: a valid clock from the flushed generation was accepted as the new audio epoch`

- `player2_audio_pipeline_test.exe` exited 1:

  `FAIL: a post-read flush replaced the consumed samples' media position`

The second test deterministically models:

`dequeue generation 7 -> flush generation 8 -> old callback performs separate metadata query`

### Implemented repair

- `AudioClockSnapshot` now carries `generation`.
- `AudioClockSnapshot::isValidForGeneration(expected)` is the acceptance gate.
- `AudioBufferQueue::read()` returns the media position and generation captured atomically with the
  samples actually consumed.
- WASAPI clock publication is one mutex-coherent `AudioClockSnapshot`, not three independent atomics.
- Flush/pause/underrun invalidation stamps the current generation.
- Demux readiness/resync ignores a valid clock from any non-active generation.
- Player2 diagnostics expose the new coherent snapshot without changing production routing.

### GREEN evidence

Both direct tests passed after the patch:

- `player2_clock_scheduler_test: PASS`
- `player2_audio_pipeline_test: PASS`

### What the first bounded soak proved

Artifact:

`artifacts/player2/priority_av_clock_bounded_full_20260723.json`

Key values:

- `avDriftMeanMs = -2.1097`
- `avP95Ms = 6.683`
- `avDriftMaxAbsMs = 31.864`
- audio queue held roughly `1.9-2.0 s`
- but video windows collapsed:
  `21.03 -> 1.30 -> 2.60 -> 1.10 -> 0.44 fps`

Conclusion: epoch correctness fixed a real race, but did not fix the throughput collapse. It exposed
the second defect rather than completing the task.

## Finding 2 — unconditional video drop-oldest was a positive-feedback loop

### Root cause

The video queue was configured with `dropOldestWhenFull=true`. A full queue is not necessarily late;
with read-ahead it may contain perfectly on-time or future frames. The old admission path nevertheless
dropped the oldest whole GOP whenever the queue was full.

The consumer then:

1. received a discontinuity,
2. flushed the video decoder,
3. restarted at a future keyframe,
4. waited for the audio clock to catch that future PTS,
5. while the producer continued filling and dropping again.

This explains why larger audio read-ahead made the failure worse. The queue was pushed into its full
state more consistently; queue depth was a trigger, not proof of lateness.

### RED evidence

`player2_packet_queue_test.exe` exited 1:

`FAIL: on-time worker-fed video overflow dropped forward instead of backpressuring`

The test distinguishes:

- full + on-time/future backlog -> preserve order and backpressure interruptibly
- full + active-generation clock proves oldest video is late -> whole-GOP recovery is allowed

### Implemented repair

- Video admission now uses `pushInterruptible`.
- `PacketQueue` exposes the oldest queued PTS for policy decisions.
- A pure helper, `shouldDropVideoBacklog(...)`, accepts only:
  - a valid audio snapshot,
  - from the active generation,
  - where the oldest video PTS is beyond the scheduler's late threshold.
- Full but on-time video now waits for capacity without discarding presentation order.
- Seek/pause/track commands and cancel can interrupt both the audio and video queue waits.
- DemuxSession now holds a guarded `m_videoQueueForInterrupt` alongside the audio queue handle.
- Genuine late backlog still drops a whole leading GOP and signals decoder discontinuity.

### GREEN evidence

The direct adaptive-policy suites passed after reconciliation:

- `player2_packet_queue_test: PASS`
- `player2_clock_scheduler_test: PASS`

The implementer also reported a fresh five-suite pass before the later normalizer PTS edit. Treat that
as useful evidence, but rerun every suite from the final dirty tree before committing.

### Short artifact after adaptive admission

Artifact:

`artifacts/player2/priority_av_clock_adaptive_bounded_full_20260724.json`

This 15-second artifact showed:

- `sustainedFps = 21.14`
- windows `21.03`, `21.42`
- audio queue about `1.9-2.0 s`
- no scheduled late drops
- internal p95 drift `7.276 ms`

However it is **not acceptance evidence**:

- it was only 15 seconds,
- it was produced amid an overlapping/delayed harness run,
- it predates the Full-normalizer PTS fix,
- Hemanth explicitly reported that audio and video were still not in sync.

Keep the artifact as proof that adaptive admission removed the catastrophic sub-1-fps feedback loop,
not as proof of A/V correctness.

## Finding 3 — Full-normalizer output PTS was in the wrong unit

### Why this finding mattered

After the adaptive queue restored throughput, Hemanth watched/listened and said:

> "the audio and video are still not in-sync"

That observation overrides the JSON. The near-zero internal drift was comparing video against the
same incorrectly labeled sink clock, so it could be numerically healthy and perceptually wrong.

### Root cause

`AudioNormalizer::pullOutputs()` copied `AVFrame::pts` directly into `AudioBuffer::ptsUs`.

For Full normalization, FFmpeg produced output such as:

`pts:4784 pts_time:0.0996667`

The raw value `4784` is in the filter sink link time base (`1/48000` here), not microseconds. The code
therefore labeled approximately `99.667 ms` of audible media as `4.784 ms`.

That mislabeled timestamp flowed into:

`AudioNormalizer -> AudioPipeline -> WASAPI queue -> AudioClockSnapshot -> PlaybackClock -> video`

Video faithfully followed a biased audio clock. The drift instrumentation used that same clock and
therefore concealed the constant real-world offset.

### RED evidence

The new Full-normalizer test failed before the production edit:

`player2_audio_normalizer_test.exe` exited 1:

`FAIL: full output PTS used the filter sample time-base instead of microseconds`

The test requires consecutive Full-output PTS delta to equal the first output buffer's duration in
microseconds.

### Minimal production repair

`AudioNormalizer.cpp` now converts output PTS using the filter sink's actual time base:

```cpp
av_rescale_q(frame->pts,
             av_buffersink_get_time_base(m_sink),
             AVRational{1, 1'000'000})
```

Only six production lines changed for this defect.

### GREEN evidence at the stop point

Fresh direct execution:

`player2_audio_normalizer_test: PASS`  
exit code `0`

Binary mtimes at stop:

- `player2_audio_normalizer_test.exe`: **2026-07-24 01:06:11**
- `player2_harness.exe`: **2026-07-24 00:40:59**

The harness is older and does not contain this fix. This is why the correct next action is a relink
and fresh ears/eyes launch, not a commit.

## Exact dirty tracked files

Do not discard or reset these. They are the work product:

```text
native/player2/audio/AudioNormalizer.cpp
native/player2/audio/WASAPIAudioSink.cpp
native/player2/audio/WASAPIAudioSink.h
native/player2/core/DemuxSession.cpp
native/player2/core/DemuxSession.h
native/player2/core/PacketQueue.cpp
native/player2/core/PacketQueue.h
native/player2/core/PlaybackClock.cpp
native/player2/core/PlaybackClock.h
native/player2/core/Player2Session.cpp
tests/player2/player2_audio_normalizer_test.cpp
tests/player2/player2_audio_pipeline_test.cpp
tests/player2/player2_clock_scheduler_test.cpp
tests/player2/player2_packet_queue_test.cpp
```

Diff size at stop:

`14 files changed, 543 insertions, 83 deletions`

Untracked, intentionally preserved:

- `.superpowers/` — task brief + SDD ledger
- `artifacts/` — prior and current diagnostic reports

`git diff --check` produced no whitespace errors at the stop point.

## Concurrency/process warning from this wake

The first A/V implementer spawned a read-only trace worker. Hemanth's "keep going" steer interrupted
that agent tree while a delayed write was still arriving. The continuation implementer saw portions
of the new tests and adaptive helpers change underneath it.

The continuation implementer then:

- waited for two stable diff snapshots,
- inspected all touched hunks,
- reconciled duplicate/partial test scaffolding,
- rebuilt the direct queue and clock tests,
- reported the 12-file epoch/adaptive diff coherent before the normalizer fix was added.

There is no other live subagent now. Nevertheless, the next wake must independently review the whole
14-file diff because delayed cross-agent writes touched a catastrophic concurrency seam.

Do not run multiple Player 2 implementers in this worktree at once.

## Build-lane warning

The normalizer RED build was delayed by an unrelated `connection-concierge` full native build using
many `cl.exe` processes. We waited rather than colliding with the shared build lane.

Before rebuilding:

1. Confirm no unrelated `cl.exe`, `ninja.exe`, or `cmake.exe` build owns the relevant output.
2. Kill only `player2_harness.exe` if it is running; it locks the executable and causes LNK1104.
3. Use the existing lab build directory `native/build-player2`.

The helper prints a harmless `vswhere.exe` warning on this machine but successfully builds with the
already available toolchain.

## Next-wake execution order

### 1. Ground and inspect

```powershell
$wt = 'C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\player2-task1-isolation'
git -C $wt status --short
git -C $wt diff --check
git -C $wt diff --stat
```

Read this handoff, `.superpowers/sdd/progress.md`, and the complete 14-file diff before editing.

### 2. Relink every affected test and the harness

Kill a stale harness first:

```powershell
Get-Process player2_harness -ErrorAction SilentlyContinue | Stop-Process -Force
```

Build:

```text
cmd /c C:\Users\Suprabha\AppData\Local\Temp\p2build.bat ^
  player2_audio_normalizer_test ^
  player2_audio_pipeline_test ^
  player2_clock_scheduler_test ^
  player2_packet_queue_test ^
  player2_playback_metrics_test ^
  player2_demux_session_test ^
  player2_harness
```

Verify that `player2_harness.exe` mtime advances beyond 01:06:11.

### 3. Run the complete focused suite from the final dirty tree

Runtime environment:

```powershell
$env:PATH =
  'C:\Qt\6.11.1\msvc2022_64\bin;' +
  'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;' +
  $env:PATH
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
```

Required tests:

```text
player2_audio_normalizer_test
player2_audio_pipeline_test
player2_clock_scheduler_test
player2_packet_queue_test
player2_playback_metrics_test
player2_demux_session_test <native/build-player2/player2-fixtures>
```

Also run any existing seek-generation/read-ahead tests included by the current Player 2 suite.

### 4. Fresh Hemanth ears/eyes gate — before the 100-second report

Use the newly linked binary and the exact clip:

```text
C:\Users\Suprabha\Downloads\Colosseum\The Wire - S4E10 - Misgivings - 20260720_175049.mp4
```

Launch visible playback with Full normalization and no auto-exit report mode. Hemanth judges:

- dialogue lip-sync,
- whether sound or picture leads,
- stutter/fracture,
- sustained smoothness after loudnorm preroll.

Do not accept JSON in place of this.

### 5. Only if ears/eyes passes, run the 100-second Full soak

Record:

- `sustainedFps`
- every `windowFps`
- `minAudioQueueMs`
- `finalAudioQueueMs`
- `audioUnderruns`
- `avDriftMeanMs`
- `avP95Ms`
- `avDriftMaxAbsMs`
- queue/GOP drop counters if available

Expected shape:

- video near source cadence (approximately 24 fps after startup),
- no late-window collapse,
- audio queue remains healthy,
- internal drift near zero,
- Hemanth still hears/sees sync.

If eyes/ears still fails after the PTS rescale, do not add a manual offset. Instrument:

- input decoded PTS,
- filter sink time base,
- rescaled normalized output PTS,
- audio buffer PTS entering the software queue,
- current WASAPI padding,
- `IAudioClock::GetPosition` and its QPC timestamp,
- the published `AudioClockSnapshot`,
- presented video PTS.

Find the first boundary where real media time diverges.

### 6. Review before commit

This is concurrency/timing work and delayed writes occurred. Obtain a fresh cross-substrate review of
the full diff against:

- generation safety,
- pause/resume and command interruptibility,
- seek-under-full-queue,
- cancel-under-full-queue,
- whole-GOP recovery only under true lateness,
- EOF/audio-tail behavior,
- clock math and PTS time-base conversion.

### 7. Commit only after all gates

Suggested message:

```text
[Agent 4 (Codex), player] Fix Player 2 worker-fed clock mastering
```

Stage and commit only the 14 explicit tracked paths. Never stage `.superpowers/` or bulk `artifacts/`
unless a later plan step explicitly names a frozen artifact.

## Priority A/V Definition of Done

All are mandatory:

- [ ] Full-normalizer PTS test passes from a freshly built executable.
- [ ] Epoch clock tests pass.
- [ ] Adaptive queue tests pass.
- [ ] Five original focused regression suites pass.
- [ ] Pause/resume remains command-responsive under full queues.
- [ ] Seek-under-full-queue passes.
- [ ] Cancel-under-full-queue passes without join hang.
- [ ] Track reconfigure preserves generation safety.
- [ ] EOF drains audible audio and presented video tails.
- [ ] 100-second Full soak holds source cadence with no late-window collapse.
- [ ] Audio buffer stays healthy with low/non-climbing underruns.
- [ ] Internal drift remains near zero.
- [ ] **Hemanth confirms real ears/eyes sync on the freshly relinked binary.**
- [ ] Cross-substrate review approves the complete diff.

Until every box is checked, the branch remains unshippable.

## Tasks 9-16 roadmap status

Git history audit performed this wake:

- **Task 9 — normalization:** already landed at `e9fa50a`; this wake adds the necessary Full-output
  PTS correction, still uncommitted.
- **Task 10 — subtitles/tracks:** landed through `d0149cc`, `084ce36`, review fixes `b76e484`.
- **Task 11 — HTTP transport:** landed at `78a8621`.
- **Task 12 — diagnostics/recovery:** landed at `9cb2259`.
- **Task 13 — shell/parity chrome:** partial:
  - `f47a971`: transport spine
  - `a125dc9`: menus, subtitles, stats
  - `tests/player2/player2_shell_contract.ps1` passed this wake.
- **Task 14:** not started in this Codex handoff.
- **Task 15:** not started.
- **Task 16:** not started.
- **Task 17+:** explicitly out of scope and untouched.

### Task 13 audit from this wake

Present:

- transport/play/pause,
- exact and relative seek,
- volume/mute,
- auto-hide,
- frame step,
- audio/subtitle track menus and delays,
- text subtitle painting,
- fit/fill/aspect/zoom controls,
- Smooth/Light/Full selection,
- stats overlay,
- right-click overflow,
- chapter ticks.

Still open or not accepted:

- playback speed and sleep timer,
- complete loading/buffering/error presentation,
- pause card,
- shortcut sheet/full hotkey registry,
- compact/snug/tight folds,
- buffered cache fraction is not surfaced,
- ASS styling and bitmap subtitle painting,
- chapter labels/menu polish,
- parity ledger eyes-on columns remain `NOT RUN`,
- no row may become PASS without its required automated and eyes-on evidence.

Do not start Task 14 until the priority A/V gate is committed and Task 13 is closed/reviewed.

## Final instruction to the next brother

Do not restart the architecture discussion and do not tune queue sizes. The branch now contains three
evidence-backed corrections:

1. generation-coherent sink clocks,
2. clock-aware adaptive video admission,
3. correct FFmpeg filter-PTS rescaling.

The immediate job is verification, not invention:

**relink -> run all tests -> verify harness mtime -> Hemanth ears/eyes -> 100-second soak -> review ->
explicit-path commit.**

Only then resume Task 13 and continue Tasks 14-16.
