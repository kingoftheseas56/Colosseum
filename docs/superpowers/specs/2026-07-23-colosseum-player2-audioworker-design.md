# Player 2 AudioWorker — audio-paced 3-worker read-ahead (implementation-ready design)

> Post Codex design-review (NEEDS-CHANGES → corrected contract below, verdict: 3rd worker required).
> All P0s verified against `agent4/player2-task8-seek@a551997`. Live app untouched.
> Prereqs already landed: `0d7bb96` honest benchmark · `b8dc654` drop-oldest PacketQueue primitive ·
> `e628c02` underrun-clock (WASAPI invalidate + `decideClockResync`). `a551997` = DemuxSession WIP
> (drop-oldest enabled = video runaway; do NOT ship as-is — this design replaces it).

## Why (the confirmed failure)

Circular deadlock: video consumes slightly slow → 8 s video `PacketQueue` fills → demux **blocks** on
`videoQueue.push()` → but audio is decoded **inline on the demux thread**, so audio starves → its clock
stalls → video paces to the stalled clock → queue never drains. Freeze at ~60 s, 1853 underruns
(100 s Full-EBU soak, The Wire S4E10). Drop-oldest alone is refuted: a full 8 s video queue is the
*normal* read-ahead state, so drop-oldest fires continuously → video runaway (fps→1, +2.1 s ahead).

The fix: pace the demux by **audio**, not video. But the naive "demux blocks on a big audio queue"
deadlocks pause/resume/seek/cancel because **the demux thread is also the sole command processor**
(`processCommands` in `run()`, verified). Hence the corrected contract.

## Architecture (3 workers + inline subtitles)

- **Demux thread**: reads + routes packets ONLY; owns container seek, generation/epochs, command
  processing. Video → drop-oldest `videoQueue` (non-blocking). Audio → **small** blocking `audioQueue`.
  Subtitles decoded inline (non-blocking; keep for now — do NOT add a 4th worker).
- **AudioWorker** (NEW): exclusively owns the audio decoder, `AVFrame`, resampler, normalizer, and all
  mutable `AudioPipeline` operations. Pops `audioQueue`, decodes, normalizes, feeds the sink. Owns its
  own flush/reinit/normalization-reconfig, driven by an acknowledged control protocol.
- **Video thread**: unchanged in spirit — pops `videoQueue`, decodes, runs the readiness barrier +
  `FrameScheduler` + present. Free to drop frames (drop-oldest only fires under genuine video overload
  now, because the demux is no longer paced by video).

## Corrected lifecycle contract (the load-bearing part)

### 1. Packet push has THREE results (not block-or-cancel)
`enum class Admit { Accepted, Interrupted, Cancelled }`. A blocked demux `audioQueue.push()` must be
**interruptible** by command arrival (Pause/Resume/Seek/track-switch/normalization) WITHOUT permanently
cancelling the queue. On `Interrupted`, demux stops routing, services commands, then resumes routing.
`Cancelled` = teardown only.

