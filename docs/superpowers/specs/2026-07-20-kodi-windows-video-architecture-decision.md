# Windows video architecture after the Kodi/D3D11 experiment

**Date:** 2026-07-20  
**Owner:** [Agent 4 (Codex), player]  
**Decision (amended 2026-07-21):** pursue a staged, feature-flagged native Windows backend; keep
mpvqt as the shipped default and fallback until the native path passes the full player contract

## Executive decision

Colosseum should keep its current mpvqt player after the normalization fix. The original stutter
was causally recovered by removing always-on dynamic `loudnorm`, while the same mpvqt,
`d3d11va-copy`, and Qt Quick composition remained. Replacing the player would therefore be a
high-risk response to a bug already fixed at its primary cause.

The Kodi study was still worthwhile. The standalone prototype proves on the exact Intel UHD 620
laptop that a native Windows path is technically viable: FFmpeg can decode the real 1080p HEVC
Wire file as D3D11 P010, a D3D11 video processor can convert it into a shared RGBA8 ring, and Qt
Quick can import and composite that texture with ordinary QML without a CPU frame transfer. This
removes uncertainty from a future architecture choice, but it does not prove a complete player's
audio/video clock, seeking, subtitles, HDR, device recovery, or maintainability.

The 2026-07-21 efficiency gate changes the disposition of the prototype, not the shipped-player
decision. On the target laptop, the native path averaged 87.4% lower per-process GPU busy and
89.3% lower measured process CPU than production mpvqt across an ABBA repeat. That clears
Hemanth's 25-30% bar by a wide margin, so the native backend should now advance as a staged arc.
mpvqt remains production until native earns feature parity and cross-hardware confidence.

## What Kodi actually contributes

Kodi is not a single magic rendering call. Its Windows player separates five contracts:

1. **Decoder surfaces.** DXVA selects NV12/P010 formats and creates decoder output views
   (`xbmc/cores/VideoPlayer/DVDCodecs/Video/DXVA.cpp:159-160`, `:577-639`).
2. **Share or copy policy.** The true-shared route uses `D3D11_RESOURCE_MISC_SHARED` and a legacy
   DXGI shared handle (`DXVA.cpp:601-619`). When direct sharing is unsuitable, `CVideoBufferCopy`
   owns a one-surface shared fallback and performs `CopySubresourceRegion` (`DXVA.cpp:937-1006`).
3. **Explicit cross-device ordering.** Kodi creates a shared D3D11 fence, exports it, opens it on
   the application device, and waits for its value (`DXVA.cpp:829-841`, `:896-903`).
4. **Video processing.** Its DXVA-HD layer constructs decoder input/output views, sets source and
   color-space state, then calls `VideoProcessorBlt`
   (`VideoRenderers/HwDecRender/DXVAHD.cpp:271-371`, `:510-520`).
5. **Presentation policy.** `RenderManager` owns separate free, queued, discard, and presenting
   states, selects frames against the player clock, and accounts for lateness
   (`VideoRenderers/RenderManager.cpp:202-232`, `:1130-1276`).

The prototype implements the first four in a narrow form and only a small PTS-paced subset of the
fifth. Kodi's queue/cadence layer is the larger architectural lesson: a GPU bridge is necessary for
a native player, but it is not itself a player.

## Colosseum and prior Tankoban paths

Production Colosseum deliberately selects Qt Quick's OpenGL backend because mpvqt renders there
(`native/main.cpp:205-211`). `MpvItem` subclasses mpvqt's `MpvAbstractItem`
(`native/player/mpvitem.h:1-16`) and currently requests `hwdec=auto-safe`
(`native/player/mpvitem.cpp:74`), which resolves to `d3d11va-copy` on this laptop. The previous
audit showed that this path became nearly smooth when only dynamic loudnorm was removed; the
normalization fix now defaults to off and exposes light/full choices
(`native/player/mpvitem.cpp:63-69`, `:321-336`; `qml/PlayerPage.qml:42-59`).

Tankoban's earlier sidecar is not a safe transplant. Its presenter copies a decoder slice into a
shared BGRA texture and uses `Flush` plus WGL lock/unlock synchronization
(`C:/Users/Suprabha/Desktop/Tankoban 2/native_sidecar/src/d3d11_presenter.cpp:76-112`,
`:138-147`). The new prototype instead proves two D3D11 devices on the same adapter, a three-slot
ownership state machine, bidirectional shared fences, and Qt's public D3D11 texture import. It has
no OpenGL/WGL or cross-process dependency.

