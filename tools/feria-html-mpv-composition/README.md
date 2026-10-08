# Feria controlled HTML / mpv composition spike

## Acceptance status: not accepted

The final tested candidate passes **28/28 native/HTML interaction assertions but only 14/28 pixel checks** on the tested Qt 6.8.2 cloud desktop. Native-only and composited video captures are frequently black despite decoded dimensions, advancing position, and codec/PTS diagnostics. One earlier Qt 6.8.2 run did show genuine H.264 frames and paused overlays, but this was not repeatable in the final candidate. **This is not a validated codec-bypass solution and is not ready for production adoption.** See [RESULTS.md](RESULTS.md) for the exact positive evidence, failures and boundaries.


This is an isolated feasibility experiment, not a production integration or a new permanent player. It builds the repository's `native/player/mpvitem.cpp` and `.h` unchanged. It creates no canonical state, database, provider/session store or application route.

## What is being tested

A normal Qt Quick window contains the existing MpvItem, with a transparent WebEngineView above it. A controlled HTML player leaves a rectangular transparent aperture; its own buttons, caption and menu remain HTML. The HTML sends a deliberately small adapter protocol through WebChannel. **No decoded pixels are copied into HTML, canvas or an HTMLVideoElement.** This is native composition, not a new browser codec.

Only fixed fixture IDs `h264`, `ffv1`, and `missing` can reach mpv. Their paths are resolved under the explicit launcher-supplied fixture directory; fixture symlinks cannot escape that directory. Arbitrary paths, URLs, raw mpv commands and native player objects are not exposed. A request interceptor allows only the controlled page assets, Qt's WebChannel script, and the two browser-baseline media files. There is no network access from the page.

Every real document navigation changes the document identity. Load/replacement changes the playback-session identity, and close invalidates it. Commands must match both identities. Source replacement disconnects and replaces the Qt-owned MpvItem, and native callbacks capture both sender and session. This avoids reassigning a queued event from a previous mpv instance to the new source. The unchanged MpvItem's inherited `ready` signal gates `loadFile`, so the mpv render context exists before playback starts.

These two constraints were found through failed live tests, not assumed:

- MpvQt 1.1.0 forwards end-file for EOF and errors, but not STOP. A stop-and-wait-for-endFile barrier never completes. This spike uses a real per-source native lifecycle boundary instead.
- libmpv requires its render context before playback. The spike now gates loading on renderer readiness, but this did not make picture delivery reliable. Decoded dimensions and an advancing clock were observed alongside black pixels. Dimensions alone are not sufficient evidence of rendered video.

HTML/body are transparent from the first paint. Opaque chrome is painted in child elements. The separate alpha probe found stale textures when toggling an initially opaque root canvas to transparent; repeated window updates did not fix that route.

## Build

Requires CMake, a C++17 compiler, Qt Quick/WebEngineQuick/WebChannel/Test 6.8 or later, MpvQt, and libmpv. The default dependency for continued reproduction should be **unmodified upstream MpvQt commit `19a92995156b006ee42e4f25c29e1f2cf3e1a336`** (reports 1.1.1, SONAME 2), which includes the upstream render-thread/core-lifetime repair. It did not solve the observed black-frame failures. A separate diagnostic patch adding Qt GL-state resets before and after rendering also failed; it is not the default dependency or a recommended fix. FFmpeg and Python Pillow are used only for fixture generation and external pixel verification. Use an official installed SDK or source an environment file first.

```sh
cmake -S tools/feria-html-mpv-composition -B /tmp/feria-composition-build -G Ninja \
  -DMpvQt_DIR=/path/to/unmodified-pinned-mpvqt/lib/cmake/MpvQt
cmake --build /tmp/feria-composition-build
ctest --test-dir /tmp/feria-composition-build --output-on-failure
```

The experimental target has no dependency on the broad Colosseum application build. PIC is enabled explicitly on GCC/Clang to avoid Qt global COPY relocations when checking a newer compatible Qt runtime. The evidence records compile-time Qt and runtime Qt separately; runtime substitution is not a rebuilt/newer SDK test.

## Reproduce on a real desktop

```sh
tools/feria-html-mpv-composition/make-fixtures.sh /tmp/feria-fixtures
tools/feria-html-mpv-composition/run-selftest.sh - /tmp/feria-composition-build /tmp/feria-fixtures /tmp/feria-evidence
```