### 2. Cancel / abort order (shared session stop object reachable from `DemuxSession::cancel()`)
Today `cancel()` only sets the flag + wakes the command CV + HTTP source — it cannot reach run-local
queues, so a demux blocked on `audioQueue.push()` would hang `joinWorker()`. New order:
1. Mark stop.
2. Cancel audio AND video packet queues.
3. Abort any blocking sink write (explicit abort, distinct from generation flush — do NOT reuse the
   flush path; `cancel()`'s existing comment about "Audio generation rejected" still applies).
4. Cancel the media source + notify command waiters.
5. Join demux, audio, video workers.
Worker failure invokes the SAME stop path and cancels both sibling queues — a video failure must wake a
demux blocked on audio, and vice versa.

### 3. Seek protocol (acknowledged; demux no longer touches the audio decoder)
1. Interrupt demux admission, stop routing.
2. Adopt new generation; invalidate the sink clock immediately.
3. Atomically clear each packet queue; install `FlushEpoch` controls.
4. Each worker flushes its OWN decoder/pipeline (AudioWorker: `avcodec_flush_buffers` +
   `flushFilters`; video: decoder + presentation queue) and ACKs.
5. Demux performs the container seek.
6. Resume routing only after BOTH acks; every new packet carries the new generation.
(Replaces the current inline `avcodec_flush_buffers(audioDecoder)` + `flushFilters()` at ~:898/901.)

### 4. Track switch → `ReconfigureAudio(codecpar, timeBase, generation)` (acknowledged)
AudioWorker opens a TEMP decoder, swaps only on success, flushes downstream state, ACKs. Demux changes
the selected stream only AFTER success. (Replaces inline `avcodec_alloc_context3`/`audioDecoder.reset`
at ~:943-948.)

### 5. Normalization → same control protocol
`configureNormalization()` mutates worker-owned filter state → route the Normalization command to the
AudioWorker, not inline (~:1008).

### 6. EOF = in-order queue item with TWO completion states
"Audio decoder drained" ≠ "WASAPI played it." AudioWorker: null-packet decoder drain → resampler drain
→ normalizer drain → **wait for sink playback drain**. Video: decoder + presentation-queue drain.
Publish EOF only after BOTH the video presentation tail and the audible audio tail finish. Normal EOF
joins gracefully — it must NOT reuse the cancellation path (which discards buffered tails).

### 7. Pause / Resume
Pause interrupts any blocked demux `audioQueue.push()` BEFORE stopping the sink; demux then sleeps on
the command channel so Resume stays processable. (WASAPI already stops consuming + retains its bounded
queue while paused — that's the deadlock source the interrupt removes.)

### 8. Clock epoch
Moving flush/reinit to the AudioWorker opens a window where the OLD clock still reads valid after
`audioPathEpoch` changes. Add a generation/epoch to `AudioClockSnapshot`; the readiness barrier +
`decideClockResync` accept a clock ONLY when its epoch matches the active audio path.

### 9. Queue sizing (measure — do not guess)
The audio queue must be SMALL. Effective demux lead ≈ audioQueue + loudnorm(~3 s) + WASAPI(~2 s). With
a 10 s audio queue that's ~15 s vs an 8 s video horizon → video perma-full → drop storm returns. Start
at **~1-2 s of compressed audio packets** and measure. Invariant to hold:
`audioPacketLead + normalizerLead + sinkLead + margin < videoHorizon`.

## Pure-logic seams to TDD first (before threading)
- The admission/interrupt state machine (Accepted/Interrupted/Cancelled transitions).
- The seek/EOF/track-switch ACK barrier (all-workers-acked gate) as a pure coordinator, decoupled from
  real threads (inject fake workers).
- Queue-sizing invariant check (`leads + margin < horizon`) as a pure predicate.
Then wire real threads around the tested seams.

## Verification (Codex's ledger → must all pass)
Deadlock-free · seek/flush · EOF-tail · cancel/join · track-switch · backpressure sizing · clock
epoch. Concretely validate: pause/resume, **seek-under-full-queue**, **cancel-under-full-queue**,
track switch, EOF tail, and a **100 s Full-EBU soak** on The Wire S4E10 showing audio healthy
(underruns low, buffer full) AND video holding ~24 fps without drifting — then Hemanth eyes/ears-on
(Qt is uncapturable; the JSON "healthy" is not the gate).

## Source anchors
Demux `run()` + `processCommands` + seek/track/normalization/EOF: `native/player2/core/DemuxSession.cpp`
(~:500-1215). Video thread + barrier: `drainDecodedFrames` ~:601. Video queue: ~:546. Audio inline
decode: ~:1184. Sink blocking enqueue + pause: `native/player2/audio/WASAPIAudioSink.cpp`
(`enqueueBlocking` ~:47, pause-stop ~:356). Clock: `PlaybackClock` + `AudioClockSnapshot`
(`WASAPIAudioSink.h`). Codex recap:
`~/.claude/recaps/agent-0/brother-agent-0-2026-07-23-player2-audio-paced-design-review.md`.