## Experiment evidence

### Gate A: synthetic GPU producer

Report: `C:\Users\Suprabha\AppData\Local\Temp\colosseum-d3d11-gate-a.json`

- Qt and producer: Intel UHD Graphics 620, adapter match true
- shared fences true; CPU transfers 0; device errors 0
- 1,437 generated and 1,435 presented frames over 60 seconds
- moving GPU pattern remained visible under live QML through resize and fullscreen

Gate A also found a concrete Qt constraint. Legacy shared handles plus independent fences worked;
adding a keyed mutex without acquiring ownership produced black output. More importantly,
`QSGD3D11Texture::fromNative()` rendered the initial BGRA8 surface black and the otherwise identical
RGBA8 surface correctly. The presentation ring is therefore RGBA8 even though BGRA support remains
enabled on the producer device.

### Gate B: real Colosseum HEVC file

Media: `The Wire - S4E13 - Final Grades - 20260720_211141.mp4`, 1920x1080 HEVC at 23.976 fps.

The 15-second hardware smoke reported 352 decoded, 351 converted, 350 presented, zero dropped,
zero late, zero CPU transfers, and zero device errors. The visual gate showed correct P010 video
in windowed and fullscreen modes under the live QML overlay.

The controlled five-minute foreground/fullscreen run is the primary Gate B result. Report:
`C:\Users\Suprabha\AppData\Local\Temp\colosseum-d3d11-gate-b-300s-fullscreen.json`.

| Counter | Result |
|---|---:|
| decoded | 7,188 |
| converted / published | 7,187 |
| presented by Qt | 7,185 |
| repeated scene-graph updates | 1 |
| dropped for lack of a safe slot | 0 |
| late by more than one frame duration | 0 |
| CPU frame transfers | 0 |
| device errors | 0 |

The one decoded/published difference and two published/presented difference occurred at timed
shutdown while the pipeline drained; they did not accumulate during playback. Adapter identity
matched the Intel UHD 620, shared fences remained active, hardware format was `d3d11va`, input was
P010, and no software fallback occurred.

`ffprobe` reports this file as limited-range but leaves its color matrix, primaries and transfer
unspecified. The bridge therefore uses explicit metadata when present and the conventional BT.709
matrix fallback for untagged HD frames; otherwise an untagged 1080p source would incorrectly take
the SD matrix.

A separate five-minute run left behind other windows measured 7,163 decoded, 7,153 published,
6,990 presented, 9 slot drops and 52 late frames. At the 91-second checkpoint of the foreground
control, the corresponding counts were 2,181 decoded, 2,180 published and 2,179 presented with no
drops or lateness. The fullscreen completion stayed at zero. This isolates Windows/Qt occlusion
throttling as the reason the background result coalesced ready frames; it is not evidence that the
foreground bridge inherently loses 163 frames. It is still a production requirement: playback
must pause intentionally or define background cadence rather than assume every hidden Qt window
will be rendered at media rate.

The path is accurately described as **hardware decode plus one GPU conversion/composition path**,
not zero-copy. D3D11VA retains decoder surfaces on the GPU; `VideoProcessorBlt` writes a new RGBA8
surface; Qt Quick then composites that surface with the scene.

## Ranked options

| Rank | Option | Decision | Why | Risk / effort |
|---:|---|---|---|---|
| 1 | Keep mpvqt with normalization off by default | Adopt | Fixes the measured incident and preserves mature audio, subtitles, seeking, streaming, HDR and controls | Low / already shipped |
| 2 | Build a feature-flagged native Windows player from this bridge | Adopt as a staged arc | The bridge is proven and the efficiency gate below clears the fixed bar by more than 3x | Very high |
| 3 | Add better production playback instrumentation | Next | Establishes whether residual drops correlate with normalization mode, QML work, or thermal state | Low-medium |
| 4 | Preserve mpvqt as the Windows fallback during the arc | Adopt | Avoids trading efficiency for regressions while audio, subtitles, seek, streaming and device recovery are rebuilt | Low ongoing |
| 5 | Replace mpvqt immediately | Reject | No remaining evidence of architectural necessity after the loudnorm fix | Very high |

## Required next experiment: normalization versus dropped frames

