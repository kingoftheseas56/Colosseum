# D3D11 Backend Efficiency Benchmark Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decide whether Colosseum's native D3D11 prototype makes normal 1080p HEVC playback materially lighter than production mpvqt on the target Intel laptop, using Hemanth's fixed 25–30% decision bar.

**Architecture:** Run two interleaved steady-state passes per contender on the same local Wire episode in fullscreen. Capture per-PID Windows GPU-engine counters, process CPU and memory, PresentMon pacing/GPU duration, and available thermal/throttle counters; discard warm-up and compare pass means without treating the video-only prototype's CPU/memory advantage as fully attributable to rendering.

**Tech Stack:** Windows 11 performance counters, PresentMon 2.3.0, PowerShell, Qt 6.11.1, MSVC 2022, committed Colosseum and prototype executables.

## Global Constraints

- Hardware is Intel i5-8365U, UHD Graphics 620, 16 GB, Windows 11.
- Media is `C:/Users/Suprabha/Downloads/Colosseum/The Wire - S4E13 - Final Grades - 20260720_211141.mp4`.
- Native is the committed `native/prototypes/d3d11_qtquick_bridge/` tree, rebuilt in a fresh output directory.
- Production is the committed `colosseum.exe`, rebuilt only after killing an existing instance by exact PID.
- Production Loudness must be Smooth/off; cache must be 100%; `hwdec-current` must be recorded.
- Run two passes per contender in ABBA order, fullscreen from the same starting point, with 30 seconds warm-up, 120 seconds measurement, and at least 90 seconds cooldown.
- No production source changes.
- Proceed only for at least 25–30% lower steady GPU busy and/or CPU, or a clear thermal/throttle win. Single-digit improvement means permanently shelve the native arc.

---

### Task 1: Lock artifacts and telemetry

**Files:**
- Create: `%TEMP%/colosseum-efficiency-20260721/manifest.txt`
- Create: `%TEMP%/colosseum-efficiency-20260721/baseline-counters.txt`

**Interfaces:**
- Consumes: committed `master`, prototype CMake target, production build script, PresentMon and Windows counters.
- Produces: exact executable hashes, build identifiers, display/media facts, and valid counter names used by every pass.

- [ ] Record `git rev-parse HEAD`, SHA-256 hashes and timestamps for both executables, media hash/stream facts, logical CPU count, display resolution and power plan.
- [ ] Confirm `PresentMon.exe` version and validate a short capture exposes present timing and GPU-duration columns.
- [ ] Validate `GPU Engine`, process, CPU temperature/frequency and performance-limit counters. Record `Power Meter` as unavailable if it remains zero under load.

### Task 2: Rebuild committed contenders

**Files:**
- Create: `%TEMP%/colosseum-d3d11-efficiency-build/`
- Modify: no tracked source files.

**Interfaces:**
- Consumes: committed prototype CMake tree and `native/build-msvc.bat`.
- Produces: exact benchmark executables and hashes.

- [ ] Enumerate running `colosseum.exe` processes and kill only their recorded PIDs.
- [ ] Rebuild production from committed `master`; preserve unrelated dirty files.
- [ ] Configure and build the prototype into a never-before-existing output directory, deploy Qt and FFmpeg runtime DLLs, then run `prototype_contract_test` and `slot_ring_test`.

### Task 3: Capture four balanced passes

**Files:**
- Create: `%TEMP%/colosseum-efficiency-20260721/{native-1,production-1,production-2,native-2}/`

**Interfaces:**
- Consumes: exact PIDs, rebuilt executables and counter manifest.
- Produces per pass: raw PresentMon CSV, raw one-second engine samples, process CPU/working-set samples, thermal/frequency/limit samples, runtime metadata and player report/stats.

- [ ] Run native pass 1 fullscreen from file start. Warm 30 seconds, sample 120 seconds, stop by its exact PID, and retain its JSON report.
- [ ] Cool down for at least 90 seconds and record end temperature/frequency before the next pass.
- [ ] Run production pass 1 fullscreen from the same file start with Loudness Smooth/off. Confirm HEVC, `d3d11va-copy`, cache 100%, then warm/sample/stop identically.
- [ ] Repeat production pass 2 after cooldown, followed by native pass 2 after cooldown (ABBA order).
- [ ] Do not interact with or foreground unrelated applications during any measured window.

### Task 4: Reduce and sanity-check metrics

**Files:**
- Create: `%TEMP%/colosseum-efficiency-20260721/summary.json`

**Interfaces:**
- Consumes: four raw pass folders.
- Produces: pass-level and contender-level means/medians with fixed formulas.

- [ ] For each one-second sample, sum target-PID instances within `Copy`, `3D`, `VideoDecode`, and `VideoProcessing`; define target `GPU busiest-engine` as the maximum of those class sums, not their total.
- [ ] Normalize process CPU to the eight-logical-CPU machine: CPU-time delta divided by wall time and eight, times 100.
- [ ] Report mean and p95 private working set, PresentMon displayed-frame interval median/p95, displayed/dropped counts, and GPU duration divided by capture wall time when available.
- [ ] Report CPU-zone temperature in Celsius, actual frequency, performance-limit percent/flags, and explicitly mark package watts unavailable if the power counter is zero.
- [ ] Compare the two passes for variance. Investigate any pass-order reversal or more than 15% relative spread before averaging.

### Task 5: Write the decision

**Files:**
- Modify: `docs/superpowers/specs/2026-07-20-kodi-windows-video-architecture-decision.md`

**Interfaces:**
- Consumes: measured summary and Hemanth's fixed bar.
- Produces: reproducible numbers table, caveats and one-line go/no-go decision.

- [ ] Append method, artifact hashes, pass table, contender means, percentage deltas, present pacing, thermal/throttle observations and raw-artifact paths.
- [ ] State that production CPU/memory include audio, demux and subtitles while native does not; treat their delta as an upper bound, not a render-only saving.
- [ ] Give Copy-engine traffic special weight as the fairest removed-work metric, but judge the arc on whole-path GPU/CPU/thermal impact against the fixed threshold.
- [ ] End with exactly one plain verdict: proceed only if the measured win clears 25–30% or thermal evidence is clear; otherwise permanently shelve the native-backend arc.
- [ ] Run placeholder scan, `git diff --check`, verify calculations from raw samples, and commit only the plan and decision document by explicit pathspec.
