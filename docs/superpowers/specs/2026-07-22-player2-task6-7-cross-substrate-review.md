# Player 2 — Cross-substrate review of Codex Tasks 6 & 7

**Reviewer:** Agent 4 (Claude), independent substrate.
**Reviewed:** Task 6 (WASAPI audio) and Task 7 (master clock / frame scheduler), commits `d51d03e` and `71138ee`, plus their touch-points in `Player2Session`, `DemuxSession`, `D3D11VideoPipeline`.
**Why:** Codex had no second pair of eyes during Tasks 6/7. The carryover requires this review, with any P0/P1 written down, **before** Task 8 edits.
**Verdict:** No P0. The audio-master invariant, the close()-flush-before-join ordering, the post-the-wait A/V measurement, and the double generation-gating are all correctly implemented — I verified each rather than trusting the recap. One P1 (latent, reopen-only), a few P2/minor notes. **Task 8 is clear to start.**

---

## Confirmed-correct (checked, not assumed)

- **close() flushes audio before joining demux** — `Player2Session::close()` advances generation → `m_audioPipeline.flush(next)` → `m_demux.cancel()`. A demux worker blocked in `enqueueBlocking` is woken by the queue flush (new generation → returns false) *before* `cancel()` joins it. No shutdown deadlock on the normal path.
- **A/V error sampled after the scheduler wait** — `DemuxSession::run` records `noteSchedulingDecision(ptsUs - positionAt(now), false)` only after the `WaitUntilQpc` sleep, so it measures presentation-scheduling error, not decoder lead. This is the Task 7 fix and it is in place.
- **Double generation gating** — worker→GUI posts check `m_activeGeneration == generation`, and every `Player2Session` handler re-checks `m_generation.accepts(generation)`. Stale products from a cancelled worker cannot reach observers.
- **Drop ceiling** — `FrameScheduler` force-presents after `maximumConsecutiveDrops` (3), preventing blackout; resets the counter on present/empty.
- **Audio-master reset/correct** — first valid audio snapshot resets the clock epoch; subsequent snapshots `correctToward` with a 5 ms clamp. Video-only falls back to QPC from first PTS.

---

## Findings (review debt)

### P1 — Producer fence value is non-monotonic across a reopen (PLAUSIBLE)
`DemuxSession::run` uses a **worker-local** `videoSequence` that restarts at 0 on every `open()`. `D3D11VideoPipeline::submitDecodedFrame` signals the shared producer fence with `token.sequence` (== `videoSequence`), and the consumer waits on that same value. Within one playback session this is monotonic and correct. **But the pipeline and its fences persist across opens.** On the 2nd+ media opened in a session, the worker signals the fence with 1, 2, 3… while the fence's last completed value is already large (~14 000 after a 10-min play). A consumer `Wait(fence, 1)` is then satisfied immediately by the *stale* high value, so it may present a texture before that frame's `VideoProcessorBlt` has actually completed → possible visual corruption/tearing on the first frames of the second video.
- **Scope:** reopen only. **Orthogonal to Task 8** — Task 8's seek stays inside one `run()`, so `videoSequence` remains monotonic across seeks.
- **Untested:** Task 7 evidence was a single uninterrupted play; the 50 open/close cycles (Task 16) would exercise this.
- **Recommended fix:** signal/wait on a **pipeline-level monotonic counter** that never resets, decoupled from `token.sequence` (which can stay the ring-identity). Belongs to a dedicated small fix or the reopen/recovery work (Task 12), **not** silently bundled into Task 8's seek path.

### P2 — Torn clock snapshot in `WASAPIAudioSink::clock()`
`clock()` reads `mediaPositionUs`, `qpcTimestamp`, `valid` as three independent atomics. The demux worker reads them non-atomically, so it can pair a fresh media position with a stale QPC (up to one callback ≈ 10 ms skew), skewing `audioNow`. Impact is bounded by the 5 ms `correctToward` clamp, so it degrades to a small correction, not a jump. **Fix option:** publish the snapshot behind a seqlock or a single versioned struct.

### P2 — Blocked audio producer not woken by sink `stop()`
`AudioBufferQueue`'s capacity condition is notified only by `read()` and `flush()`. If `WASAPIAudioSink::open()` triggers `stop()` (format change / reopen) while the demux worker is blocked in `enqueueBlocking`, nothing wakes it. The normal session close/seek path flushes audio (→ queue flush → notify) before touching the worker, so this is covered today; a mid-stream sink reopen is not. Low likelihood. **Fix option:** a cancel flag on the queue that `stop()` sets and notifies.

### Minor
- `ISimpleAudioVolume::SetMasterVolume`/`SetMute` are called on **every** audio callback (~10 ms). Redundant, and it drives the app's Windows per-app mixer slider rather than applying per-sample gain. Consider only-on-change, or per-sample gain.
- `WASAPIAudioSink::Impl::m_endpointFrames` is stored but never read — dead.

---

## Bearing on Task 8

Task 8 must (all confirmed *not yet* implemented, so these are new work, not regressions):
- Add a **command channel** into the single linear `DemuxSession::run` loop — it currently has no seek and no pause path, only cooperative `m_cancelled` checks.
- Make **pause/resume real**: `Player2Session::play/pause` are state-only; they never touch `PlaybackClock::pause/resume`, the decode loop, or the WASAPI endpoint.
- On seek, **invalidate the clock and reset the worker-local `audioMasterActive`** so the master clock re-establishes at the new position (otherwise the clock free-runs on the old epoch through the seek).
- Keep the **single** `PlaybackGeneration` as the only barrier — no component invents its own seek epoch.
