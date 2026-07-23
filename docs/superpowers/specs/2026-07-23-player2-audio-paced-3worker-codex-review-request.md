# Codex design-review request — Player 2 audio-paced 3-worker read-ahead

> Paste the block below into a fresh Codex chat (high reasoning). It is self-contained. Codex should
> review the *design* (not a diff) and return a verdict + failure modes + refinements BEFORE Agent 4
> implements it. Worktree: `Colosseum/.worktrees/player2-task1-isolation`, branch
> `agent4/player2-task8-seek`. Live app untouched.

---

You are reviewing a concurrency design for the Colosseum "Player 2" video engine before it is built.
Be adversarial about thread lifecycle and deadlock. This continues your own REQUEST-CHANGES review of
the read-ahead engine (you refuted the "17 fps GPU wall" as a measurement artifact; the real failures
are architectural). Three of your points are already addressed and committed on the branch:

- **Measurement fixed** (commit `0d7bb96`): `PlaybackMetricsAccumulator` anchors fps at the first
  valid audio clock, reports windowed fps + min audio-queue + signed A/V drift. An honest 100 s soak
  then proved the real failure (below).
- **Underrun clock fixed** (`e628c02`): WASAPI invalidates the audio clock on a zero-copy underrun;
  `PlaybackClock::decideClockResync` hard-resets the video master on recovery from a gap instead of
  crawling 5 ms/frame.
- **PacketQueue drop-oldest primitive** (`b8dc654`): a full video push can drop the oldest backlog to
  a keyframe and flag a decoder-flush discontinuity, instead of blocking. INERT until enabled.

## The confirmed failure (from the honest 100 s Full-EBU soak, The Wire S4E10)

Blocking design: video fps ~20 → 15 → **0.00 from t=60 s onward (frozen)**, 1853 audio underruns,
audio buffer empty by ~40 s, A/V drift 227 ms. Root cause = a **circular deadlock**:

1. Video consumes slightly below real time → its 8 s video `PacketQueue` fills.
2. The **demux thread blocks** on `videoQueue.push()` (`DemuxSession.cpp:1175`).
3. Audio is decoded **inline on that same demux thread** (`avcodec_send_packet(audioDecoder…)` +
   `receiveAudioFrames()` at `DemuxSession.cpp:1184-1185`). Blocked on video, the demux stops
   decoding audio → audio starves, its clock stalls.
4. Video paces to the stalled audio clock and stops draining the queue → queue stays full → demux
   stays blocked. Each waits on the other.

## Why drop-oldest ALONE does not fix it (already tried, uncommitted, refuted)

Enabling `dropOldestWhenFull` on the video queue cured audio (underruns 1853 → 40, buffer healthy the
whole 100 s) but caused **video runaway**: fps → ~1 from t=30 s, video drifts ~2.1 s AHEAD of audio.
Reason: a *full 8 s video queue is the normal read-ahead steady state, not distress*. Drop-oldest then
fires continuously, discarding the front packets (which sit AT the clock), so video keeps skipping
ahead then waiting. Removing the block without a new pacing source lets the demux read unbounded →
queue perma-full → perma-drop. One throttle removed, another created.

## Current architecture (2 threads)

- **Demux thread** (`DemuxSession::run`, ~`:500-1215`): reads packets; routes video via a blocking
  `videoQueue.push` (`:1175`); decodes audio AND subtitles **inline**; owns seek/flush/EOF/cancel and
  generation/`seekEpoch` epochs; creates the video queue (`:546`) and cancels+joins the video thread
  (`:1209-1211`). Seek flushes the video queue (`:919`) and the audio decoder inline; EOF sends a null
  audio packet + `videoQueue.setEndOfStream()` (`:1129-1139`).
- **Video thread** (spawned `:553`): pops `videoQueue` (`:804`), decodes video, runs the audio-master
  readiness barrier + `FrameScheduler` pacing + present (`drainDecodedFrames`, `:601`). Reads the
  audio clock from `audioPipeline->clock()`.

## Proposed architecture (3 threads — the design to review)

- **Demux thread**: reads + routes packets ONLY. Video → drop-oldest `videoQueue` (non-blocking).
  Audio → a NEW **blocking** `audioQueue` (~10 s bound, covers loudnorm's ~3 s lookahead). Blocking on
  the audio queue is what PACES the demux to real-time audio consumption, so audio never starves and
  the read stays near real time. Subtitles: TBD (still inline, or their own small queue).
- **NEW audio-decode thread**: pops `audioQueue`, owns the audio decoder, runs `receiveAudioFrames`
  → `audioPipeline`. Handles its own flush on discontinuity/seek and drain-then-flush on EOF.
- **Video thread**: unchanged in spirit — now free to drop frames (drop-oldest only fires under
  genuine video-decode overload, not normal read-ahead) without starving audio or running away,
  because the demux is no longer paced by video.

## Pressure-test these lifecycle questions (be specific and adversarial)

1. **Deadlock freedom.** With the demux blocking ONLY on the audio queue and the audio thread blocking
   only on the sink (WASAPI render thread always drains), is any new circular wait possible? Prove it
   or find the cycle. What if the audio sink stalls (device loss, pause)?
2. **Seek/flush.** Ordering to flush both queues + both decoders + reset epochs without a race or a
   stale packet crossing the epoch boundary. Who owns the audio decoder flush now — the audio thread
   or the demux? How is the flush signalled and acknowledged?
3. **EOF.** With audio on its own thread, correct ordering of `audioQueue.setEndOfStream()` /
   drain / null-packet flush vs the video EOF, so the "publish EOF only after the video tail drains"
   invariant still holds and audio doesn't cut early.
4. **Cancel/join.** Correct cancel-both-queues-then-join-both order so neither thread deadlocks on a
   full/empty queue during teardown. Any risk the demux is blocked on `audioQueue.push` when cancel
   arrives?
5. **Track switch** (`SelectAudioTrack`): the audio decoder is reinitialised mid-stream. With the
   decoder owned by the audio thread, how is the reinit coordinated (hand-off vs signal)?
6. **Backpressure sizing.** Audio queue bound (~10 s) vs memory; interaction with the video 8 s
   drop-oldest bound. Does the audio-paced demux keep the video queue near-full-but-draining (healthy)
   rather than perma-full (drop storm)?
7. **Clock interactions.** The just-landed underrun-clock fix (`decideClockResync`) and the readiness
   barrier both read `audioPipeline->clock()`. Does moving audio decode off the demux thread change
   WHEN a valid clock first appears, or the barrier's correctness?
8. **Reduction check.** Is the 3rd thread truly required, or is there a smaller correct fix (e.g., a
   watermark-gated video push that only drops under real distress while a bounded audio lookahead
   paces the demux)? If smaller suffices, say so.

## Return

A design verdict (SOUND / NEEDS-CHANGES), the concrete failure modes you found ranked by severity, the
minimal correct thread-lifecycle contract (seek/flush/EOF/cancel/track-switch), and any simplification.
Do not write the implementation — Agent 4 executes on Opus after your review.
