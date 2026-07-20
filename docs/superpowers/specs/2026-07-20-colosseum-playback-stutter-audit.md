# Colosseum playback-stutter audit

**Date:** 2026-07-20  
**Scope:** Colosseum 1080p HEVC playback on Intel i5-8365U / UHD 620 / Windows 11  
**Author:** [Agent 4 (Codex), player audit]

## Verdict

The catastrophic stutter is caused primarily by Colosseum's always-on dynamic
`loudnorm` audio filter, not by the downloaded file, cache, HEVC decode capability,
default video scalers, or the mere fact that video is composed through Qt Quick.

Removing only `loudnorm` changed the same local S4E13 sample from roughly 18.5-19.4
dropped frames per second to 29 total drops at 51 seconds, then 53 total at 81 seconds.
That is about a 96% reduction in steady-state drops while `hwdec-current` remained
`d3d11va-copy` and cache remained 100%.

FFmpeg's independent audio-only benchmark explains the result. Processing 30 seconds
of the file's 48 kHz 5.1 AAC track took 0.234 s user CPU and 30 MiB peak RSS without a
filter. Dynamic `loudnorm` produced 192 kHz 5.1 output and took 15.297 s user CPU and
211 MiB peak RSS: approximately 65 times the user CPU time and seven times the peak
memory. FFmpeg documents that dynamic loudnorm upsamples to 192 kHz to detect true
peaks accurately.

The correct first fix is therefore a user-visible audio-normalization choice that can
disable dynamic loudnorm for smooth playback. It must not be silently removed because
the normalization was a deliberate product choice. Replacing mpvqt or the Qt Quick
player architecture is not justified by this bug.

## Reproduction and controls

### Test media

`The Wire - S4E13 - Final Grades - 20260720_211141.mp4`

- fully downloaded local file
- HEVC, 1920x1080, 23.976 fps
- approximately 4.81 Mbps total bitrate
- AAC 5.1, 48 kHz primary audio
- 78.7 minutes

This matches the reported 1080p HEVC workload closely. The completed S4E2-S4E11 files
have the same video format and approximately 4.73-4.83 Mbps bitrates.

### Procedure

Each Colosseum variant was compiled with `native/build-msvc.bat` into the same
`native/build-msvc` directory. Before each rebuild, the running `colosseum.exe` was
stopped by its exact PID. The resulting executable was launched, the same local S4E13
file was started from the beginning, and Colosseum's own Playback stats card supplied:

- `frame-drop-count`
- `vo-drop-frame-count`
- `hwdec-current`
- `cache-buffering-state`

The second displayed drop counter was unavailable in this build and rendered as
`NaN`; all comparisons below use the first counter consistently. Cache was 100% in
every measured run, so download/network starvation is excluded.

### Results

| Variant | Only changed variable | Observed decoder | Drop observation | Result |
|---|---|---|---:|---|
| Baseline | none | `d3d11va-copy` | 718 at 37 s; +554 over the next 30 s | about 18.5 drops/s steady-state |
| Harbor-style performance flags | bilinear scale/cscale/dscale, `vd-lavc-fast=yes`, interpolation/deband/dither/HDR peak off | `d3d11va-copy` | 1,011 at 52 s | about 19.4 drops/s; no recovery |
| No loudnorm | removed only `af=loudnorm=I=-14:TP=-1.5:LRA=11` | `d3d11va-copy` | 29 at 51 s; 53 at 81 s | about 0.8 drops/s after startup; about 96% lower |
| `hwdec=yes` | changed only `auto-safe` to `yes`; loudnorm retained | `d3d11va-copy` | 220 at 38 s; 445 at 68 s | improved run, but still about 7.5 drops/s and same decoder |

The `hwdec=yes` measurement must not be interpreted as proof that `yes` selects a
different decoder. Current mpv documentation states that `yes`, `auto`, and
`auto-safe` are aliases, and the runtime decoder remained `d3d11va-copy`. The measured
difference is therefore run variance, scheduling/thermal state, or startup state—not
a defensible configuration fix.

### Independent audio benchmark

Both commands decoded the first 30 seconds of the primary audio track to FFmpeg's null
output. Video was disabled.

| Audio path | Output | User CPU | System CPU | Wall time | Peak RSS |
|---|---|---:|---:|---:|---:|
| no filter | PCM 48 kHz 5.1 | 0.234 s | 0.188 s | 0.508 s | 30,060 KiB |
| dynamic loudnorm | PCM 192 kHz 5.1 | 15.297 s | 0.547 s | 18.646 s | 210,584 KiB |

This is an audio-only reproduction of the expensive work that competes with mpv's
video timing on a 15 W four-core ULV processor.

