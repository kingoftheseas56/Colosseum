# Colosseum Player 2 - isolated development and promotion design

**Date:** 2026-07-21
**Owner:** [Agent 4 (Codex), player]
**Status:** Proposed for Hemanth review; architecture direction approved in conversation
**Decision source:** the native D3D11 efficiency gate in
`2026-07-20-kodi-windows-video-architecture-decision.md`

## One-sentence decision

Build the native Windows player inside the Colosseum repository as a standalone `player2_harness`
backed by a reusable `player2_core` library, keep both completely absent from the production app's
link and QML load paths, and promote that exact library into Colosseum only after a written parity,
stability and efficiency ledger reaches 100%.

## Why this shape

Reader 2 established the safe Colosseum pattern: grow the replacement in its own C++/QML subtree,
boot it through a standalone harness, and leave the live application alone until swap day. Player 2
uses the same pattern, with one added boundary: the harness must link a reusable engine library.
That prevents a successful demo executable from later being rewritten during integration.

The efficiency benchmark justifies the arc but does not justify replacing mpvqt today. The native
prototype averaged 87.4% lower per-process GPU busy and 89.3% lower measured process CPU on the
target Intel laptop, but it is video-only. Player 2 must pay the real costs of audio, subtitles,
streaming, seeking and recovery and still preserve at least a 25-30% whole-path efficiency win.

## Ratified architecture

### One repository, two applications

Player 2 lives on Colosseum `master` as an inactive sibling application. Completed slices may merge
normally because production neither links nor loads them. This is safer than a months-long feature
branch and makes the final promotion a small, reviewable wiring change rather than a giant merge.

```text
Colosseum repository
|
+-- native/player2/              reusable native engine and Qt Quick surface
|   +-- core/                    session, demux, clocks, tracks and typed contract
|   +-- video/                   D3D11VA decode, conversion, ring and presentation
|   +-- audio/                   FFmpeg audio decode and WASAPI output
|   +-- subtitles/               subtitle parse/render pipeline
|   +-- platform/windows/        adapter, device-loss and display handling
|   +-- player2_harness_main.cpp standalone application entry point
|   +-- CMakeLists.txt           player2-only targets
|
+-- qml/player2/                 complete replacement player UI
|   +-- Harness.qml              lab shelf/file/stream launcher
|   +-- Player2Shell.qml         future Colosseum immersive surface
|   +-- controls/                transport, tracks, menus, stats and overlays
|   +-- Theme.qml                player-local tokens while isolated
|
+-- tests/player2/               contract, clock, state, media and soak tests
+-- native/prototypes/
|   +-- d3d11_qtquick_bridge/    frozen evidence; source donor, not runtime
+-- native/player/               current mpvqt production player; unchanged
+-- qml/PlayerPage.qml           current production UI; unchanged
```

The root CMake file receives only an additive option and `add_subdirectory(player2)`. During the
lab arc, the production `colosseum` target must not link `player2_core`, register Player 2 QML types,
or load anything under `qml/player2/`.

### Build products

1. **`player2_core`** - a static library containing the real playback session, FFmpeg pipelines,
   clock, state machine, diagnostics and D3D11 Qt Quick surface. There is no harness-only playback
   implementation.
2. **`player2_harness`** - a standalone Qt Quick executable that links `player2_core` and boots
   `qml/player2/Harness.qml`. It is the daily development and eyes-on parity application.
3. **Focused test executables** - headless or synthetic targets for state transitions, clocks,
   ring ownership, track switching, subtitle timing, seek/flush and device recovery.
4. **`player2.bat`** - a developer launcher comparable to Reader 2's launcher. Hemanth never needs
   to assemble command lines.

`COLOSSEUM_BUILD_PLAYER2` controls whether the lab targets build. It may default on for developer
builds, but it has no effect on the production executable until the promotion phase.

## Responsibility split

The house rule remains literal: **QML paints; C++ decides.**

### C++ owns

- source opening, headers, range/seek capability and cancellation;
- demux and hardware/software decoder selection;
- playback state and legal state transitions;
- the audio master clock, drift correction and frame scheduling;
- D3D11 devices, decoder surfaces, video processing, texture-ring ownership and fences;
- audio decode, resample and WASAPI buffering;
- track/chapter discovery and switching;
- subtitle parsing, timing and render products;
- buffering truth, errors, retries and end reasons;
- normalization algorithms and their performance accounting;
- screenshots/GIF frame supply, diagnostics and telemetry;
- device loss, display changes, HDR/color policy and recovery.

### QML owns

- the video surface's geometry and visual composition;
- transport chrome, menus, drawers, tooltips and focus behavior;
- progress/buffer visualization from typed C++ state;
- subtitle and diagnostic presentation when C++ supplies timed render data;
- responsive layout, animation and the established Colosseum visual language;
- forwarding user intent through typed commands.

