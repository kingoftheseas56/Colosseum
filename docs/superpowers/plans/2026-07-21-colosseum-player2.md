# Colosseum Player 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Player 2 as an isolated, feature-complete Windows player in the Colosseum repository, prove 100% lab and integrated parity plus the numeric efficiency gates, then promote it behind a reversible backend flag.

**Architecture:** `player2_core` owns demux, decode, clocks, audio, subtitles, recovery and typed state; `Player2VideoItem` only paints the shared D3D11 texture; `player2_harness` and later Colosseum consume the same library. Production continues using mpvqt until lab parity, soak, hardware and efficiency gates pass.

**Tech Stack:** C++17, Qt 6.11 Core/Gui/Quick/QML/Network/Test, FFmpeg shared libraries, D3D11/DXGI, WASAPI, CMake/Ninja/MSVC, QML, PowerShell contract and benchmark scripts.

## Global Constraints

- The governing design is `docs/superpowers/specs/2026-07-21-colosseum-player2-isolated-development-design.md`.
- House doctrine is literal: QML paints; C++ decides. QML sends typed intent and renders typed state; it does not demux, select decoders, infer buffering, pace frames or repair A/V drift.
- Before Task 17, production must not link `player2_core`, register Player 2 types, load `qml/player2/`, or change the default player. `native/player/mpvitem.*`, `qml/PlayerPage.qml` and production routing are forbidden before that task.
- Keep `native/prototypes/d3d11_qtquick_bridge/` frozen after Task 3. It is evidence and a source donor, never a runtime dependency.
- One build directory per configuration. Use `native/build-player2` for the lab and `native/build-msvc` for production verification. Kill `colosseum.exe` by PID before rebuilding production.
- The lab profile root is `%LOCALAPPDATA%\Colosseum\Player2Lab`; it must never mutate production Continue progress, downloads, collections or preferences.
- Every asynchronous operation carries a session generation. No decoded frame, audio packet or subtitle cue from an old generation may become visible after seek, track change, close or reopen.
- Unsupported hardware and hidden CPU transfers are typed failures. Never silently claim zero-copy.
- Each task lands as one explicit-path commit after its listed tests pass. Timing, threading, recovery and promotion tasks require cross-substrate review against that task's exit criteria.
- Preserve unrelated dirty-tree changes. Run `git status --short` before staging and stage only the paths named by the current task.
- For every task, stage with `git add -- <task paths>`, inspect `git diff --cached -- <task paths>`, then commit with the task's explicit pathspec. Never use `git add .`.
- `native/CMakeLists.txt` is shared. Before Task 1 or Task 17 changes it, post the exact additive lines to Brotherhood `agents/chat.md` as required by governance.

## File and Responsibility Map

| Path | Responsibility | First task |
|---|---|---:|
| `native/player2/CMakeLists.txt` | Player 2 library, harness and focused test targets | 1 |
| `native/player2/core/Player2Types.*` | Typed public values, enums, errors and diagnostics | 2 |
| `native/player2/core/Player2StateMachine.*` | Legal state transitions and observable failures | 2 |
| `native/player2/core/Player2Session.*` | Public QML-facing command/state facade | 5 |
| `native/player2/core/PlaybackGeneration.h` | Monotonic epoch carried by all async products | 5 |
| `native/player2/core/DemuxSession.*` | FFmpeg open, demux, tracks, chapters and cancellation | 5 |
| `native/player2/core/PlaybackClock.*` | Audio-master and QPC video-only clock epochs | 7 |
| `native/player2/core/FrameScheduler.*` | Select/drop/repeat frames against the master clock | 7 |
| `native/player2/video/D3D11TextureRing.*` | Producer/consumer slot and fence ownership | 3 |
| `native/player2/video/D3D11VideoPipeline.*` | D3D11VA decode and VideoProcessor conversion | 3 |
| `native/player2/video/Player2VideoItem.*` | Qt Quick scene-graph texture import and paint only | 3 |
| `native/player2/audio/AudioPipeline.*` | Decode, resample, normalization and timestamp flow | 6, 9 |
| `native/player2/audio/WASAPIAudioSink.*` | Endpoint negotiation, queueing, clock and device loss | 6 |
| `native/player2/subtitles/SubtitlePipeline.*` | Embedded/external text and bitmap cue timing | 10 |
| `native/player2/network/HttpMediaSource.*` | Headers, range semantics, cancellation and reconnect | 11 |
| `native/player2/platform/windows/DeviceRecovery.*` | Adapter/display/D3D/audio-device recovery policy | 12 |
| `native/player2/diagnostics/PlaybackDiagnostics.*` | Typed counters and reproducible JSON snapshots | 12 |
| `native/player2/host/Player2HostServices.h` | App orchestration interface, no Colosseum dependencies | 2 |
| `native/player2/host/HarnessHostServices.*` | Deterministic lab fixtures and event recording | 4, 14 |
| `native/player2/player2_harness_main.cpp` | Standalone executable and QML type registration | 4 |
| `qml/player2/Harness.qml` | Lab launcher and scenario controls | 4 |
| `qml/player2/Player2Shell.qml` | Replacement immersive player surface | 13 |
| `qml/player2/controls/` | Transport, tracks, menus, overlays and stats | 13-15 |
| `tests/player2/` | Unit, contract, media, soak, parity and benchmark evidence | all |
| `player2.bat` | One-click lab launcher | 4 |
| `docs/superpowers/specs/player2-parity-ledger.md` | Auditable lab/integrated parity record | 13 |

