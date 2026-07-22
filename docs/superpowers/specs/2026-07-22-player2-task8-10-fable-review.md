# Player 2 Tasks 8–10 — Fable cross-model review + outcomes

**Reviewer:** Agent 4 (Fable 5), a different-minded pass over the same author's Opus work.
**Range:** `bb9dbbd..084ce36` (review record + Tasks 8, 9, 10). Verdict: engine core sound, no blockers.
**Verified, not assumed:** the single-generation barrier, worker-owned flush discipline, the
"subtitle-select must not advance the generation" reasoning, and the tests' fixture-ground-truth
stale-guards all held under hostile reading.

## Fixed this pass (surgical)

- **F1 — audioDelay was decorative (P2, parity-theater).** The property was stored but nothing
  consumed it — the surface matched mpvitem, the behaviour did not. Now wired: `Player2Session::
  setAudioDelay` pushes a live µs offset to the worker (`DemuxSession::setAudioDelay`), applied to
  the audio pts reported to the master clock, so video is scheduled against the offset (positive =
  audio lags video, mpv parity). The true position (seek landing, frame-step) stays unshifted.
  **Sign/magnitude are eyes-on-pending** (no headless A/V rig); validate in Task 13/16.
- **F2 — audio-track-change signal lied on failure (P2).** If the requested decoder failed to open,
  the old track kept playing but the signal reported the *requested* index. Now reports
  `audioStreamIndex` (the track actually decoding).
- **F3 — data race on normalizer latency counters (P3).** `m_pushedSamples`/`m_pulledSamples` are
  now `std::atomic<qint64>` (decode thread writes, GUI reads the diagnostic).
- **F6 — misleading ASS-format comment (P3).** The code correctly parses FFmpeg's decoded event
  ("ReadOrder,Layer,Style,…", text after the 8th comma); the comment described the *file* format and
  would have invited a wrong "fix". Comment corrected.

## Deferred — parity-ledger debt (do NOT count these rows done before closing)

- **F4 — subtitle blank after seek until the next cue starts.** mpv re-displays the cue active at the
  landing point; we don't. Fix belongs with the subtitle-layer shell work (Task 13) or a small
  "re-emit active cue after seek" in the worker. **Owner: Task 13.**
- **F5 — PGS/bitmap subtitle path untested and likely byte-swapped.** Only the SRT text path has a
  fixture. The palette copy labelled "RGBA" copies FFmpeg PAL8 entries verbatim, which are ARGB
  native-endian → almost certainly **BGRA byte order** (R/B swapped). Needs a bitmap fixture + eyes-on
  before the ledger claims PGS. **Owner: Task 10 close-out / Task 13 eyes-on.**
- **F7 — pause ignored during a seek.** `Player2Session::pause()` only accepts Playing/Buffering;
  mpv accepts pause anytime. Minor. **Owner: Task 13 transport wiring.**

## Standing note

Execution runs on Opus (model-routing default); the review was the Fable deliverable. These fixes
landed on Opus. Production Player 2 remains OFF.