QML must never infer buffering, synthesize playback position, choose a decoder, repair A/V drift,
or build raw FFmpeg/mpv option strings.

## Public contract

Player 2 exposes two QML-facing types plus one host-services boundary. Keeping control, painting
and Colosseum orchestration separate prevents a QQuickItem or a large QML page from becoming the
playback brain.

### `Player2Session` (`QObject`)

This is the stable application-facing backend. Its names should preserve the useful vocabulary of
today's `MpvItem` where doing so reduces promotion risk, but it must use typed diagnostics instead
of a generic `mpvProperty()` escape hatch.

Core state:

- `state`: `Idle`, `Opening`, `Buffering`, `Playing`, `Paused`, `Seeking`, `Ended`, `Recovering`,
  or `Error`;
- `position`, `duration`, `bufferedDuration`, `seekable`, `isLive`, `playbackRate`;
- `volume`, `muted`, `audioDelay`, `subtitleDelay`, `normalizationMode`;
- `videoInfo`, `audioInfo`, `audioTracks`, `subtitleTracks`, `chapters`;
- `selectedAudioTrack`, `selectedSubtitleTrack`;
- `error`, `endReason`, `diagnostics`.

Commands:

- `open(PlaybackRequest)`, `close()`, `play()`, `pause()`;
- `seekExact(seconds)`, `seekRelative(seconds)`, `frameStep(direction)`;
- `selectAudioTrack(id)`, `selectSubtitleTrack(id)`, `addExternalSubtitle(request)`;
- `setVolume()`, `setMuted()`, `setPlaybackRate()`, `setAudioDelay()`,
  `setSubtitleDelay()`, `setNormalizationMode()`;
- `setVideoPresentation(presentationRequest)` for fit/fill/aspect policy;
- `captureFrame()`, `startGifCapture()`, `stopGifCapture()`;
- `retry()` for a recoverable current request.

`PlaybackRequest` is a typed value object containing URL/path, display metadata, media identity,
resume position, request headers, live/stream flags and optional external-subtitle descriptors.
Player 2 does not know Cinemeta, Torrentio, downloads-page models or Colosseum navigation.

### `Player2VideoItem` (`QQuickItem`)

This item paints the current presentation texture and reports scene-graph lifecycle facts back to
the session. It consumes the shared ring and fences proven by the prototype. It contains no demux,
transport, retry or policy decisions.

### `Player2HostServices` (`QObject` interface)

The player shell needs application decisions that do not belong to a media engine: adjacent
episodes, alternate source candidates, download intent, Continue persistence, online-subtitle
search, skip-segment lookup, live channels/DVR and metadata hydration. These cross one explicit
typed host interface. `HarnessHostServices` supplies deterministic fixtures in the lab;
`ColosseumPlayer2HostServices` is written only during promotion and adapts the existing C++ stores.

The shell may request an action and paint the returned state, but it does not search addons, rank
torrents, write progress, choose the next episode or operate DVR itself. `player2_core` remains
independent of Cinemeta, Torrentio and Colosseum navigation.

### Events

The session emits typed state, position, buffering, track, chapter, error, recovery, capture and
diagnostic changes. Every asynchronous command either reaches an observable state or produces a
typed failure; the harness must never wait forever on a silent operation.

## Playback architecture

```text
PlaybackRequest
      |
      v
Player2Session state machine
      |
      +--> Demux worker --> D3D11VA decoder --> VideoProcessorBlt --> shared texture ring
      |                                                               |
      |                                                        Player2VideoItem
      |
      +--> Audio decoder --> resampler/normalizer --> WASAPI queue --> audio master clock
      |                                                               |
      +-------------------------- frame scheduler <--------------------+
      |
      +--> Subtitle worker --> timed cues/bitmaps --> QML/D3D overlay presentation
```

Audio is the master clock whenever an audible track is active. A monotonic QPC clock becomes master
for video-only media. Video frames are selected against the master clock; the renderer never sleeps
the GUI thread to pace frames. Seek is a state transition: pause scheduling, flush every decoder and
queue, seek demux, establish a new clock epoch, decode to the target, then resume. Track switches use
the same flush/epoch rule so stale audio, subtitles or frames cannot cross generations.

The current prototype is a donor to `video/`, then becomes frozen evidence. Player 2 must not link
the prototype executable or keep two evolving copies of its ring/fence implementation.

## Harness boundary and safe data

`player2_harness` must be capable of testing:

- a local file selected through the lab UI or command line;
- a supplied HTTP/HTTPS media URL plus headers;
- the known Wire HEVC fixture and smaller committed legal fixtures;
- subtitle and multi-audio fixtures;
- synthetic device-loss, slow-source and decoder-error scenarios.