## Phase Gates

| Gate | Tasks | Exit condition |
|---|---:|---|
| A. Isolated foundation | 1-4 | Harness builds and presents the proven zero-copy video path; production binary has no Player 2 linkage |
| B. Playback engine | 5-9 | Local media has audio-master A/V sync, deterministic seek/flush, tracks and all normalization modes |
| C. Media breadth | 10-12 | Subtitles, HTTP/live semantics, typed diagnostics and device recovery work without stale generations |
| D. Lab parity | 13-15 | Every parity-ledger row is `PASS` in the standalone harness; no silent deferrals |
| E. Numeric proof | 16 | Sync, soak, seeks, memory, pacing, hardware and >=25% efficiency gates pass |
| F. Opt-in integration | 17 | Real Colosseum services pass integrated parity with mpvqt still default and pre-frame fallback working |
| G. Default promotion | 18 | Repeated integrated gates pass; Player 2 becomes default with one-release mpvqt rollback |

---

## Phase A - Isolated Foundation

### Task 1: Add the isolated build boundary

**Files:**
- Modify: `native/CMakeLists.txt`
- Create: `native/player2/CMakeLists.txt`
- Create: `native/player2/core/Player2BuildMarker.h`
- Create: `tests/player2/player2_isolation_contract.ps1`

**Interfaces:**
- CMake option `COLOSSEUM_BUILD_PLAYER2`, default `OFF`.
- Static target `player2_core`, executable `player2_harness`, and `player2_unit_tests` umbrella label.
- `colosseum` must have no direct or transitive dependency on `player2_core` before Task 17.

- [ ] Write the failing isolation contract. It must configure with the option off and on, inspect Ninja target dependencies, and reject `player2_core` or `qml/player2` in the production target graph.

```powershell
if ((Get-Content -Raw native/CMakeLists.txt) -notmatch 'option\(COLOSSEUM_BUILD_PLAYER2') {
    throw 'COLOSSEUM_BUILD_PLAYER2 option is missing'
}
cmake -S native -B native/build-player2 -G Ninja -DCOLOSSEUM_BUILD_PLAYER2=ON
cmake --build native/build-player2 --target help | Select-String 'player2_core|player2_harness'
ninja -C native/build-player2 -t query colosseum |
    Select-String 'player2_core|qml.player2' | ForEach-Object { throw "production links Player 2: $_" }
```

- [ ] Run `powershell -NoProfile -File tests/player2/player2_isolation_contract.ps1`; expect failure `COLOSSEUM_BUILD_PLAYER2 option is missing`.
- [ ] Add the option and subdirectory without changing the `colosseum` source or link lists.

```cmake
option(COLOSSEUM_BUILD_PLAYER2 "Build the isolated Player 2 laboratory" OFF)
if(COLOSSEUM_BUILD_PLAYER2)
    add_subdirectory(player2)
endif()
```

- [ ] Define an initially minimal `player2_core` and test target in `native/player2/CMakeLists.txt`; require Qt Core/Gui/Quick/Qml/Network/Test and the FFmpeg development root but do not link mpvqt.
- [ ] Run the isolation contract again; expect `player2_isolation_contract: PASS`.
- [ ] Build `cmake --build native/build-player2 --target player2_core`; expect exit 0.
- [ ] Commit only the four task paths: `git commit -m "[Agent 4 (Codex), player] Scaffold isolated Player 2 targets" -- native/CMakeLists.txt native/player2/CMakeLists.txt native/player2/core/Player2BuildMarker.h tests/player2/player2_isolation_contract.ps1`.

### Task 2: Freeze the typed public contract and state machine

**Files:**
- Create: `native/player2/core/Player2Types.h`
- Create: `native/player2/core/Player2Types.cpp`
- Create: `native/player2/core/Player2StateMachine.h`
- Create: `native/player2/core/Player2StateMachine.cpp`
- Create: `native/player2/host/Player2HostServices.h`
- Create: `tests/player2/player2_state_machine_test.cpp`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:**

```cpp
enum class Player2State { Idle, Opening, Buffering, Playing, Paused, Seeking, Ended, Recovering, Error };
enum class NormalizationMode { Smooth, Light, Full };
enum class Player2ErrorCode { None, Cancelled, OpenFailed, UnsupportedHardware, DecodeFailed, NetworkFailed, DeviceLost, AudioDeviceLost, InvalidCommand };
struct ExternalSubtitleRequest { QUrl source; QString title; QString language; };
struct PlaybackRequest {
    QUrl source;
    QString mediaId;
    QString title;
    double resumeSeconds = 0.0;
    QHash<QByteArray, QByteArray> headers;
    QVariantMap displayMetadata;
    QList<ExternalSubtitleRequest> externalSubtitles;
    bool stream = false;
    bool live = false;
};
struct Player2Error { Player2ErrorCode code = Player2ErrorCode::None; QString message; bool recoverable = false; };
class Player2HostServices : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void requestAdjacentEpisode(const QString &mediaId, int direction) = 0;
    virtual void requestAlternateSources(const QString &mediaId) = 0;
    virtual void reportProgress(const QString &mediaId, double position, double duration) = 0;
};
```