The tested input byte hashes are:

- `h264-aac.mp4`: SHA-256 `1f479e7d5479d63eed8931cc8a7fefd18d33d1555adce5b7ec27d9466389393d`
- `ffv1-pcm.mkv`: SHA-256 `fb07125f45c91226bfcbd06a6790f4298b8176ca89de0f83ab4e79526b469d54`

Regenerated FFmpeg output may differ by encoder/build version; preserve exact tested files when comparing runtime results.

The first argument may be an environment file instead of `-`. After sourcing it, verify that the selected `python3` has Pillow (`python3 -c "import PIL"`); SDK environment files can change Python selection. Install Pillow in that selected environment or run the external verifier with a Python interpreter that already has it. The launcher uses a private temporary page directory and an off-the-record WebEngine profile. Local-file provenance is intentional: loading local fixture media from a qrc-origin page was rejected by Chromium before codec decoding, so that result was not a valid codec baseline.

Omit `--self-test` when launching the executable directly to leave its manual controls open. Supply `--fixtures DIRECTORY` and `--evidence DIRECTORY`. The command protocol remains narrow. The diagnostic WebChannel object is registered only with `--self-test`.

## Evidence contract

The finite test is designed to finish under two minutes, with a 110-second watchdog. It sends native Qt pointer clicks to the actual HTML controls and records `isTrusted` click events. The clicked handlers invoke the WebChannel adapter; native assertions independently read cached MpvItem position/duration/pause/volume/dimensions. Ordinary 10 Hz state publication does not call blocking mpv getters.

The test covers browser HTMLVideoElement playback attempts of both exact local fixtures, mpv play/pause/seek/volume, source replacement, interrupted load/close/reopen, EOF, missing-file error, reopen after error, forbidden source/command rejection, document reload, stale document/session rejection, resize/scroll/zoom/fullscreen geometry, and paused HTML caption/menu overlays.

An application exit code of zero and `outcomes.json.passed` cover only native/HTML assertions. The JSON explicitly marks that scope. The external pixel verifier is mandatory; its `passed` value combines native assertions and pixel results.

Outputs:

- `outcomes.json`: actual native assertions, HTML baseline events/errors/frame counts, trusted pointer events, document/source identities, capture geometry, Qt/Chromium versions and bounded mpv diagnostics
- Native window screenshots: two different playback positions, a native-only capture, paused overlays, transformed layouts, FFV1, and document reopen
- `pixel-checks.json`: central native-video regions must contain varied pixels and the two sought frames must visibly differ. A black frame with valid decode metadata is a failure.
- `runtime.log`, `session-tests.log`, and `exit-code.txt`

Pixel checks and actual screen inspection must accompany JSON assertions. Screenshot existence alone is not picture proof. Browser baseline results are measurements, not predetermined “unsupported” assertions. An access error is an invalid codec comparison; a media decoder error after reading the actual bytes is relevant evidence.

Diagnostics use bounded explicit checkpoints. They include video/audio codecs, decoded dimensions, mpv estimated frame number, output/decoder drop counters, audio parameters and audio PTS, Qt frame swaps, position notifications and native-process CPU seconds. Estimated frame number is not an exact decoded-frame counter, and CPU excludes the Chromium child process. This is a feasibility smoke test, not a sustained performance benchmark.

Audio output is deliberately `null`: audio decoding/PTS can be measured, but audible sound and real device behavior are **not tested**. Hardware decoding is disabled for the controlled software-rendered experiment. HTML captions here are controlled overlay text, not a provider subtitle pipeline.

## Limits

No arbitrary HTMLMediaElement compatibility, arbitrary website adapter, production page migration, provider credentials, cookies, MSE, DRM, streaming authorization, remote media, or production adoption is included. Geometry supports an axis-aligned rectangular aperture plus the controlled page's resize, scroll, zoom and fullscreen behavior. Arbitrary CSS transforms, clipping stacks, occlusion by unrelated DOM elements and cross-platform rendering remain separate work.

Licenses in the original Colosseum/MpvQt-derived source files are preserved because those sources are compiled in place without edits. This folder contains only the isolated adapter, controlled page, test driver and reproduction helpers.
