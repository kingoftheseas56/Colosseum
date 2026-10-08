# Controlled HTML/mpv experiment: verified results and remaining gap

## Decision

**Not accepted as an end-to-end codec-bypass implementation.** The isolated adapter and interaction contract work, but native picture delivery is unstable on the tested software-rendered cloud desktop. Black frames remain a core failure, not a cosmetic issue. No production files, player routing, canonical state or provider integration were changed.

## What was actually verified

- The existing `MpvItem` and its three companion files compile unchanged in the isolated target.
- Native Qt pointer clicks enter the real HTML controls with trusted click events, cross WebChannel, and change cached native play/pause/seek/volume state.
- The final Qt 6.8.2 diagnostic run completed in 26.3 seconds with 28/28 native/HTML assertions. These include source replacement, interrupted close/reopen, EOF, missing-file error, reopening after error, document reload, stale document/session rejection, forbidden URL/action rejection, and source-switch volume-display synchronization.
- The facade publishes cached MpvItem getters at 10 Hz. Blocking diagnostic reads happen only at explicit finite test checkpoints.
- A prior Qt 6.8.2 / MpvQt 1.1 run produced genuinely different visible H.264 frames at native positions 1 and 3 seconds. Their central image mean absolute differences were approximately 14.5, 14.8 and 18.2 per RGB channel.
- That prior run also showed the HTML menu changing pixels, caption off/on changing and restoring pixels, and the unobstructed paused video remaining exactly unchanged. Resize, scroll, zoom and controlled fullscreen preserved varied native video in those captures.
- The separate transparency probe established that a page transparent from its first paint can expose an underlying Qt Quick scene while retaining HTML controls. This does not by itself validate the mpv FBO integration.

## Core failure

The final Qt 6.8.2 candidate passed only **14/28 pixel checks**. Initial H.264, FFV1, and several transformed/native-only captures were black or nearly uniform, even though native decoded dimensions were 640×360, positions advanced, codec diagnostics identified the expected streams, and estimated frame numbers changed. The black native-only captures rule out the HTML aperture as the complete explanation.

Some reopen captures displayed video while earlier captures in the same configuration did not. That inconsistency is why the earlier successful H.264 images are recorded as a limited observation, not reliable acceptance evidence.

The final executable has no SIGSEGV interception and no forced 33 ms repaint timer. Its SHA-256 is:

`5c975263169e6cbb7e708d3fa1b91577229f30a3ba8376e75150fb263725f508`

## Dependency comparisons

1. **MpvQt 1.1.0, Qt 6.8.2:** native/HTML interactions passed after fixes; one run visibly rendered H.264, but FFV1 replacement and document-reopen frames were black.
2. **MpvQt 1.1.0, Qt 6.11.1 runtime with a 6.8.2-compiled executable:** replacement frames remained black and a later recreation crashed in `mpv_render_context_render`.
3. **Unmodified upstream MpvQt `19a92995156b006ee42e4f25c29e1f2cf3e1a336` (1.1.1 / SONAME 2), Qt 6.8.2:** lifecycle/control tests completed, but 14/28 pixel checks still failed.
4. **Same upstream revision plus an isolated diagnostic GL-state-reset patch, Qt 6.8.2:** 28/28 native/HTML assertions and 14/28 pixel checks. The patch did not resolve the rendering failure and is not recommended as the default.

Use the unmodified pinned upstream wrapper for further reproduction. The original MpvQt 1.1 prefix, unmodified newer prefix, patched diagnostic prefix, and their failed-run evidence were kept separate. No Qt or Chromium engine was rebuilt. Runtime substitution is explicitly different from compiling with a Qt 6.11 SDK.

## Genuine browser codec baseline

The HTMLVideoElement baseline reads the same fixed local fixture bytes from a controlled local-file page. The earlier qrc-origin attempt was rejected before media loading and was excluded as codec evidence.

- **Qt 6.8.2 WebEngine build:** H.264/AAC produced 640×360 video, nine decoded video frames, advancing time and no media error in the initial measured interval.
- **Qt 6.11.1 WebEngine runtime:** H.264/AAC failed with `DEMUXER_ERROR_NO_SUPPORTED_STREAMS` and no video frames in the measured build. This is build-specific evidence, not a claim that every Qt 6.11 build lacks H.264.
- **FFV1/PCM:** the browser media clock advanced for roughly four seconds while video dimensions and decoded video-frame count stayed zero. This is consistent with audio-only playback, not successful FFV1 video playback. Audible output was not checked.

The native mpv path identified H.264/AAC and FFV1/PCM, populated decoded dimensions and audio-output parameters, and advanced audio PTS. Its audio output was deliberately `null`, so decoding evidence must not be described as audible playback.

## Evidence interpretation

`outcomes.json.passed` means native/HTML assertion success only. Its `passScope` and `pixelVerificationRequired` fields make that explicit. `verify_pixels.py` is required and emits `pixel-checks.json`, whose overall result combines those assertions with picture verification.

The pixel verifier requires:

- varied native video before hiding WebEngine, while WebEngine is hidden, and after restoration
- two visibly different sought frames
- actual menu and caption pixel changes while paused
- unchanged unobstructed video during those overlay changes
- varied video and matching DOM/native geometry after resize, scroll, zoom and fullscreen
- visible video after document reopen

Screenshots, positive dimensions and clocks cannot substitute for those checks. The final failures remain failures even when the application exits zero.

## Reproduction boundaries

There is no MSE/DRM path, arbitrary website adapter, remote provider URL, provider credential or production state in this experiment. Browser codec support is not expanded by changing this controlled page. The proposed mechanism is native composition underneath a cooperating HTML UI, and its actual video path remains unaccepted until the rendering instability is explained and repeatably fixed.

The same four native source-file SHA-256 values remain:

- `mpvitem.h`: `febeb782c9d1bb12166c5fabf82e7426cbfffa82070d1f08a5f545c9b0c856ad`
- `mpvitem.cpp`: `31ba0fd23a5880f6df85d927e6a09f825ebea731d3c8029640a12a90ad97c4a3`
- `mpvproperties.h`: `17b3d6211dff086d17a9cb50cd4a7c41a6bbfd9a094c7e3792b3980bef130f84`
- `http_header_fields.h`: `6028c4f5642f519a9dff0d50ec38d22df8a549da50dcd006a8bb7f670ea6a47a`
