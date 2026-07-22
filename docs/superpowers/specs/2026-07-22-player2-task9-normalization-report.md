# Player 2 Task 9 — Normalization modes + the frames-drop answer

**Question on Hemanth's agenda:** *"Do the frames drop when there's audio normalization happening?"*
Answered here with real playback of a real HEVC clip on the target-class hardware, not inference.

## What Task 9 built

An explicit, **typed** loudness stage (`AudioNormalizer`) between the resampler and the WASAPI
endpoint. The public contract carries only the mode enum — never a raw filter string:

| Mode | Meaning | Internal filter |
|---|---|---|
| **Smooth** (default) | bit-transparent; input passed through untouched | none |
| **Light** | gentle dynamic gain (intent of the old dynaudnorm path) | `dynaudnorm=framelen=100:gausssize=3` |
| **Full** | EBU R128 loudness normalization | `loudnorm=I=-16:TP=-1.5:LRA=11` |

The graph is owned exclusively by the decode thread; live mode changes and seek/track flushes reach it
through the command channel, never from the GUI thread (same thread-safety rule as the resampler).

## Evidence (The Wire S4E13, HEVC/D3D11VA/P010, i5-8365U + UHD 620, 30 s passes)

| Mode | scheduledLateDrops | A/V p95 (steady) | ring starvation | filter latency | startup underruns |
|---|---|---|---|---|---|
| Smooth | **0** | ~4–6 ms | 0 | 0 ms | ~2–7 |
| Light | **0** | ~3–4 ms | 0 | ~384 ms | ~120–330 |
| Full | **0** | ~2–18 ms | 0–51 (run-dependent) | ~2960 ms | ~250–580 |

Zero device errors and zero CPU transfers in every mode (zero-copy intact).

## The plain answer

- **The frame scheduler never drops a frame because of normalization.** `scheduledLateDrops = 0` in
  Smooth, Light and Full across every run. A/V sync stays well inside the 40 ms threshold.
- **Light is effectively free for video** and adds only a brief (~0.4 s) audio warmup at playback start
  while the filter fills its small lookahead.
- **Full (EBU R128) carries an inherent ~3 s analysis latency** — audio begins a few seconds in while
  video plays immediately, and under CPU pressure the heavy loudnorm on the *shared serial decode
  thread* can occasionally starve the video ring (0–51 frames over 30 s, run-dependent). This is
  producer starvation, not a scheduler drop, and it never desynced A/V here.
- **Smooth remains the default** and is cost-free.

A key A/V-sync fix landed with this task: on first audio lock the clock now *converges* toward the
audio master instead of snapping backward, so the warmup delay of Light/Full no longer spikes A/V
error (Light p95 fell from ~2.8 s to ~4 ms). It also smooths post-seek re-lock.

## Honest limits / follow-ups

- 30 s passes show cold-start variance (one Smooth pass logged a startup-transient p95). The rigorous,
  long-soak numeric gate for this lives in **Task 16**; Task 9 answers the agenda question directionally.
- Full's ring-starvation-under-load is the clearest cost. If Full becomes a shipping default it would
  want the audio filter off the video decode thread; as an opt-in mode with Smooth default it is
  acceptable and documented (candidate parity-ledger ACCEPTED EXCEPTION).
- Reproduce: `tests/player2/player2_normalization_benchmark.ps1 -Media <clip> -SoakSeconds <n>`.
