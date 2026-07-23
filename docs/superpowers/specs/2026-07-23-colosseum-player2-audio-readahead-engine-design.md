# Player 2 — Audio read-ahead engine (design)

**Author:** Agent 4 (Claude) · **Date:** 2026-07-23 · **Status:** design, implementation in progress
**Branch/worktree:** `agent4/player2-task8-seek` in `Colosseum/.worktrees/player2-task1-isolation`

## The problem (measured, not guessed — from the sounding-keel wake)

On **Full** normalization (`loudnorm=I=-16:TP=-1.5:LRA=11`, EBU R128), the audio fractures and
crackles constantly. Root cause, instrumented: `loudnorm` has a ~3-second internal lookahead, but
Player 2's single decode loop produces audio only at **real time** — the video pacing sleep in
`DemuxSession::receiveVideoFrames` (`WaitUntilQpc`, `DemuxSession.cpp:774-790`) blocks the one loop
that also reads audio packets. So `loudnorm` is permanently ~3 s behind its own supply and the
WASAPI sink queue never builds a cushion: underruns climb monotonically, `depthFrames` dips to 0.
(Smooth stays healthy at 0.5-0.8 s because it does no lookahead.)

A previous attempt moved the *filter* to its own thread — it did **not** help, because the *supply*
is throttled, not the filter. The supply is throttled because **demuxing is paced by video**.

## The cure — unpace the demuxer (read-ahead), pace video on its own thread

The fix is the read-ahead half of mpv's model: **demuxing must run ahead of playback**, so audio can
be decoded + normalized faster than real time and fill both `loudnorm`'s internal lookahead and the
sink's 2 s output cushion.

### Why 2 threads, not 3 (reduction-reflex decision)

mpv uses three threads (demux read-ahead → audio-decode → video-decode). The third thread —
a *separate* audio-decode thread — exists solely to keep audio's sink back-pressure from stalling
video packet supply. `WASAPIAudioSink::write()` uses `enqueueBlocking` (blocks while the 2 s queue
is full, `WASAPIAudioSink.cpp:429-440,46-64`). If audio is decoded inline on the demux thread, a
full sink blocks the demux thread — **but only for one endpoint period (~10-40 ms)**, because the
render thread frees capacity every period and notifies. The video packet queue (bounded ~2 s)
absorbs a 40 ms gap without a hitch. So the third thread's benefit is marginal here; it does not earn
its complexity (cross-thread codec ownership + audio-track-rebuild-on-another-thread).

**Decision: 2 threads.**
- **Demux thread (control + audio + subtitles):** owns the `AVFormatContext`, the audio codec, the
  subtitle pipeline, generation, and all transport commands — exactly as today, minus video decode
  and minus pacing. It reads packets *unpaced*, decodes audio inline (races ahead), decodes
  subtitles inline, and routes **video** packets to a `PacketQueue`.
- **Video decode thread (paced presenter):** owns the video codec + `AVFrame`; pops video packets,
  decodes to D3D11, paces against the master clock, presents. This is where the `WaitUntilQpc` sleep
  now lives — off the reading path.

The 2-thread design is a **strict subset** of the 3-thread design: the video split + video queue are
identical in both. If eyes-on ever shows residual coupling, adding the audio thread is a clean
addition (an audio `PacketQueue` + a thread) with **zero rework** of what we build now.

## The seam

```
                 ┌───────────────────────── Demux thread (unpaced) ─────────────────────────┐
  container ───► │ av_read_frame → route:                                                   │
                 │   video pkt ─► videoQueue.push()   (blocks only when ~2 s ahead)          │
                 │   audio pkt ─► decode+normalize+sink.write()  (inline, unpaced)           │
                 │   sub  pkt ─► subtitle decode → post cue (inline)                         │
                 │ + transport commands (seek/pause/track/normalization), generation owner   │
                 └───────────────┬──────────────────────────────────────────────────────────┘
                                 │ videoQueue (PacketQueue, bounded)
                 ┌───────────────▼──────────── Video decode thread (paced) ─────────────────┐
                 │ pop → (flush codec on generation change) → decode → discard-to-seek-      │
                 │ target → pace vs PlaybackClock → submit to D3D11VideoPipeline             │
                 └──────────────────────────────────────────────────────────────────────────┘
```

`PlaybackClock` is already fully `std::mutex`-guarded (`PlaybackClock.h`), so both threads may read
and correct it safely. The **sink clock stays the master**: audio (demux thread) feeds it, video
thread corrects `PlaybackClock` toward it and paces — the same correction logic as today, just moved
onto the video thread.

## Protocols