- [ ] Write table-driven state tests for every legal edge and explicit rejection of illegal edges. Required examples: `Idle -> Opening`, `Playing -> Seeking`, `Seeking -> Playing`, `Opening -> Error`, and rejection of `Idle -> Playing`.
- [ ] Run `cmake --build native/build-player2 --target player2_state_machine_test`; expect compile failure because the types do not exist.
- [ ] Implement value types, metatype registration and a state machine whose rejected transition returns a typed `InvalidCommand` error without mutating state.
- [ ] Run `native/build-player2/player2_state_machine_test.exe`; expect `player2_state_machine_test: PASS`.
- [ ] Run the isolation contract; expect PASS.
- [ ] Commit the seven task paths with message `[Agent 4 (Codex), player] Define Player 2 contract and states`.

### Task 3: Extract the proven D3D11 zero-copy video organ

**Files:**
- Create: `native/player2/video/D3D11TextureRing.h`
- Create: `native/player2/video/D3D11TextureRing.cpp`
- Create: `native/player2/video/D3D11VideoPipeline.h`
- Create: `native/player2/video/D3D11VideoPipeline.cpp`
- Create: `native/player2/video/Player2VideoItem.h`
- Create: `native/player2/video/Player2VideoItem.cpp`
- Create: `tests/player2/player2_texture_ring_test.cpp`
- Create: `tests/player2/player2_video_fixture_test.cpp`
- Create: `tests/player2/player2_zero_copy_contract.ps1`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:**

```cpp
struct VideoFrameToken { quint64 generation = 0; quint64 sequence = 0; qint64 ptsUs = 0; };
class D3D11VideoPipeline {
public:
    bool initialize(ID3D11Device *qtDevice, QString *error);
    bool submitDecodedFrame(AVFrame *frame, VideoFrameToken token, QString *error);
    std::optional<VideoFrameToken> acquireLatestForPresentation(quint64 generation);
    void retirePresentedFrame(quint64 consumerFenceValue);
    void flush(quint64 nextGeneration);
};
class Player2VideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *session READ session WRITE setSession NOTIFY sessionChanged)
};
```

- [ ] Port the prototype slot-ring test first and add generation invalidation, producer starvation and consumer-fence completion cases. Run it and expect failure on missing Player 2 headers.
- [ ] Write the zero-copy contract by adapting the frozen prototype contract. Require D3D11 Qt Quick, public native texture import, shared handles/fences, D3D11VA, `VideoProcessorBlt`, explicit color metadata, RGBA output and zero `av_hwframe_transfer_data`, `sws_scale`, `UpdateSubresource`, OpenGL or `QQuickFramebufferObject` occurrences.
- [ ] Extract, rename and separate the prototype code. `Player2VideoItem` may acquire/import/present textures, but it must not open media or own the decode worker.
- [ ] Run `player2_texture_ring_test.exe`; expect PASS. Run `player2_zero_copy_contract.ps1`; expect `player2_zero_copy_contract: PASS`.
- [ ] Build `player2_video_fixture_test` in release mode and replay the Wire fixture. Write diagnostics JSON and require `cpuTransfers=0`, `deviceErrors=0`, adapter match true and D3D11 hardware frames.
- [ ] Re-run the existing prototype contract to prove the donor was not modified.
- [ ] Obtain cross-substrate review of texture/fence lifetime and the no-hidden-copy invariant.
- [ ] Commit only new Player 2 video/test files and `native/player2/CMakeLists.txt` with message `[Agent 4 (Codex), player] Extract Player 2 zero-copy video core`.

### Task 4: Boot the standalone harness with safe lab storage

**Files:**
- Create: `native/player2/player2_harness_main.cpp`
- Create: `native/player2/host/HarnessHostServices.h`
- Create: `native/player2/host/HarnessHostServices.cpp`
- Create: `qml/player2/Harness.qml`
- Create: `qml/player2/Theme.qml`
- Create: `qml/player2/qmldir`
- Create: `player2.bat`
- Create: `tests/player2/player2_harness_contract.ps1`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:** at this task `player2_harness --scenario synthetic [--report PATH]`; Tasks 5 and 11 add `--file PATH`, `--url URL` and `--headers-json PATH`. Settings organization/application is `Colosseum/Player2Lab`; `HarnessHostServices` records events to a lab JSONL file rather than production stores.