Use the existing production Colosseum player, not this video-only prototype. Run the same local
Wire segment with the video path, window state, stats overlay, and thermal starting condition held
constant. Test the persisted modes in this order with a cooldown or randomized repeat:

1. `off` (no `af`) — control;
2. `light` (`dynaudnorm=m=15:s=9`);
3. `full` (`loudnorm=I=-14:TP=-1.5:LRA=11`).

For each mode, discard startup warm-up, then record `frame-drop-count`, `vo-drop-frame-count`,
`hwdec-current`, cache state, process CPU, and elapsed playback time over the same five-minute
interval. Repeat at least twice because the prior `hwdec=yes` run demonstrated scheduling/thermal
variance. This directly answers Hemanth's question: **do frames drop when audio normalization is
happening, and is the light mode cheap enough on the i5-8365U?**

Do not infer the answer from Gate B: Gate B has no audio decoder, audio clock, or normalization.

## Native arc entry and guardrails

The efficiency benchmark below clears the trigger for a staged native path even though loudness
normalization already fixed the original user-visible stutter. The next smallest production step
is a build-flagged Windows-only backend that reuses the proven ring while adding an audio master
clock and seek/flush contract. It must not begin with UI polish or wholesale mpv removal.

A real adoption decision must first prove:

- audio master clock, drift correction and underrun behavior;
- seek, flush, EOS and stream-switch state transitions;
- subtitle/overlay timing and HDR/color-space policy;
- device loss, display change and fullscreen recovery;
- streaming backpressure and cancellation;
- performance parity across at least the target Intel iGPU and a discrete GPU;
- FFmpeg/Kodi-derived licensing boundaries.

Until those gates exist, the prototype is evidence, not production architecture.

## Efficiency gate: native D3D11 versus production mpvqt (2026-07-21)

### Decision bar and verdict

Hemanth's fixed bar was to pursue the native arc only if it reduced steady-state GPU busy and/or
CPU by at least 25-30%, or demonstrated a clear package-power/thermal win. A single-digit
improvement meant shelving the arc permanently.

**Verdict: GO - pursue the staged native-backend arc.** Across two passes per contender, native
reduced per-process GPU busy by **87.4%** and measured process CPU by **89.3%**. It therefore clears
the decision bar without relying on memory, power, temperature, or the expected Copy-engine
signal. This is permission to build the backend behind a feature flag, not permission to remove
mpvqt before feature parity.

### Locked setup

- Machine: Intel i5-8365U (4 cores / 8 logical processors), Intel UHD Graphics 620 driver
  `31.0.101.2135`, Windows 11, 1920x1080 at 59 Hz, High performance power plan.
- Media: `The Wire - S4E13 - Final Grades - 20260720_211141.mp4`, HEVC Main 10,
  1920x1080, 24000/1001 fps, SHA-256
  `694F819864DD3DF9B9ABCDE2FF152E820F5E6C259E80D63673422F6FEBF594EC`.
- Source tree: commit `59d7dac84c0e8c4389508118e803406b3bf2a8f6`.
- Native executable: fresh out-of-tree rebuild, SHA-256
  `6A154FB934A7916BCB239293E997AD5E669740B76708AE09BA1FA9B665EED78E`.
- Production executable: fresh worktree rebuild, SHA-256
  `C5FF2F5F2CBCD37507404D814EB571D6C0A52BF7082D3404CB8E906E89281CEF`.
- Production configuration observed in the player: fullscreen, local file, cache 100%,
  `hwdec-current=d3d11va-copy`, Loudness `Smooth` (no normalization filter). Player diagnostics
  were hidden during sampling.
- Order: native 1, production 1, production 2, native 2 (ABBA), with at least 90 seconds of
  no-playback cooldown between contenders. Each run started the episode from the beginning,
  discarded at least 30 seconds of warm-up, then retained 120 one-second steady-state samples.
