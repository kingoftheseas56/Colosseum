# Windows video architecture after the Kodi/D3D11 experiment

**Date:** 2026-07-20  
**Owner:** [Agent 4 (Codex), player]  
**Decision:** retain mpvqt for production; preserve the proven D3D11 bridge as a contingency, not a replacement player

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
| 2 | Keep this D3D11 bridge as a measured contingency | Preserve, do not integrate | The difficult Qt/D3D interoperability is proven on target hardware | Low now; no runtime impact |
| 3 | Add better production playback instrumentation | Next | Establishes whether residual drops correlate with normalization mode, QML work, or thermal state | Low-medium |
| 4 | Build a feature-flagged native Windows player | Conditional only | Could remove `d3d11va-copy` and OpenGL/FBO overhead, but recreates a full media player's timing and feature surface | Very high |
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

## Trigger for revisiting a native player

Reopen the native path only if controlled production measurements still show material sustained
drops with normalization off (or if a future requirement cannot be met through libmpv). The next
smallest production step would then be a build-flagged Windows-only spike that reuses the proven
ring while adding an audio master clock and seek/flush contract. It should not begin with UI polish
or wholesale mpv removal.

A real adoption decision must first prove:

- audio master clock, drift correction and underrun behavior;
- seek, flush, EOS and stream-switch state transitions;
- subtitle/overlay timing and HDR/color-space policy;
- device loss, display change and fullscreen recovery;
- streaming backpressure and cancellation;
- performance parity across at least the target Intel iGPU and a discrete GPU;
- FFmpeg/Kodi-derived licensing boundaries.

Until those gates exist, the prototype is evidence, not production architecture.