- [ ] Write the contract to reject imports of production stores, production settings roots, mpvqt/libmpv, `qml/PlayerPage.qml`, Cinemeta or Torrentio.
- [ ] Implement QML registration, D3D11 scene-graph selection before window creation, synthetic-scenario command-line parsing and deterministic exit/report behavior.
- [ ] Make `Harness.qml` support synthetic video; it must show typed adapter, source, decode path, generated/presented/dropped/late/CPU-transfer counters.
- [ ] Run `player2_harness.exe --scenario synthetic --report artifacts/player2/gate-a.json`; expect exit 0 and 300+ presented frames with zero device errors.
- [ ] Launch through `player2.bat`; verify the user performs no terminal action.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add standalone Player 2 harness`.

---

## Phase B - Complete Local Playback Engine

### Task 5: Add cancellable demux, generations and the session facade

**Files:**
- Create: `native/player2/core/PlaybackGeneration.h`
- Create: `native/player2/core/DemuxSession.h`
- Create: `native/player2/core/DemuxSession.cpp`
- Create: `native/player2/core/Player2Session.h`
- Create: `native/player2/core/Player2Session.cpp`
- Create: `tests/player2/player2_demux_session_test.cpp`
- Create: `tests/player2/fixtures/make_media_fixtures.ps1`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:**

```cpp
class PlaybackGeneration {
public:
    quint64 current() const noexcept;
    quint64 advance() noexcept;
    bool accepts(quint64 candidate) const noexcept;
};
class Player2Session : public QObject {
    Q_OBJECT
    Q_PROPERTY(Player2State state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
public slots:
    void open(const PlaybackRequest &request);
    void close();
    void play();
    void pause();
};
```

- [ ] Generate small legal deterministic fixtures with FFmpeg: video-only, A/V, two-audio-track, embedded subtitle and chaptered media. Commit the script and manifest, not oversized generated binaries unless each fixture is under the repository fixture limit.
- [ ] Write tests for stream discovery, metadata, cancellation during open, close/reopen generation advance, end reason and absence of old-generation packets.
- [ ] Implement FFmpeg interrupt callbacks and one demux worker. All worker-to-session delivery uses queued Qt signals carrying the generation.
- [ ] Run `player2_demux_session_test.exe`; expect PASS and no process hang under a 30-second timeout.
- [ ] Extend the harness with `--file PATH`, bind the real `Player2Session`, and run the local A/V fixture. Video may be silent at this task, but state must reach `Playing`, duration/tracks must populate and close must return to `Idle`.
- [ ] Run `player2_harness.exe --file "<Wire S4E13 absolute path>" --report artifacts/player2/gate-b.json`; expect D3D11VA HEVC, zero CPU transfers and visible video. Resolve the already-used audit fixture path from the benchmark ADR; do not commit the copyrighted file or its path.
- [ ] Obtain cross-substrate review of cancellation, ownership and thread shutdown.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add Player 2 demux session`.

### Task 6: Add FFmpeg audio decode and WASAPI output

**Files:**
- Create: `native/player2/audio/AudioPipeline.h`
- Create: `native/player2/audio/AudioPipeline.cpp`
- Create: `native/player2/audio/WASAPIAudioSink.h`
- Create: `native/player2/audio/WASAPIAudioSink.cpp`
- Create: `tests/player2/player2_audio_pipeline_test.cpp`
- Modify: `native/player2/core/Player2Session.*`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:**

```cpp
struct AudioClockSnapshot { qint64 mediaPositionUs = 0; qint64 qpcTimestamp = 0; bool valid = false; };
class WASAPIAudioSink {
public:
    bool open(const AudioFormat &format, QString *error);
    int write(const AudioBuffer &buffer, quint64 generation, QString *error);
    AudioClockSnapshot clock() const;
    void flush(quint64 generation);
    void setVolume(float linear); void setMuted(bool muted);
};
```

- [ ] Write deterministic resample/timestamp tests and a fake sink test for underrun, flush and generation rejection.
- [ ] Add FFmpeg `swresample` linkage, then implement audio decode, conversion to the negotiated shared-mode endpoint and event-driven WASAPI buffering. No busy loop and no GUI-thread audio work.
- [ ] Expose typed audio device/format/queue-depth diagnostics and volume/mute commands through `Player2Session`.
- [ ] Run unit tests; expect exact sample-count/timestamp assertions and PASS.
- [ ] Play the Wire fixture for five minutes; require audible synchronized output, no queue growth and no old-generation audio after close.
- [ ] Obtain cross-substrate review of COM apartment, endpoint lifetime, buffer arithmetic and shutdown.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add Player 2 WASAPI audio`.

### Task 7: Establish the master clock and frame scheduler

**Files:**
- Create: `native/player2/core/PlaybackClock.h`
- Create: `native/player2/core/PlaybackClock.cpp`
- Create: `native/player2/core/FrameScheduler.h`
- Create: `native/player2/core/FrameScheduler.cpp`
- Create: `tests/player2/player2_clock_scheduler_test.cpp`
- Modify: `native/player2/core/Player2Session.*`
- Modify: `native/player2/video/D3D11VideoPipeline.*`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:** `PlaybackClock::reset(epochMediaUs, epochQpc)`, `positionAt(qpcNow)`, `setRate(rate)`, `pause/resume`; `FrameScheduler::choose(masterUs, frames)` returns `Present`, `DropLate`, `RepeatCurrent` or `WaitUntilQpc` and never sleeps the GUI thread.

- [ ] Write virtual-clock tests for 23.976/24/25/29.97/60 fps, pause/resume, playback rates, audio-master loss, video-only QPC fallback, drift correction and bounded late-drop policy.
- [ ] Use the audio clock whenever a selected audio track is being decoded, even when muted; use QPC only for video-only media or when no audio track is selected.
- [ ] Drive render-thread updates with deadlines/signals while keeping all policy in C++.
- [ ] Run scheduler tests; expect deterministic PASS without real-time sleeps.
- [ ] Add a 30-minute accelerated timestamp simulation; require p95 absolute error <=40 ms and no monotonic drift.
- [ ] Run a real ten-minute Wire playback report; record A/V samples for the eventual gate.
- [ ] Obtain cross-substrate timing review.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add Player 2 clock and scheduler`.

### Task 8: Make seek, flush, EOS and track switching generation-safe

**Files:**
- Create: `tests/player2/player2_seek_generation_test.cpp`
- Modify: `native/player2/core/DemuxSession.*`
- Modify: `native/player2/core/Player2Session.*`
- Modify: `native/player2/core/PlaybackClock.*`
- Modify: `native/player2/video/D3D11VideoPipeline.*`
- Modify: `native/player2/audio/AudioPipeline.*`

**Interfaces:** `seekExact(double)`, `seekRelative(double)`, `frameStep(int)`, `selectAudioTrack(QString)`, and observable `seekCompleted(generation, actualSeconds)`; seek order is advance generation, pause scheduling, flush every active pipeline, seek demux, decode to target, reset clock epoch, publish completion, resume prior play/pause state.

- [ ] Write 100-seek tests with tagged video/audio products and track changes during play, pause and immediately after seek.
- [ ] Implement the single generation barrier and EOS drain. No component may invent its own independent seek epoch.
- [ ] Run the test under a 60-second timeout; expect 100 completions and zero stale products.
- [ ] Exercise frame step forward/back on a paused fixture and exact seek near start/end.
- [ ] Obtain cross-substrate review of lock ordering and flush races.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Make Player 2 seeks generation-safe`.

### Task 9: Implement Smooth, Light and Full normalization as an explicit audio stage

**Files:**
- Create: `native/player2/audio/AudioNormalizer.h`
- Create: `native/player2/audio/AudioNormalizer.cpp`
- Create: `tests/player2/player2_audio_normalizer_test.cpp`
- Create: `tests/player2/player2_normalization_benchmark.ps1`
- Modify: `native/player2/audio/AudioPipeline.*`
- Modify: `native/player2/core/Player2Session.*`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:** `Smooth` is bit-transparent except required endpoint conversion; `Light` matches the intent of current `dynaudnorm`; `Full` implements EBU R128 loudness normalization. Mode changes are observable, flush filter latency and preserve the current position without reopening media.

- [ ] Write tests for mode mapping, sample format/rate preservation, finite outputs, latency reporting and live mode changes.
- [ ] Add FFmpeg `avfilter` linkage and implement normalization through an isolated filter graph. The public contract contains no raw filter string.
- [ ] Run the normalization benchmark as three cooldown-separated Wire passes. Record normalized CPU, GPU busy, dropped/late frames and A/V p95 for every mode.
- [ ] Require Smooth to remain the default. Full may cost more but must not violate A/V sync or cause unbounded drops; report its cost rather than disguising it.
- [ ] Answer the agenda question explicitly in the benchmark JSON and parity ledger: whether frames drop under Light and Full normalization.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add explicit Player 2 normalization modes`.

---

## Phase C - Media Breadth and Recovery

### Task 10: Add subtitles, chapters and presentation controls

**Files:**
- Create: `native/player2/subtitles/SubtitlePipeline.h`
- Create: `native/player2/subtitles/SubtitlePipeline.cpp`
- Create: `native/player2/core/TrackPolicy.h`
- Create: `native/player2/core/TrackPolicy.cpp`
- Create: `tests/player2/player2_subtitle_timing_test.cpp`
- Create: `tests/player2/player2_track_policy_test.cpp`
- Modify: `native/player2/core/Player2Session.*`
- Modify: `native/player2/core/DemuxSession.*`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:** timed text cue and bitmap subtitle products carry generation/start/end; external subtitle requests contain URL/path, title and language; track policy receives explicit saved preferences and returns typed selected IDs/reason; chapters are typed immutable rows.

- [ ] Write cue-boundary, delay, seek-flush, ASS positioning, bitmap lifetime, forced/default and language-policy tests.
- [ ] Implement embedded and external subtitle decode. C++ supplies styled timed products; QML later paints them. Use libass if selected, record its license and keep its render products behind `SubtitlePipeline`.
- [ ] Implement audio/subtitle delays and fit/fill/aspect presentation requests in the session contract.
- [ ] Run both tests; expect zero old-generation cue visibility and PASS.
- [ ] Play subtitle and multi-track fixtures through the harness; verify switches and chapter crossings while playing and after seek.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add Player 2 subtitles and tracks`.

### Task 11: Add HTTP streaming, buffering truth, cancellation and live policy

**Files:**
- Create: `native/player2/network/HttpMediaSource.h`
- Create: `native/player2/network/HttpMediaSource.cpp`
- Create: `tests/player2/player2_http_fixture_server.ps1`
- Create: `tests/player2/player2_http_media_test.cpp`
- Modify: `native/player2/core/DemuxSession.*`
- Modify: `native/player2/core/Player2Session.*`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:** source capabilities report seekable/range-supported/known-duration/live; buffering state comes from queue thresholds plus source progress; reconnect is a typed state transition with bounded attempts; request headers are applied without logging secrets.

- [ ] Build a local deterministic HTTP server scenario for range success, range rejection, slow chunks, disconnect/reconnect, unknown length and cancellation.
- [ ] Write tests requiring honest `Opening`, `Buffering`, `Playing`, `Recovering`, `Ended` and `Error` transitions, plus cancellation within two seconds.
- [ ] Implement custom FFmpeg AVIO or a bounded Qt-network bridge. Choose one driver and document thread ownership; do not layer two independent caches.
- [ ] Run the HTTP test suite; expect PASS with no internet dependency.
- [ ] Test one explicitly supplied real stream URL in the harness without importing addon/catalogue logic.
- [ ] Obtain cross-substrate review of backpressure, interruptibility and credential redaction.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add Player 2 streaming transport`.

### Task 12: Add diagnostics, color/HDR policy and device recovery

**Files:**
- Create: `native/player2/diagnostics/PlaybackDiagnostics.h`
- Create: `native/player2/diagnostics/PlaybackDiagnostics.cpp`
- Create: `native/player2/platform/windows/DeviceRecovery.h`
- Create: `native/player2/platform/windows/DeviceRecovery.cpp`
- Create: `tests/player2/player2_device_recovery_test.cpp`
- Create: `tests/player2/player2_diagnostics_contract.ps1`
- Create: `docs/superpowers/specs/player2-color-hdr-policy.md`
- Modify: `native/player2/video/D3D11VideoPipeline.*`
- Modify: `native/player2/audio/WASAPIAudioSink.*`
- Modify: `native/player2/core/Player2Session.*`

**Interfaces:** diagnostics snapshot includes codec, hardware format, adapter, FPS/bitrate, buffer, presented/dropped/late/repeated, CPU transfers, device errors, audio queue and A/V error; recovery reasons are typed; HDR policy explicitly identifies supported passthrough/tone-map behavior and rejects unsupported combinations.

- [ ] Write schema/contract tests and injectable fake-device tests for D3D removal, audio endpoint loss, display/adapter change and clean shutdown during recovery.
- [ ] Implement one recovery coordinator owned by `Player2Session`; individual pipelines report facts but do not independently reopen media.
- [ ] Preserve explicit matrix/range handling and untagged-HD fallback from the prototype. Test 8-bit and 10-bit SDR; document honest HDR support level.
- [ ] Run device tests; expect deterministic recovery or typed terminal error, never a hang.
- [ ] Run diagnostics contract; expect no generic `mpvProperty`-style string lookup and a stable JSON schema.
- [ ] Obtain cross-substrate review of device teardown and recovery ordering.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Add Player 2 recovery and diagnostics`.

---

## Phase D - Standalone Lab Parity

### Task 13: Build the Player 2 shell and core playback chrome

**Files:**
- Create: `qml/player2/Player2Shell.qml`
- Create: `qml/player2/controls/TransportBar.qml`
- Create: `qml/player2/controls/SeekBar.qml`
- Create: `qml/player2/controls/TrackMenu.qml`
- Create: `qml/player2/controls/OverflowMenu.qml`
- Create: `qml/player2/controls/SubtitleLayer.qml`
- Create: `qml/player2/controls/StatsOverlay.qml`
- Create: `docs/superpowers/specs/player2-parity-ledger.md`
- Create: `tests/player2/player2_shell_contract.ps1`
- Modify: `qml/player2/Harness.qml`
- Modify: `native/player2/CMakeLists.txt`

**Interfaces:** `Player2Shell` receives `session` and `hostServices`; controls call typed session commands; the ledger has columns `Behavior`, `Production evidence`, `Harness test`, `Eyes-on result`, `Lab status`, `Integrated status`, `Notes`, with status restricted to `NOT RUN`, `FAIL`, `PASS`, or `ACCEPTED EXCEPTION` plus Hemanth's written reference.

- [ ] Seed every row from the design's parity table and map all existing `tests/test_player_*` scripts to their corresponding behavior. No row begins complete.
- [ ] Write QML contract checks that reject timers modifying playback position, raw FFmpeg/mpv strings, production store access and policy implemented in delegates.
- [ ] Implement play/pause, exact/relative seek, speed, frame step, volume/mute, track selection, subtitle delay/style, chapters, fill/aspect, loading/buffering/error, auto-hide, compact folds, hotkeys/context menu and stats.
- [ ] Add QML Test or deterministic harness scenarios for every implemented interaction; run headlessly where possible and eyes-on for scene-graph behavior.
- [ ] Compare against current PlayerPage without modifying it; mark a row PASS only with both required evidence columns populated.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Build Player 2 playback shell`.

### Task 14: Close source, episode, skip and persistence seams with deterministic host services

**Files:**
- Create: `qml/player2/controls/SourceDrawer.qml`
- Create: `qml/player2/controls/EpisodeBrowser.qml`
- Create: `qml/player2/controls/UpNextCard.qml`
- Create: `qml/player2/controls/SkipButton.qml`
- Create: `tests/player2/player2_host_services_test.cpp`
- Create: `tests/player2/player2_orchestration_contract.ps1`
- Modify: `native/player2/host/Player2HostServices.h`
- Modify: `native/player2/host/HarnessHostServices.*`
- Modify: `qml/player2/Player2Shell.qml`
- Modify: `docs/superpowers/specs/player2-parity-ledger.md`

**Interfaces:** typed asynchronous requests/results for adjacent episodes, alternate sources, online subtitles, skip segments, download intent, metadata hydration and progress events. The harness returns fixed fixtures and records requests; the player never searches/ranks sources or writes progress itself.

- [ ] Write host tests for dead/stub source, retry, alternate source preserving identity/position, previous/next, Up Next cancel/confirm, ordered queue, intro/recap/credits manual and automatic skip, resume/start-over and progress cadence.
- [ ] Implement controls and deterministic fixture scenarios. Each async request reaches data, empty or typed error within its scenario deadline.
- [ ] Build and run `player2_host_services_test.exe`; expect `player2_host_services_test: PASS` with every request resolved exactly once.
- [ ] Run the orchestration contract; reject Cinemeta/Torrentio/download-store imports anywhere under `native/player2` or `qml/player2`.
- [ ] Re-run mapped production parity scripts and harness scenarios; update only evidence-backed ledger rows.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Close Player 2 orchestration seams`.

### Task 15: Close window, capture, live/DVR and remaining chrome parity

**Files:**
- Create: `native/player2/core/CaptureService.h`
- Create: `native/player2/core/CaptureService.cpp`
- Create: `qml/player2/controls/CaptureOverlay.qml`
- Create: `qml/player2/controls/LiveGuide.qml`
- Create: `qml/player2/controls/DvrPanel.qml`
- Create: `qml/player2/controls/PauseCard.qml`
- Create: `tests/player2/player2_capture_test.cpp`
- Create: `tests/player2/player2_window_lifecycle_test.cpp`
- Modify: `native/player2/host/Player2HostServices.h`
- Modify: `native/player2/host/HarnessHostServices.*`
- Modify: `qml/player2/Player2Shell.qml`
- Modify: `docs/superpowers/specs/player2-parity-ledger.md`

**Interfaces:** asynchronous screenshot/GIF jobs return typed progress/result and never block playback; window events are commands/facts separated from engine state; live/DVR operations cross host services; power inhibition is requested while playing and released on pause/end/error/close.

- [ ] Write tests for screenshot output, GIF start/stop/abort, minimize/warm resume, PiP/display transition, shutdown, live-edge jump, DVR start/stop and power-inhibit lifetime.
- [ ] Implement remaining UI including pause card, shortcut sheet, clocks, accessibility, close confirmation, download state/action and live exclusions.
- [ ] Run the complete lab parity suite. Require every ledger row `PASS` or a specific `ACCEPTED EXCEPTION` carrying Hemanth's written reference; otherwise Phase D fails.
- [ ] Perform eyes-on comparison at wide, compact and tight sizes and in fullscreen/PiP.
- [ ] Freeze the lab-parity ledger with date, commit and artifact links.
- [ ] Obtain cross-substrate review against the whole design Definition of Done.
- [ ] Commit task paths with message `[Agent 4 (Codex), player] Reach Player 2 lab parity`.

---

## Phase E - Numeric Proof Before Production Touches

### Task 16: Automate and pass every promotion gate

**Files:**
- Create: `tests/player2/player2_seek_soak.ps1`
- Create: `tests/player2/player2_memory_soak.ps1`
- Create: `tests/player2/player2_av_sync_gate.ps1`
- Create: `tests/player2/player2_efficiency_abba.ps1`
- Create: `tests/player2/player2_hardware_matrix.ps1`
- Create: `tests/player2/player2_gate_summary.ps1`
- Create: `docs/superpowers/specs/player2-promotion-gate-report.md`
- Create: `docs/superpowers/specs/player2-runtime-licensing-manifest.md`
- Modify: `docs/superpowers/specs/player2-parity-ledger.md`

**Interfaces:** every script writes machine-readable JSON plus a concise Markdown row; gate summary exits nonzero if any mandatory threshold fails.

- [ ] Add failing schema tests for missing warmup/cooldown, fewer than two contender passes, mismatched media/features, absent normalization mode, stale-generation count, unbounded memory slope or missing hardware identity.
- [ ] Run release-build gates: 30-minute A/V p95 <=40 ms; two-hour Wire soak; 100 scripted local/HTTP seeks; 50 open/close cycles; memory stability; PresentMon/common pacing when elevated; Intel target plus one discrete GPU; Smooth/Light/Full measurements.
- [ ] Repeat the production comparison with Loudness=Smooth and equivalent audio, subtitles and visible chrome. Use ABBA ordering and cooldowns. Treat production-minus-native CPU/memory cautiously and state remaining fairness limits.
- [ ] Require Player 2 to retain at least 25% lower steady GPU busy and/or normalized CPU than mpvqt. A miss is a hard no-go: stop before Task 17 and report it plainly.
- [ ] Record the exact FFmpeg build configuration and redistribution terms, every optional media dependency such as libass, origin/modification status, shipped DLL list and required notices in the runtime licensing manifest. Kodi remains an architecture reference; no Kodi implementation is copied.
- [ ] Run `player2_gate_summary.ps1`; expect `PLAYER2 PROMOTION GATES: PASS` and exit 0.
- [ ] Obtain independent cross-substrate review of raw samples, arithmetic, parity equivalence and threshold conclusion.
- [ ] Commit scripts, frozen raw-data manifest, ledger and report with message `[Agent 4 (Codex), player] Prove Player 2 promotion gates`.

---

## Phase F - Feature-Flagged Colosseum Integration

### Task 17: Integrate the proven library as an opt-in backend

**Precondition:** Task 16 is green. Before modifying shared `native/CMakeLists.txt`, post the exact additive lines in Brotherhood `agents/chat.md`. Kill any running `colosseum.exe` by PID before rebuilding.

**Files:**
- Create: `native/player2/host/ColosseumPlayer2HostServices.h`
- Create: `native/player2/host/ColosseumPlayer2HostServices.cpp`
- Create: `native/player2/PlayerBackendRouter.h`
- Create: `native/player2/PlayerBackendRouter.cpp`
- Create: `tests/player2/player2_backend_router_test.cpp`
- Create: `tests/player2/player2_integrated_contract.ps1`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`
- Modify: `qml/Main.qml`
- Modify: `docs/superpowers/specs/player2-parity-ledger.md`

**Interfaces:** C++ setting `playerBackend = MpvQt | Player2`; default `MpvQt`; router attempts Player 2 capability/open before first visible frame and returns typed `UsePlayer2`, `FallbackToMpvQt(reason)` or terminal error. No mid-session backend swap.

```cpp
enum class PlayerBackend { MpvQt, Player2 };
struct BackendDecision { PlayerBackend backend; QString fallbackReason; };
BackendDecision chooseBackend(const PlaybackRequest &request, const Player2Capabilities &capabilities);
```

- [ ] Write router tests for default mpvqt, explicit opt-in, unsupported adapter, initialization failure before first frame, and failure after first frame. The last case must report/retry as a new session, not hot-swap clocks.
- [ ] Link/register Player 2 only under the promotion build flag. Adapt existing progress, adjacent episode, source, subtitle, download, metadata, live/DVR and power services behind `ColosseumPlayer2HostServices` without moving those policies into the engine.
- [ ] Add a second immersive loader for `Player2Shell.qml`. Preserve the existing mpvqt route unchanged as fallback.
- [ ] Rebuild the committed production tree with `native/build-msvc.bat`; expect `colosseum.exe` and no compile/QML errors.
- [ ] Run mpvqt-default smoke, Player2 opt-in smoke and unsupported-capability fallback smoke.
- [ ] Reopen every host-backed parity row, execute against real services, and update integrated status only from evidence.
- [ ] Commit explicit task paths with message `[Agent 4 (Codex), player] Integrate Player 2 behind opt-in flag`.

## Phase G - Default Promotion

### Task 18: Pass integrated parity, switch the default and preserve rollback

**Files:**
- Create: `tests/player2/player2_integrated_gate.ps1`
- Create: `docs/superpowers/specs/player2-default-promotion-report.md`
- Modify: `native/player2/PlayerBackendRouter.*`
- Modify: `docs/superpowers/specs/player2-parity-ledger.md`

**Interfaces:** default becomes `Player2`; explicit `MpvQt` rollback remains available for one release cycle; fallback reason is visible in diagnostics; removal of mpvqt is outside this plan.

- [ ] Write the failing integrated gate to require every integrated ledger row PASS, all Task 16 numeric gates rerun on the integrated binary, real host-service scenarios, clean fallback and a recorded release/rollback owner.
- [ ] Run the gate before changing the default; expect failure `Player2 is not the default`.
- [ ] Change only the backend default to Player 2. Keep mpvqt linked, routable and tested.
- [ ] Kill `colosseum.exe` by PID, rebuild the committed production tree, and run the integrated gate. Expect `PLAYER2 DEFAULT PROMOTION: PASS`.
- [ ] Perform a final target-laptop Wire playback with Smooth, Light and Full normalization and record dropped frames, A/V p95, GPU/CPU and fallback status.
- [ ] Obtain independent cross-substrate review of the complete diff and written Definition of Done, then Hemanth's smoke approval.
- [ ] Commit explicit paths with message `[Agent 4 (Codex), player] Promote Player 2 as default backend`.
- [ ] Open a separate future decision record for mpvqt removal after one release cycle; do not delete it in this task.

## Final Verification Checklist

- [ ] `git diff --check` returns no errors.
- [ ] Run `$forbidden = @('T'+'BD', 'FIX'+'ME', 'PLACE'+'HOLDER', 'implement'+' later', 'handle'+' errors'); rg -n ($forbidden -join '|') native/player2 qml/player2 tests/player2 docs/superpowers/specs/player2-*`; expect no matches except deliberate contract-test rejection strings.
- [ ] `powershell -NoProfile -File tests/player2/player2_isolation_contract.ps1` passes.
- [ ] All C++/QML/PowerShell Player 2 tests pass from a clean `native/build-player2` release build.
- [ ] The frozen prototype contract still passes.
- [ ] The lab and integrated parity ledgers are 100% closed with no silent deferrals.
- [ ] The Task 16 and Task 18 gate summaries pass from committed binaries and identify the tested hardware/media/settings.
- [ ] Production mpvqt remains the one-release rollback; its eventual removal is not bundled into Player 2 promotion.
- [ ] Licensing manifest covers the actual FFmpeg build, libass or other selected dependencies, redistribution terms and notices.