### Read-ahead & back-pressure
- `videoQueue` bounds: `maxBufferedUs ≈ 2 s`, plus a byte cap and packet-count cap as safety. Demux
  blocks on `push` when video is ~2 s ahead; the video thread drains at real time and frees it.
- Audio inline `write()` blocks when the sink's 2 s queue is full — bounded to one endpoint period.
- Net effect: the demux thread oscillates ~2 s ahead of playback for both streams; `loudnorm` gets
  unpaced supply and stays fed; the sink keeps a cushion. **This is the cure.**

### Seek (generation-on-packet)
The single `gen` barrier is preserved. On a Seek command (demux thread):
1. `gen = newGen`; `m_activeGeneration = newGen`; store shared `m_seekTargetUs`.
2. Flush audio codec + `AudioPipeline` (queue + normalizer) — as today.
3. `videoQueue.flush()` — drops old-gen video packets still in flight.
4. `pipeline->flush(newGen)`, `playbackClock->invalidate()`, `frameScheduler->reset()` — as today.
5. `av_seek_frame(container, clampedTarget)`.
6. Resume reading; new packets are pushed/tagged with `newGen`.

The **video thread**, when it pops a packet whose generation differs from its last, flushes its own
`avcodec` buffers (correct: codec flush happens on the thread that owns the codec), then discards
decoded frames with `pts < m_seekTargetUs` and presents the first frame `>= target` (the video
landing frame), exactly mirroring today's `decodingToTarget` logic — just on the video thread.

### Seek landing (who resets the clock / posts seekCompleted)
- **Audio present (audio is master):** the demux thread lands the seek in `receiveAudioFrames`
  (`landSeek`: reset clock, restore pause state, `postSeekCompleted`) — unchanged, still on the
  demux thread. The video thread only discards-to-target + presents; it never resets the clock.
- **Video-only (no audio):** the video thread lands (reset clock, `postSeekCompleted`). Cross-thread
  but `PlaybackClock` is mutex-safe and `postSeekCompleted` is a queued signal.

### Pause / resume
- Shared `m_paused` (as today). Demux: `audioPipeline->setPaused` (endpoint stop), clock pause.
  Demux keeps reading into the bounded video queue until it fills, then blocks — fine.
- Video thread: while paused, stop presenting/pacing (park on a condition or poll `m_paused`), do
  not advance. The endpoint stop freezes the audio clock; the video thread holds position.

### End of file
- Demux hits `AVERROR_EOF`: drain audio decoder + `AudioPipeline::drain` (flushes `loudnorm`
  lookahead) — as today — then `videoQueue.setEndOfStream()`.
- Video thread drains the queue, gets `EndOfStream`, flushes the video decoder tail, presents the
  remaining buffered frames, then signals completion.
- **`ended(EndOfFile)` is posted only after the video thread has drained**, so the UI is not told
  "done" while up to ~2 s of buffered video is still presenting. Coordinated via a completion flag /
  join, not by the demux hitting EOF alone.

### Cancel / teardown (no hangs — the join guarantee)
- `cancel()`: set `m_cancelled`, `videoQueue.cancel()` (wakes a blocked video `pop`/`push`), and
  flush the sink (bumps its generation → wakes a blocked `enqueueBlocking` on the demux thread so it
  observes the cancel), unblock the HTTP source (as today), then join **both** threads.
- Order: signal → wake every queue/sink waiter → join video thread → join demux thread.

### Track change, A/V delay, frame-step, device-lost, subtitles
- **Audio track change:** stays entirely on the demux thread (it owns the audio codec) — unchanged.
- **Subtitles:** decoded + posted inline on the demux thread — unchanged.
- **A/V delay, frame-step, device-lost detection, HDR policy:** unchanged; frame-step is a seek, so
  it follows the seek protocol; video device-lost still surfaces via the video decode path.

## Verification plan
- **Regression oracle (must stay green):** `player2_demux_session_test`, `player2_seek_generation_test`,
  `player2_audio_pipeline_test`, `player2_audio_normalizer_test` (fixture-based, real FFmpeg decode).
  Baseline captured green on 2026-07-23 before any change.
- **Foundation:** `player2_packet_queue_test` — 7 hermetic concurrency tests, green (`5557e2c`).
- **Read-ahead behavior:** a new assertion that under Full normalization the audio sink depth /
  buffered-ahead stays healthy (no monotonic underrun climb) while playing a fixture — the
  measurable proof of the cure, replacing the manual instrumentation from sounding-keel.
- **Eyes-on (Hemanth):** launch `player2_harness --file <5.1 clip> --normalization full`; confirm
  "loud" no longer fractures. This is the acceptance gate no test replaces.

## Not in scope this pass
- The separate audio-decode thread (3rd thread) — only if eyes-on shows residual coupling.
- Task 13 remaining chrome, Tasks 14-16.
