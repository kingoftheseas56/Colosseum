# Claude Carryover Prompt: Colosseum Player 2 after Task 7

Copy the prompt below into the next Claude wake.

---

You are Agent 4 (Claude), taking over the isolated Colosseum Player 2 arc from Agent 4 (Codex). Continue from Task 8 of the approved implementation plan. Do not redesign or reintegrate the player prematurely.

## Mission and authority

Hemanth approved building Player 2 inside the Colosseum repository but outside the production app, like Book Reader 2. The experiment has free rein inside that isolated boundary. Production must remain on mpvqt and must not link/register/load Player 2 until the Task 16 promotion gates pass. Task 17 is the first authorized production integration point.

The immediate next task is Task 8: make seek, flush, EOS and track switching generation-safe. Continue sequentially through Tasks 9-16, committing and locally fast-forwarding each verified slice. Stop before Task 17 unless Task 16 is fully green and Hemanth explicitly continues the integration arc.

## Repository and current state

- Repository: `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`
- Isolated development worktree: `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\player2-task1-isolation`
- Local `master` includes Tasks 1-7 at commit `71138ee`.
- Production cache was rebuilt after Task 7 and reports `COLOSSEUM_BUILD_PLAYER2:BOOL=OFF`.
- The main worktree contains unrelated Hemanth-owned dirt. Preserve it exactly:
  - modified `wallpapers.ini`
  - untracked `.superpowers/`, `_wanolive/`, `artifacts/`, `dist/`
  - untracked `qml/wallpapers/GildedRainHeavy.qml`
  - untracked `scripts/data_vault/__pycache__/`
  - untracked probe/fixture files under `tests/`
- Create a fresh Task 8 branch from local master in the isolated worktree. Do not develop in the dirty main worktree.

## Canonical documents to read first

1. `docs/superpowers/specs/2026-07-21-colosseum-player2-isolated-development-design.md`
2. `docs/superpowers/plans/2026-07-21-colosseum-player2.md`
3. `docs/superpowers/specs/2026-07-21-player2-task7-clock-scheduler-report.md`
4. `docs/superpowers/specs/2026-07-20-kodi-windows-video-architecture-decision.md`
5. The Player 2 contract/types under `native/player2/core/`

The plan is already approved. Do not stop to ask Hemanth for another mock-up, design approval, or implementation-plan approval. Investigation-first and TDD still apply inside each task.

## Shipped slices and commits

- `b693a61` — `[Agent 4 (Codex), player] Scaffold isolated Player 2 targets`
- `818d63b` — `[Agent 4 (Codex), player] Define Player 2 contract and states`
- `f4af374` — `[Agent 4 (Codex), player] Extract Player 2 zero-copy video core`
- `ed67b93` — `[Agent 4 (Codex), player] Add standalone Player 2 harness`
- `5809c65` — `[Agent 4 (Codex), player] Add Player 2 demux session`
- `d51d03e` — `[Agent 4 (Codex), player] Add Player 2 WASAPI audio`
- `71138ee` — `[Agent 4 (Codex), player] Add Player 2 clock and scheduler`

All are already fast-forwarded into local master. Do not cherry-pick or recreate them.

## Architecture that now exists

### Isolation

- `COLOSSEUM_BUILD_PLAYER2` defaults OFF.
- `player2_harness.exe` is a standalone Qt Quick D3D11 executable with its own `Colosseum/Player2Lab` storage identity.
- The harness imports no mpvqt, production services, production stores, Cinemeta or Torrentio.
- Production `colosseum.exe` remains unchanged and unlinked.

### Video

- FFmpeg D3D11VA decode produces D3D11 frames.
- A D3D11 video processor converts NV12/P010 into a three-slot shared RGBA texture ring.
- Producer and Qt Quick consumer use separate D3D11 devices on the same adapter with shared handles and explicit cross-device fences.
- Qt Quick wraps textures through `QNativeInterface::QSGD3D11Texture::fromNative`.
- No CPU video transfer is permitted; diagnostics count any violation.

### Demux/session

- `PlaybackGeneration` is the one existing stale-product boundary.
- `DemuxSession` owns the FFmpeg worker and posts typed metadata/packet/end signals back through Qt.
- `Player2Session` owns the audio sink/pipeline, playback clock, frame scheduler and demux session in destruction-safe member order.
- The current worker demuxes and decodes audio/video serially. Audio queue backpressure is condition-variable based and bounded at two seconds.

### Audio