The harness uses a separate settings/profile root such as
`%LOCALAPPDATA%\Colosseum\Player2Lab`. It may read an explicitly selected file from Colosseum's
download vault, but it must not update live Continue progress, production preferences, collections,
download state or player settings. Test credentials, addon discovery and catalogue navigation are
not copied into the lab. Streaming tests receive an already resolved URL, matching the future
`PlaybackRequest` boundary.

## What "100% parity" means

Parity is a ledger, not a feeling and not source-code similarity. A row is complete only when the
behavior exists in the standalone harness, has automated evidence where practical, and passes an
eyes-on comparison against production Colosseum. Deferred or "not applicable" rows require
Hemanth's written acceptance; they cannot silently count as complete.

There are two closures. **Lab parity 100%** authorizes the small feature-flagged integration diff;
it does not make Player 2 the default. Once `ColosseumPlayer2HostServices` is connected, every
host-backed row reopens and must pass against real Colosseum services. **Integrated parity 100%**,
plus the numeric gates, authorizes changing the default. This prevents a deterministic harness
stub from being mistaken for proof that real downloads, progress, sources or DVR are wired.

| Area | Required behavior before promotion |
|---|---|
| Open/end/error | Local and HTTP sources; honest opening/buffering/ended/error states; cancel and retry |
| Transport | Play/pause, exact/relative seek, seek thumbnails, frame step/back, speed, A-B loop, sleep timer and resume/start-over |
| Clock | Audio-master sync, video-only clock, drift correction, underrun recovery, no stale generation after seek |
| Audio | Decode/output, mute/volume, audio tracks, delay, device change, Smooth/Light/Full normalization |
| Video | D3D11VA HEVC/H.264/AV1 where supported, software fallback policy, fit/fill/aspect, 8/10-bit color |
| Subtitles | Embedded/external, drag-and-drop/online addition, text/ASS styling, forced/default selection, language automation and delay |
| Chapters/tracks | Discovery, labels, current selection, switching during play and after seek |
| Streaming | Range seeks, unknown duration, cache/buffer truth, cancellation, reconnect and live-edge policy |
| Source recovery | Alternate-source drawer, dead/stub detection, retry, wake reconnect and source switching without losing identity |
| Episodes | Episode browser, previous/next, queue ordering, Up Next countdown, cancel/confirm and progress identity |
| Skip behavior | Chapter/AniSkip intro, recap and credits segments; manual button and each auto-skip preference |
| Player chrome | Every current PlayerPage control, compact folds, hotkeys/shortcut sheet, keyboard/mouse behavior, auto-hide, context menu, pause card, clocks and accessibility |
| Window modes | Fullscreen, windowed developer mode, minimize/warm resume, PiP and display transition |
| Captures | Screenshot, reveal folder, GIF start/stop/abort without blocking playback |
| App actions | Download state/action, progress reporting, metadata hydration and player power inhibition through host services |
| Live/DVR | Channel configuration/switching, guide, DVR start/stop, live-edge jump and live exclusions |
| Color/HDR | Explicit matrix/range handling, untagged-HD fallback, SDR correctness and written HDR policy |
| Recovery | D3D device loss, audio-device loss, adapter/display change and clean application shutdown |
| Diagnostics | Typed codec, decode path, FPS/bitrate, buffer, dropped/late/repeated frames and error reason |
| Persistence seam | Position/save events reproduce current Continue behavior without the lab writing live data |

## Numeric promotion gates

The ledger alone is insufficient. The following gates must all pass on a release build:

1. **A/V synchronization:** over a 30-minute local fixture, absolute A/V error is at most 40 ms at
   p95 and shows no monotonic drift. Track switching and seeking re-enter that bound within two
   seconds.
2. **Steady playback:** two hours of the target HEVC content with audio and subtitles produces no
   crash, deadlock, device error or steadily increasing dropped/late count.
3. **Seek and flush:** 100 scripted seeks across local and seekable HTTP media produce no frame,
   audio or subtitle from an older generation after the seek-complete event.
4. **Memory stability:** a two-hour soak and 50 open/close cycles show no unbounded working-set,
   texture, decoder, audio-device or thread growth.
5. **Efficiency retention:** repeat the ABBA benchmark with equivalent audio, subtitle and chrome
   features enabled. On the i5-8365U/UHD 620, Player 2 must remain at least 25% lower than mpvqt in
   steady GPU busy and/or normalized CPU. Package power is supporting evidence, not a substitute for
   a regression-free player.
6. **Pacing:** use one common instrument for both backends when elevation is available. Presented
   frame interval p95, missed presents and application drop counters must show no material pacing
   regression even when the aggregate efficiency gate passes.
7. **Hardware breadth:** the full smoke/efficiency subset passes on the target Intel iGPU and at
   least one discrete GPU. Unsupported hardware fails over according to the written fallback policy.