- Raw pass telemetry and prototype reports are retained under
  `C:\Users\Suprabha\AppData\Local\Temp\colosseum-efficiency-20260721\`.

### Results

GPU percentages come from per-PID `GPU Engine` counters. `GPU busy` is the busiest summed engine
class in each sample (the Task Manager definition of total GPU utilization), then averaged; 3D was
the busiest class in every pass. CPU is process processor time divided by eight logical
processors. Values below are arithmetic means of the two pass summaries; parenthesized values are
the range of the two pass means.

| Metric | Native D3D11 | Production mpvqt | Native delta | Decision weight |
|---|---:|---:|---:|---|
| GPU busy, mean | 6.45% (6.11-6.79) | 51.36% (51.00-51.72) | **87.4% lower** | Primary; clears bar |
| GPU busy, mean P95 | 7.16% | 54.35% | 86.8% lower | Primary; repeat is stable |
| GPU 3D, mean | 6.45% | 51.36% | **87.4% lower** | Primary render-path signal |
| GPU Copy, mean / P95 | 0.00% / 0.00% | 0.00% / 0.00% | no observed delta | Hypothesis killed by this instrument |
| GPU Video Decode, mean | 4.60% | 5.01% | 8.3% lower | Similar decoder cost |
| GPU Video Processing, mean | 4.34% | 0.00% | native adds this engine | Expected `VideoProcessorBlt` conversion |
| CPU, mean | 1.25% (1.03-1.47) | 11.67% (11.63-11.71) | **89.3% lower** | Clears bar, but upper-bound caveat applies |
| CPU, mean P95 | 2.31% | 13.56% | 83.0% lower | Secondary |
| Working set, mean | 264 MB | 1,193 MB | 77.9% lower | Context only; not a pipeline estimate |
| Private bytes, mean / mean P95 | 278 / 278 MB | 1,128 / 1,156 MB | 75.4% lower by mean | Context only; upper bound |
| CPU-zone temperature, mean | 77.35 C | 75.85 C | native 1.5 C higher | No thermal win established |
| Actual CPU frequency, mean | 2,219 MHz | 2,063 MHz | native 7.6% higher | Context; same throttle flags |
| Package power | unavailable | unavailable | - | No power claim |
| Present pacing fallback | 0 dropped / 0 late in both native reports | mpv `frame-drop-count` 143 / 135 at pass-end inspection | favors native, not cross-instrument latency | PresentMon unavailable |

Per-pass means, retained to expose thermal/order variance:

| Pass | GPU busy | GPU Copy | CPU | Working set | CPU zone | Actual frequency |
|---|---:|---:|---:|---:|---:|---:|
| Native 1 | 6.79% | 0.00% | 1.03% | 273.5 MB | 77.85 C | 2,218 MHz |
| Production 1 | 51.00% | 0.00% | 11.63% | 1,205.3 MB | 75.85 C | 2,061 MHz |
| Production 2 | 51.72% | 0.00% | 11.71% | 1,180.5 MB | 75.85 C | 2,065 MHz |
| Native 2 | 6.11% | 0.00% | 1.47% | 254.6 MB | 76.85 C | 2,220 MHz |

Production's two CPU and GPU means differ by less than 1.5% relative; native GPU differs by 10.5%.
Native CPU differs by 35% relative, but the absolute spread is only 0.44 percentage points
(1.03-1.47%) at the sampler's low end. Both native CPU passes independently clear the fixed bar
by more than 87%, so averaging does not hide a pass-order reversal or a threshold-edge result.

Both native reports retained `d3d11va`, P010 input, matching Intel adapters, shared fences and zero
CPU transfers, device errors, software fallbacks, dropped frames, or late frames. Native pass 1
ended at 5,185 decoded / 5,181 presented with one repeat; native pass 2 ended at 5,696 decoded /
5,692 presented with two repeats. The small decoded/presented tail difference is pipeline drain at
manual close, not accumulating loss.

### What the benchmark proves - and what it does not

1. **The efficiency win is in 3D/composition, not a separately visible Copy queue.** The expected
   smoking gun did not appear: Windows reported 0% Copy for both contenders. `d3d11va-copy` can
   move data through driver paths that are accounted to 3D or are not exposed as a dedicated Copy
   engine on this UHD 620. The Copy hypothesis is therefore killed for this counter set; the
   reproduced 51% versus 6% 3D delta is the evidence that supports the decision.
2. **CPU and memory are deliberately not treated as pure pipeline savings.** Production includes
   audio decode/output, demux, subtitles, the full QML player and application services; the
   video-only native process does not. The 89.3% CPU and 77.9% working-set deltas are upper bounds
   on what an integrated native player could save. The GPU result is fairer, though production also
   paints subtitles that the prototype omits.
3. **No package-power or thermal win is claimed.** PresentMon 2.3.0 was installed but could not
   create a privileged per-process ETW capture from this non-elevated session. No HWiNFO or Intel
   package-power source was installed, and `Power Meter(_Total)` returned invalid/zero data. The
   ACPI CPU-zone sensor was coarse and actually averaged 1.5 C warmer for native; throttle state
   was unchanged (`Performance Limit Flags=2`, `% Performance Limit=83`) in all passes.
4. **Present latency is not measured.** Because PresentMon was unavailable, native's internal
   counters and mpv's frame-drop counter are operational corroboration, not a common latency
   instrument. They strengthen the result but are not used to calculate the 87.4% decision metric.
5. **The result gates an arc, not a replacement.** A native player must still pay for audio clock,
   subtitles, seeking, streaming, HDR/color, device recovery and diagnostics. Re-run this same
   efficiency suite after each major feature tranche; if integration erodes the advantage below
   the fixed 25-30% bar, stop the arc and retain mpvqt.

## Definition of Done review

**Reviewer:** [Agent 4 (Codex), review] — self-review through the Brotherhood review discipline;
this is not an independent producer-versus-reviewer gate.

### Gate A

- **MET — D3D11 and adapter identity:** reports identify Qt and producer as Intel UHD Graphics 620
  with matching adapter identity and active shared fences.
- **MET — composited visual:** the moving GPU pattern was observed below live translucent QML.
- **MET — 60-second stability:** 1,437 generated / 1,435 presented, zero device errors and no
  deadlock or corrupt frame.
- **MET — window transitions:** resize, windowed and fullscreen rendering were exercised.
- **MET — synchronization/no CPU transfer:** both fence counters advanced and `cpuTransfers=0`.

### Gate B

- **MET — ordering:** real media work began only after Gate A passed.
- **MET — hardware decode:** the real file reports HEVC, `d3d11va`, P010, 1920x1080.
- **MET — GPU conversion:** source and contract test require `VideoProcessorBlt`; no transfer or
  software-upload API is present.
- **MET — five-minute visual run:** 7,188 decoded / 7,187 published / 7,185 presented fullscreen,
  with zero dropped, late, device-error and CPU-transfer counters.
- **MET — timing telemetry:** PTS pacing and decoded/converted/published/presented/repeated/late/
  dropped counters are implemented and recorded.
- **MET — architectural comparison:** this document compares Kodi, production Colosseum and the
  prior Tankoban sidecar without claiming production readiness.

### Deliverables and scope

- **MET — isolated prototype:** all executable source is under
  `native/prototypes/d3d11_qtquick_bridge/`; no production player target or QML was modified.
- **MET — reproducibility:** the adjacent README contains proven configure, build, deploy, test and
  run commands plus counter definitions and limitations.
- **MET — decision:** this ADR ranks the choices, records both controls, and names the next smallest
  step and the audio-normalization A/B.
- **MET — anti-scope:** no audio, subtitle, seek, streaming, HDR, OpenGL, child-window, CPU-transfer
  or software-decode path was added; Kodi code was not copied.

Review edge: the verification plan requested deleting the original untracked build directory, but
the command safety layer refused recursive deletion after path verification. Verification instead
used a never-before-existing out-of-tree directory under `%TEMP%`, which is stronger evidence that
the committed source configures and builds independently and leaves the Git worktree clean.

### Efficiency amendment review

- **MET - committed artifacts:** source SHA, executable hashes and media hash were rechecked after
  the runs; the prototype and production committed trees rebuild without work pending.
- **MET - balanced capture:** four pass files each contain 120 valid samples in ABBA order, with
  fullscreen start-from-beginning playback, warm-up and cooldown controls documented.
- **MET - calculations:** raw pass means reproduce the reported 87.437887% GPU reduction; CPU,
  memory, engine, frequency, temperature and throttle values are retained per pass.
- **MET - fairness:** the document labels production CPU/memory as upper bounds, gives GPU 3D the
  primary decision weight, and kills rather than invents the absent Copy-engine signal.
- **PARTIAL - common pacing and package power:** PresentMon could not open a privileged ETW
  capture, package watts were unavailable, and the ACPI temperature counter was coarse. These
  fields are marked unavailable and are not used to reach the verdict.
- **MET - fixed decision:** the one-line `GO` is evaluated against Hemanth's 25-30% bar and keeps
  mpvqt as fallback until the feature contract is rebuilt and the efficiency win revalidated.
- **MET - scope:** only this decision document changed; production player source is untouched.

**APPROVE - the original prototype gates and the efficiency amendment meet their written decision
requirements; begin the staged feature-flagged arc, but do not replace mpvqt yet.**