- FFmpeg software audio decode feeds `swresample`, producing 48 kHz stereo float32.
- `WASAPIAudioSink` is event-driven shared-mode WASAPI on a dedicated COM MTA thread. There is no busy loop and no GUI-thread audio work.
- The bounded queue blocks its producer when full; endpoint consumption or generation flush wakes it.
- A generation flush rejects blocked stale writers and clears per-generation underrun diagnostics.
- `Player2Session::close()` advances the generation and flushes audio before joining demux, so old queued audio stops immediately.
- Volume/mute use `ISimpleAudioVolume`. Muting does not disable the audio master clock.

### Clock/scheduler

- `PlaybackClock` supplies QPC/media epochs, rate, pause/resume, invalidation and bounded correction.
- `FrameScheduler` returns Present, DropLate, RepeatCurrent or WaitUntilQpc with a 2 ms early tolerance, 40 ms late threshold and a maximum of three consecutive drops.
- With selected audio, the worker disciplines playback time to the WASAPI snapshot. Video-only files initialize a QPC fallback clock from their first video PTS.
- Deadline waits occur only on the demux worker. QML and the render thread do not sleep.
- `D3D11VideoPipeline` records one A/V error sample after the deadline wait per scheduling decision. Do not move this sample back before the wait; that measures decoder lead, not presentation scheduling error.

## Verified evidence

### Task 6 five-minute Wire audio soak

- HEVC, D3D11VA, P010, adapter match and shared fences true
- 300.055 seconds
- valid WASAPI endpoint clock
- queue capped at exactly two seconds after a real Matroska interleave burst
- zero CPU video transfers and zero D3D device errors
- the initial implementation failed at 131 seconds because a full queue was treated as fatal; the verified fix applies condition-variable backpressure instead of increasing capacity

### Task 7 final ten-minute Wire gate

Fixture: `C:\Users\Suprabha\Downloads\Colosseum\The Wire - S4E13 - Final Grades - 20260720_211141.mp4`

- 600.04 seconds uninterrupted
- HEVC / D3D11VA / P010
- 14,241 frames generated; 14,232 presented
- A/V scheduling p95: 2.497 ms
- scheduled late drops: 0
- texture-ring producer starvation: 1
- audio underruns: 5
- maximum audio queue: 1,030.667 ms; final queue: 524.667 ms
- CPU video transfers: 0
- D3D device errors: 0
- final state: Idle

The p95 is video PTS versus extrapolated WASAPI media position at GPU submission after the scheduler wait. It is not a photon/display-latency measurement; Task 16 still owns external PresentMon/ETW evidence.

Video-only and A/V fixture gates were rerun after clock invalidation was added. Both passed with zero scheduler drops, zero ring starvation, zero device errors and zero CPU transfers. The video-only fallback reported 9.135 ms p95; the short A/V fixture reported 2.124 ms p95.

### Green tests/contracts at handoff

- `player2_state_machine_test`
- `player2_texture_ring_test`
- `player2_audio_pipeline_test`
- `player2_clock_scheduler_test`
- `player2_demux_session_test`
- `player2_harness_contract.ps1`
- `player2_zero_copy_contract.ps1`
- `player2_isolation_contract.ps1`
- frozen prototype `prototype_contract_test.ps1`
- production `native/build-msvc.bat` with Player 2 OFF

## Task 8: exact next objective

Implement the approved Task 8 contract from the plan:

- Add `tests/player2/player2_seek_generation_test.cpp`.
- Modify `DemuxSession`, `Player2Session`, `PlaybackClock`, `D3D11VideoPipeline` and `AudioPipeline` only as required.
- Public transport interfaces: `seekExact(double)`, `seekRelative(double)`, `frameStep(int)`, `selectAudioTrack(QString)`, observable `seekCompleted(generation, actualSeconds)`.
- Mandatory seek order:
  1. advance the single `PlaybackGeneration`
  2. pause scheduling
  3. flush every active pipeline
  4. seek demux
  5. decode to the target
  6. reset the clock epoch
  7. publish completion
  8. restore the prior play/pause state
- Write the failing 100-seek tagged-product test first.
- Exercise audio track changes while playing, paused and immediately after seek.
- Enforce a 60-second test timeout and require 100 completions with zero stale audio/video products.
- Verify exact/relative seek near start/end and paused frame step forward/back.
- Review lock ordering and flush races before commit.
- Commit with `[Agent 4 (Claude), player] Make Player 2 seeks generation-safe`, using explicit pathspecs, then fast-forward local master.

Important: actual pause currently changes Player 2 session state but does not suspend FFmpeg decode or the WASAPI endpoint. Task 8 must make transport pause/resume real while preserving the Task 7 audio-master invariant. Do not create a second generation counter or private seek epoch in any component.