8. **Normalization:** Smooth, Light and Full are separately measured for CPU, A/V sync and dropped
   frames. The outstanding normalization experiment becomes part of the Player 2 parity record,
   not a forgotten side test.

## Failure and fallback policy

During lab development, unsupported hardware or a failed D3D11 invariant ends the harness session
with a typed error; it must never hide a CPU transfer or silently claim zero-copy. During the
feature-flagged production period, **automatic mpvqt fallback** handles an unsupported adapter or
failure before the first visible frame: Colosseum reopens the request through mpvqt and records the
fallback reason in diagnostics.

Fallback is allowed only before Player 2 has produced user-visible playback. Mid-session backend
switching would duplicate clocks and progress semantics and is excluded. A Player 2 failure after
playback starts is reported honestly; retry may reopen through mpvqt as a new session with the last
confirmed position. Any future removal of mpvqt requires a separate decision that first supplies a
tested Player 2 software-decoder/device fallback.

## Development and review cadence

The arc is divided by capability, not UI screens:

1. extract the proven video path into `player2_core` without losing its benchmark contract;
2. establish the typed session/state machine and deterministic media harness;
3. add audio output and the master-clock/scheduler contract;
4. add seek/flush/EOS and track switching;
5. add subtitles, chapters and presentation controls;
6. add HTTP streaming, buffering, cancellation and recovery;
7. add captures, window modes, diagnostics, color/HDR and device recovery;
8. reproduce every PlayerPage interaction and close the parity ledger;
9. run the full soak, hardware and feature-equivalent efficiency gates;
10. perform promotion behind a feature flag.

Each slice lands with its tests while Player 2 remains inactive in production. Non-trivial timing,
threading and recovery slices receive cross-substrate review against a written Definition of Done.
No slice earns completion from an eyes-on demo alone.

## Promotion into Colosseum

Promotion is deliberately smaller than development:

1. Link the already-proven `player2_core` into `colosseum` behind a build flag.
2. Register `Player2Session` and `Player2VideoItem` in production.
3. Add one backend selection setting owned by C++; default remains mpvqt initially.
4. Add a second immersive loader for `qml/player2/Player2Shell.qml`, fed the same application-level
   playback request and progress events as the mpvqt page.
5. Run internal side-by-side and opt-in sessions; compare diagnostics and user-visible behavior.
6. Make Player 2 the default only after the integrated build repeats the parity and efficiency
   gates. Keep mpvqt as a one-release-cycle rollback.
7. Remove mpvqt only through a separate decision after real-world confidence. Removing it is not
   part of Player 2's initial promotion.

There is no bulk source merge on promotion day: Player 2 code is already in the repository. The
promotion diff is limited to build linkage, type registration, backend selection, the loader seam
and shared progress/error plumbing.

## Files forbidden before promotion

Until the parity and numeric gates pass, Player 2 work must not modify behavior in:

- `native/player/mpvitem.*`;
- `qml/PlayerPage.qml`;
- the production player loader or session-routing functions in `qml/Main.qml`;
- production progress, download, stream-resolution or window-mode contracts;
- default production build/runtime backend selection.

Additive root-CMake registration and test infrastructure are allowed. Shared files still follow the
Brotherhood coordination rule.

## Licensing boundary

FFmpeg linkage and redistribution must be reviewed against the actual selected build and codec
configuration before promotion. Kodi is an architecture reference only; no Kodi implementation is
copied. Every borrowed algorithm or library records origin, license and modification status beside
the dependency. The promotion gate includes a deployable runtime manifest and notices review.

## Out of scope

- changing the current production player during the lab arc;
- redesigning the approved player UX rather than reaching parity;
- a cross-process sidecar or permanent IPC texture bridge;
- macOS/Linux backends in this Windows-native arc;
- deleting mpvqt at first promotion;
- catalogue, torrent selection, download management or Continue redesign;
- new media features that production does not already possess, unless Hemanth separately approves
  them after parity.

## Definition of Done for the isolated-development architecture

- Player 2 has a dedicated C++/QML/test subtree and standalone executable in the same repository.
- The harness and future Colosseum integration consume the same `player2_core` library.
- Production does not link or load Player 2 before promotion.
- QML/C++ responsibilities and the typed application boundary are explicit.
- "100% parity" is a written, auditable ledger with no silent deferrals.
- A/V sync, soak, seek/flush, memory, efficiency, pacing, normalization and hardware gates are
  numeric and reproducible.
- Lab data cannot mutate the user's live Colosseum progress or settings.
- Failure/fallback behavior is explicit and never hides a CPU transfer or mid-session backend swap.
- Promotion is a small feature-flagged wiring change with mpvqt retained as rollback.
- Production-source anti-scope and licensing review are explicit.

**Design verdict:** build Player 2 now as an inactive in-repo sibling; promotion begins only after
the complete parity ledger and every numeric gate pass.
