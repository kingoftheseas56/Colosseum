# Kodi-inspired D3D11-to-Qt Quick prototype design

**Date:** 2026-07-20  
**Owner:** [Agent 4 (Codex), player]  
**Status:** Approved by Hemanth through the 2026-07-20 free-reign commission

## Purpose

Determine whether a future Colosseum-native Windows video path can retain decoded frames on the
GPU and present them inside the Qt Quick scene without mpvqt's OpenGL render target or a native
child window. The experiment must answer the D3D11/Qt boundary question before it grows into a
second player.

The prototype is disposable and isolated. It does not replace `MpvItem`, change `PlayerPage.qml`,
or alter the production `colosseum.exe` target.

## Decisions

1. Use a dedicated D3D11 producer device on the same DXGI adapter as Qt Quick's D3D11 device.
2. Transfer ownership through a three-slot shared-texture ring, not CPU readback.
3. Synchronize producer and consumer explicitly with shared D3D11 fences.
4. Import consumer-side `ID3D11Texture2D` objects through Qt 6.11's public
   `QNativeInterface::QSGD3D11Texture::fromNative()` API.
5. Prove the bridge with GPU-generated RGBA frames before adding FFmpeg.
6. If the bridge passes, decode the completed Colosseum S4E13 HEVC file with FFmpeg D3D11VA,
   convert NV12/P010 through the D3D11 video processor into the same shared RGBA ring, and present
   video at source PTS. Audio is deliberately out of scope.

## Architecture

### Qt consumer

The standalone Qt Quick executable selects the D3D11 scene-graph backend before constructing its
window. A custom `QQuickItem` obtains Qt's `ID3D11Device` on the render thread, opens the producer's
shared texture and fence handles, wraps each texture as a `QSGTexture`, and displays the selected
slot through a `QSGSimpleTextureNode`. Ordinary QML content is drawn over the item to prove that the
video remains part of the scene graph.

The consumer waits on the producer fence before sampling a newly published slot. It releases the
previous slot only after a frame using the replacement slot has been submitted, signalling the
consumer fence from the render thread. Native resources are created and destroyed with the scene
graph and never manipulated from QML.

### Stage 1 producer

The producer creates a D3D11 device with BGRA and video support on Qt's adapter. It owns three
shareable RGBA render-target textures. A worker thread generates a moving diagnostic pattern using
GPU clears, signals the producer fence, and publishes the ready slot plus sequence number. It never
reuses the currently displayed slot and waits for the consumer fence before reusing a released one.

### Stage 2 HEVC producer

The second stage reuses the ring and synchronization unchanged. FFmpeg demuxes and decodes only the
video stream with a D3D11VA hardware context backed by the producer device. Each decoded
`AV_PIX_FMT_D3D11` frame supplies a decoder texture and array slice. A D3D11 video processor reads
the NV12/P010 input view, applies the source color-space metadata, and writes RGBA into a free shared
slot. The worker publishes frames according to their PTS using a monotonic clock.

The test file is:

`C:\Users\Suprabha\Downloads\Colosseum\The Wire - S4E13 - Final Grades - 20260720_211141.mp4`

## Observable telemetry

The QML overlay and log report:

- Qt graphics API and adapter identity;
- producer adapter identity and whether shared fences are active;
- source codec, pixel format, dimensions and frame rate;
- decoded, converted, published, presented, repeated and late/dropped frame counts;
- whether any CPU frame transfer occurred.

The counters distinguish decode/convert failures from presentation misses. A statement of
"zero-copy" is forbidden: the intended path avoids GPU-to-CPU transfer but includes a D3D11 video
processor pass from the decoder surface into a shareable RGBA presentation texture.

## Failure handling

The prototype fails loudly and stops the affected stage when:

- Qt is not using D3D11;
- the producer and consumer adapters do not match;
- D3D11.4 shared fences are unavailable;
- a texture or fence handle cannot be opened on the consumer device;
- FFmpeg does not return D3D11 frames;
- the video processor cannot accept the decoded input format or produce RGBA.

It does not silently fall back to software decode, CPU upload, mpv, OpenGL, or a child window,
because any such fallback would invalidate the experiment.

## Verification gates

### Gate A: synthetic bridge

- The executable reports Qt D3D11 and matching adapter LUIDs.
- The moving diagnostic pattern is visible beneath live QML overlays.
- At least 60 seconds run without device errors, deadlock, corruption or stale frames.
- Resize and fullscreen transitions preserve rendering.
- Producer/consumer fence counters advance and CPU frame-transfer count remains zero.

### Gate B: real HEVC

- Gate A has passed first.
- FFmpeg identifies HEVC and returns `AV_PIX_FMT_D3D11` frames.
- The D3D11 video processor converts decoder surfaces without CPU download.
- S4E13 is visibly correct beneath QML overlays for at least five minutes.
- Presentation follows source PTS; decoded, presented and late/dropped counters are recorded.
- The result is compared with Colosseum/mpvqt and Kodi's contracts without claiming production
  readiness from a video-only experiment.

## Deliverables

1. An isolated, buildable prototype under `native/prototypes/d3d11_qtquick_bridge/`.
2. A reproducible build/run instruction alongside the prototype.
3. An architectural decision document under `docs/superpowers/specs/` that maps Kodi, Tankoban and
   Colosseum, records both gate results, and recommends retain/prototype/adopt/reject with risks.

## Non-goals

- No production player integration.
- No audio, subtitles, seeking, streaming, HDR output or full player controls.
- No Kodi source copying; Kodi is GPL-2.0 and is used only as an architectural reference.
- No claim that RGBA presentation is the final HDR/color-management architecture.