## Tasks after Task 8

Continue the approved plan in order:

- Task 9: explicit Smooth, Light and Full normalization stage using typed modes, not raw filter strings.
- Task 10: subtitles, chapters and presentation controls.
- Task 11: external subtitles, audio policy and passthrough rules.
- Task 12: network/progressive input and reconnection.
- Task 13: production-like host services in the standalone lab.
- Task 14: Player 2 shell/parity UI inside the lab.
- Task 15: close the parity ledger.
- Task 16: quantitative promotion gates, license manifest and go/no-go evidence.

Do not touch production integration files until Task 16 passes. Task 17 is intentionally outside the isolated arc.

## Hemanth’s audio-normalization agenda item

Hemanth explicitly asked: “do the frames drop when there’s audio normalisation happening?” Task 9 must answer this with evidence, not inference.

- Smooth is the bit-transparent/default path except endpoint conversion.
- Light matches the intent of current `dynaudnorm`.
- Full implements EBU R128 loudness normalization.
- Run cooldown-separated Wire passes for all three modes.
- Record CPU, GPU busy, scheduler late drops, ring starvation, A/V p95 and queue behavior.
- State plainly whether Light or Full causes frame drops on the i5-8365U/UHD 620.
- Do not silently remove or weaken normalization; it is a deliberate user choice.

## Commands and environment

Build from the isolated worktree. Useful environment:

```powershell
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK='1'
$env:PATH='C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;' + $env:PATH
```

Player 2 build:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build native\build-player2 --target player2_unit_tests player2_harness'
```

Key tests:

```powershell
.\native\build-player2\player2_state_machine_test.exe
.\native\build-player2\player2_texture_ring_test.exe
.\native\build-player2\player2_audio_pipeline_test.exe
.\native\build-player2\player2_clock_scheduler_test.exe
.\native\build-player2\player2_demux_session_test.exe .\native\build-player2\player2-fixtures
.\tests\player2\player2_harness_contract.ps1
.\tests\player2\player2_zero_copy_contract.ps1
.\tests\player2\player2_isolation_contract.ps1
.\native\prototypes\d3d11_qtquick_bridge\tests\prototype_contract_test.ps1
```

Real Wire gate pattern:

```powershell
$report=Join-Path $env:TEMP 'player2-task8-wire.json'
$media='C:\Users\Suprabha\Downloads\Colosseum\The Wire - S4E13 - Final Grades - 20260720_211141.mp4'
.\native\build-player2\player2_harness.exe --file $media --report $report --soak-seconds 600
Get-Content $report
```

Before every production rebuild, kill `colosseum.exe` by exact PID, then run `native\build-msvc.bat` once in `native\build-msvc`. Confirm `COLOSSEUM_BUILD_PLAYER2:BOOL=OFF` after each isolated slice.

## House constraints and review debt

- QML paints; C++ decides. Transport/config/scheduling/filter policy stays in C++.
- One build per output directory. Do not cross-configure the Player 2 and production build trees.
- Use TDD for every behavior change and systematic debugging for every unexpected gate failure.
- Stage/commit only explicit task pathspecs.
- Merge verified slices back to local master with `--ff-only` while preserving main-worktree dirt.
- Do not overclaim audible or display-photon observations from telemetry. Say exactly what the instrument measures.
- Kodi is an architectural reference only; no Kodi implementation was copied.
- Cross-substrate review was unavailable during Codex Tasks 6 and 7. As Claude, perform the missing independent review before extending the code:
  - Task 6: COM apartment and destruction order, endpoint lifetime, buffer arithmetic, blocked-writer cancellation, generation flush ordering.
  - Task 7: audio clock mapping, QPC extrapolation, drift correction, scheduler drop ceiling, cross-session invalidation and measurement semantics.
- Non-fatal FFmpeg warnings about PGS subtitle dimensions are expected on the Wire fixture. Do not treat them as Task 8 failures; Task 10 owns subtitle decoding.

## First actions for this wake

1. Bootstrap Agent 4 governance/memory in the Brotherhood workspace.
2. `cd` into the nested Colosseum repo before any git command.
3. Confirm local master is at or after `71138ee` and production Player 2 remains OFF.
4. Read the canonical documents and the Task 6/7 code listed above.
5. Perform the missing cross-substrate review and write down any P0/P1 issue before Task 8 edits.
6. Create the isolated Task 8 branch and begin with the failing 100-seek generation test.

Report findings honestly even if a later gate argues against Player 2. The point of Tasks 8-16 is to earn parity and promotion, not assume it.

---
