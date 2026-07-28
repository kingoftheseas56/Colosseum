# Colosseum mpv Zero-Drop Experiment

**Date:** 2026-07-28
**Owner:** Agent 4 (Codex), player lane
**Decision:** Approved by Hemanth on 2026-07-28

## Objective

Prove whether Colosseum's existing mpv integration can sustain the same zero-additional-drop
playback that standalone mpv demonstrated on the same machine and Tenet file.

This is a short-term product experiment, not a Player 2 promotion decision. Player 2 remains
preserved on its branch while the experiment runs.

## Evidence Behind the Experiment

- Standalone mpv uses its native `gpu-next` / D3D11 presentation path on this machine.
- With `video-sync=display-resample` and interpolation enabled, a later five-minute standalone
  Tenet observation showed zero dropped frames.
- Colosseum's current mpv path has previously shown continuously increasing output drops on the
  same file.
- The earlier 139-drop standalone observation over 30 minutes does not invalidate the later
  zero-drop interval. The experiment therefore measures counter deltas after warm-up rather than
  requiring an absolute lifetime counter of zero.

## Chosen Approach

Use a staged experiment:

1. Enable the proven synchronization and interpolation settings inside Colosseum's current mpv
   integration.
2. Measure the current integration without changing its presentation architecture.
3. Stop if it passes.
4. If it fails, preserve the failure evidence and use it to scope a separate direct-present D3D11
   mpv spike.

The experiment does not modify Player 2 and does not begin the direct-present spike.

## Controlled Inputs

- Media: the same local Tenet file used in the standalone and Colosseum observations.
- Machine and display: Hemanth's current Intel UHD 620 laptop and its current display mode.
- Settings under test:
  - `video-sync=display-resample`
  - `interpolation=yes`
- Each measured run starts from a clean Colosseum launch.
- Each run uses the same playback segment where practical.
- Normalization and unrelated playback settings remain unchanged during the comparison.

The implementation plan must record the exact media path, mpv property values, display refresh,
build identity, and start timestamp used for the final evidence.

## Measurement Protocol

1. Launch the exact newly built Colosseum executable.
2. Open Tenet and allow 30 seconds of warm-up.
3. At the measurement boundary, record:
   - decoder-drop counter;
   - output-drop counter;
   - current hardware-decoding mode;
   - A/V synchronization value;
   - playback position.
4. Continue uninterrupted playback for five minutes.
5. Record the same values at the end.
6. Repeat with a fresh Colosseum launch.
7. Hemanth supplies the eyes-on smoothness verdict; Agent 4 handles builds, logs, and counter
   collection.

Counters are evaluated as deltas. Startup activity before the 30-second boundary is excluded.

## Pass and Fail Gates

The experiment passes only when both consecutive Colosseum runs meet every condition:

- decoder-drop delta is zero;
- output-drop delta is zero;
- playback remains synchronized without a sustained A/V drift;
- playback completes the measured interval without a crash, recovery loop, or visible stall;
- Hemanth judges the motion smooth.

Any non-zero measured drop delta is a failure for the zero-drop objective. The report must retain
the timing and pattern of drops instead of reducing the result to a simple red status.

If a run is invalidated by an unrelated interruption, the report identifies it as invalid rather
than counting it as a pass or failure.

## Failure Classification

If the current integration fails, classify the evidence before proposing the next design:

- drops begin immediately and accumulate steadily;
- drops cluster around seeks, overlays, or chrome activity;
- drops correlate with a hardware-decode fallback;
- drops occur without decoder loss but at output presentation;
- A/V synchronization drifts despite stable drop counters.

The classification is evidence for a later direct-present design. It is not permission to begin
that implementation automatically.

## Safety and Scope

- mpv remains Colosseum's daily-driver engine.
- No default switch to Player 2 occurs.
- No Player 2 files are changed by this experiment.
- Existing dirty Player 2 work and runtime artifacts in the worktree are preserved.
- The mpv behavior change must be reversible and isolated.
- The normal player must still open, play, pause, seek, close, and render its existing QML chrome.

## Deliverable

Produce a concise evidence report containing:

- exact commit and executable identity;
- exact mpv properties used;
- start/end counter snapshots and deltas for both runs;
- hardware-decode and A/V synchronization observations;
- Hemanth's eyes-on verdict;
- one result: `PASS - tuned integrated mpv sustains zero additional drops` or
  `FAIL - direct-present design required`, with the observed failure pattern.

No Player 2 investment decision is made from this experiment alone.