## Hypothesis verdicts and ranked causes

### 1. Confirmed primary cause: always-on dynamic loudnorm

**Confidence: high. Risk of fix: low technically, product-choice sensitive.**

Colosseum installs dynamic loudnorm unconditionally in
`native/player/mpvitem.cpp:70`. The adjacent source comment already records the
internal 192 kHz resample as an accepted trade. FFmpeg confirms that dynamic mode
upsamples to 192 kHz, and the local benchmark quantifies the cost on the exact audio
track. Harbor and Stremio do not install this filter.

The controlled app A/B is causal: removing only this property leaves the file, cache,
decoder, mpvqt renderer, Qt scene, subtitles, and hardware unchanged, while nearly all
drops disappear.

Primary source: [FFmpeg loudnorm documentation](https://ffmpeg.org/ffmpeg-filters.html#loudnorm).

### 2. Secondary headroom cost: copy decoding plus FBO composition

**Confidence: medium as a residual contributor; disproven as the primary regression.  
Risk of architectural fix: high.**

Colosseum forces Qt Quick to OpenGL in `native/main.cpp:210-211`. KDE mpvqt's
`MpvAbstractItem` is a `QQuickFramebufferObject`; its renderer calls
`mpv_render_context_render()` into a `QOpenGLFramebufferObject`, after which Qt Quick
samples/composites that texture. Qt documents the texture-item approach as more
expensive than an inline or underlay renderer because it adds a render target and
render pass.

`d3d11va-copy` also copies decoded frames back to system memory. mpv documents copy
hardware decoders as less efficient than direct modes. In Colosseum's OpenGL/libmpv
path, direct `d3d11va` requires a compatible D3D11/ANGLE output context; changing only
the hwdec preference did not provide that context and did not change
`hwdec-current`.

However, two pieces of evidence kill this as the root cause:

1. With loudnorm removed, the same Colosseum FBO and `d3d11va-copy` path becomes
   nearly smooth.
2. Stremio's official Qt shell also subclasses `QQuickFramebufferObject`, renders mpv
   into an OpenGL FBO with `mpv_render_context_render()`, and asks Qt Quick to update
   for each frame. Stremio is smooth on the same machine and does not configure
   loudnorm.

This path reduces performance margin, making loudnorm's CPU spike visible sooner, but
replacing mpvqt would treat the amplifier instead of the cause.

Primary sources:

- [Qt texture-item performance tradeoff](https://doc.qt.io/qt-6/qtquick-scenegraph-rhitextureitem-example.html)
- [Qt Quick scene-graph integration approaches](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)
- [mpv hardware-decoding documentation](https://mpv.io/manual/stable/#options-hwdec)
- [Stremio's FBO/libmpv implementation](https://github.com/Stremio/stremio-shell/blob/master/mpv.cpp)

### 3. Disproven for this workload: default video scalers and missing performance profile

**Confidence: high for native 1080p playback. Risk of optional profile: low.**

Harbor's performance tier sets bilinear scale/cscale/dscale, enables
`vd-lavc-fast=yes`, and disables interpolation, deband, dithering, and HDR peak
computation. Applying the same eight flags to Colosseum did not improve the test:
1,011 drops at 52 seconds versus baseline's approximately 18.5 drops/s.

This result is expected for a 1920x1080 source displayed on a 1920x1080 surface:
expensive scaling is not the active bottleneck, while Colosseum already leaves
interpolation and deband at mpv defaults. A performance profile remains useful for
4K, HDR, scaling, or future quality features, but it does not fix the reported file.

### 4. Disproven as a distinct lever: `hwdec=auto-safe` versus `yes`/`auto`

**Confidence: high. Risk of changing the string: low but no benefit.**

Current mpv documentation defines `yes`, `auto`, and `auto-safe` identically. Both
tested values resolved to `d3d11va-copy`. Therefore changing the string does not
remove the copy path and should not be presented as a fix.

A true direct-D3D11 experiment would require changing the rendering/interop context,
not the hwdec alias. That crosses the mpvqt/OpenGL architecture boundary and belongs
in a separate prototype, not this repair.

### 5. Not causal: cache and download buffering

**Confidence: high.**

The file is complete and local. Colosseum reported 100% cache in every run. Its large
cache/demuxer settings at `native/player/mpvitem.cpp:46-51` are unnecessary for the
local test but did not starve playback. Changing them would not address the measured
CPU bottleneck.

## Reference-player comparison

### Harbor

Harbor's Windows path sets a native mpv `wid` and does not use its libmpv render API
path on Windows. That avoids Qt-style per-frame framework composition. Its optional
performance tier also lowers video cost, and it has no always-on loudnorm.

Harbor therefore has more performance headroom, but the A/B shows that its scaler
profile is not the decisive difference for this native-resolution file. The decisive
difference is the absent audio filter.

### Stremio

Stremio is the stronger control for the composition hypothesis. Its official Qt shell
uses the same fundamental QQuickFramebufferObject/OpenGL FBO/libmpv render pattern as
Colosseum, yet it is smooth on this hardware. Its mpv setup contains no loudnorm. This
demonstrates that Qt/FBO composition is compatible with smooth playback on the laptop
when the audio pipeline is not consuming the remaining CPU budget.

## Concrete fixes, ordered by value

### Fix A: user-visible normalization mode

**Recommendation: ship after a product-default decision.  
Effort: small-medium. Technical risk: low. Product risk: medium.**

Add a persisted player preference with at least:

- **Off / Smooth playback:** no `af=loudnorm`; this is the measured recovery path.
- **EBU R128 normalization:** current `loudnorm=I=-14:TP=-1.5:LRA=11` behavior, with a
  note that it is CPU-intensive on older/low-power processors.

QML should render the choice; C++ should own and apply the mpv filter. Do not build the
filter string in QML. Changing modes while playing should use mpv's `af` property and
be verified for a clean filter reinitialization.

Default options for Hemanth's call:

1. Preserve current normalization by default and expose Smooth playback as an opt-in.
   This preserves the deliberate loudness choice but leaves affected users stuttering
   until they discover the setting.
2. Default new installs to Smooth playback, preserving normalization as an explicit
   opt-in. This prioritizes reliable playback and matches Harbor/Stremio behavior.
3. Prompt once on first playback or after detecting sustained drops. This is more
   complex and should not silently switch audio behavior mid-session.

No code change in this audit chooses among those product semantics.

### Fix B: lightweight performance profile

**Recommendation: safe optional feature, not the fix for this incident.  
Effort: small. Risk: low.**

Expose a C++-owned profile containing Harbor's eight low-cost flags. It may help scaled,
4K, HDR, or future quality-filter workloads. Do not claim it fixes the measured 1080p
case; the A/B did not recover frames.

### Fix C: keep the current hwdec preference

**Recommendation: no change.**

Changing `auto-safe` to `yes` or `auto` is an alias-only edit in current mpv and does
not alter `hwdec-current`. A direct D3D11 path requires a render-backend prototype and
should be evaluated only after Fix A on harder content.

### Fix D: lighten the Qt Quick playback scene only if residual drops remain

**Recommendation: defer. Effort: medium. Risk: medium.**

PlayerPage contains no `ShaderEffect`, `MultiEffect`, `Canvas`, `FrameAnimation`, or
enabled layer on the main dock; it even explicitly avoids `layer.enabled` near line
4271. Most timers are conditional or low frequency. Safe residual work would be:

- suspend nonessential timers while chrome is hidden;
- keep transient overlays invisible and non-updating during steady playback;
- measure with the stats overlay closed, because the overlay itself repaints at 1 Hz;
- profile scene-graph batches and render-thread time with `QSG_INFO`/Qt rendering logs.

These are margin improvements, not substitutes for disabling the confirmed filter.

### Fix E: replace or bypass mpvqt embedding

**Recommendation: do not pursue for this bug. Effort: high. Risk: high.**

Native child-window embedding, an OpenGL underlay, a custom `QSGRenderNode`, or a D3D11
renderer could remove a render pass or enable direct D3D11 interop. Each option changes
load-bearing composition, overlay ordering, clipping, fullscreen behavior, and input.
Stremio proves that the existing FBO architecture can be smooth, so this rewrite lacks
a supporting necessity after the loudnorm A/B.

## Ship classification

| Change | Classification |
|---|---|
| Add explicit Off / EBU R128 normalization preference | Safe implementation shape; needs product-default call |
| Default normalization Off for new installs | Technically safe; deliberate user-choice change, needs Hemanth |
| Harbor-style performance profile | Safe optional follow-up; not incident fix |
| Change `auto-safe` to `yes`/`auto` | Do not ship as a fix; aliases |
| Hide/suspend nonessential QML work during playback | Safe only after profiling identifies work |
| Replace mpvqt/FBO or force direct D3D11 | Load-bearing architecture; separate prototype and review |

## Definition of done check

- Same local 1080p HEVC file reproduced with cache at 100%: met.
- Colosseum, Harbor, and Stremio render/config paths compared: met.
- Four requested hypotheses confirmed or killed with evidence: met.
- Performance-profile, loudnorm, and hwdec changes isolated: met.
- Root causes ranked with concrete risk/effort fixes: met.
- Deliberate loudnorm choice preserved in source: met.
- No architectural or product-default change made without user direction: met.

