# Kodi-inspired D3D11-to-Qt Quick Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and measure an isolated Windows prototype that imports synchronized D3D11 textures into Qt Quick, then drives that bridge with D3D11VA-decoded HEVC frames from Colosseum's completed Wire episode.

**Architecture:** A dedicated D3D11 producer device and Qt Quick's D3D11 consumer device share a three-slot BGRA texture ring plus producer/consumer fences. Stage 1 fills the ring with a GPU diagnostic pattern; Stage 2 replaces the pattern with FFmpeg D3D11VA frames converted from NV12/P010 to BGRA by `ID3D11VideoProcessor`.

**Tech Stack:** C++17, Qt 6.11.1 Quick/QML, D3D11/DXGI 1.2+, FFmpeg shared libraries from `C:/tools/ffmpeg-master-latest-win64-gpl-shared`, CMake/Ninja/MSVC 2022.

## Global Constraints

- Work only in `native/prototypes/d3d11_qtquick_bridge/` plus the named docs and test files.
- Do not modify `colosseum.exe`, `MpvItem`, `PlayerPage.qml`, or the production `native/CMakeLists.txt`.
- No OpenGL, child HWND, CPU video download/upload, software-decode fallback, or silent synchronization fallback.
- Stage 2 may start only after Stage 1 passes.
- The real-media target is `C:/Users/Suprabha/Downloads/Colosseum/The Wire - S4E13 - Final Grades - 20260720_211141.mp4`.
- Audio, subtitles, seeking, streaming and HDR output are out of scope.
- Kodi GPL source is reference material only; copy no implementation.

---

## File map

- `native/prototypes/d3d11_qtquick_bridge/CMakeLists.txt`: isolated target and dependency discovery.
- `native/prototypes/d3d11_qtquick_bridge/README.md`: exact build/run commands and experiment gates.
- `native/prototypes/d3d11_qtquick_bridge/src/slot_ring.h`: platform-independent slot lifecycle.
- `native/prototypes/d3d11_qtquick_bridge/src/shared_bridge.h/.cpp`: D3D devices, shared textures, handles, fences and metrics.
- `native/prototypes/d3d11_qtquick_bridge/src/frame_producer.h/.cpp`: synthetic pattern worker and source contract.
- `native/prototypes/d3d11_qtquick_bridge/src/ffmpeg_hevc_source.h/.cpp`: demux, D3D11VA decode, PTS pacing and video-processor conversion.
- `native/prototypes/d3d11_qtquick_bridge/src/video_bridge_item.h/.cpp`: render-thread import and `QSGSimpleTextureNode` presentation.
- `native/prototypes/d3d11_qtquick_bridge/src/main.cpp`: CLI selection, D3D11 backend and QML registration.
- `native/prototypes/d3d11_qtquick_bridge/qml/Main.qml`: video surface, overlays and telemetry only.
- `native/prototypes/d3d11_qtquick_bridge/tests/slot_ring_test.cpp`: deterministic ownership tests.
- `native/prototypes/d3d11_qtquick_bridge/tests/prototype_contract_test.ps1`: source/build contract gate.
- `docs/superpowers/specs/2026-07-20-kodi-windows-video-architecture-decision.md`: evidence, measurements and decision.

---

### Task 1: Slot ownership contract and isolated build

**Files:**
- Create: `native/prototypes/d3d11_qtquick_bridge/CMakeLists.txt`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/slot_ring.h`
- Create: `native/prototypes/d3d11_qtquick_bridge/tests/slot_ring_test.cpp`
- Create: `native/prototypes/d3d11_qtquick_bridge/tests/prototype_contract_test.ps1`

**Interfaces:**
- Produces: `SlotRing::claimForProducer()`, `publishProduced()`, `acquireLatestForConsumer()`, `retireAfterConsumerSubmission()`, `markConsumerFenceComplete()` and `SlotState`.
- Invariant: a slot in `Displaying` or `Retiring` can never be claimed by the producer.

- [ ] **Step 1: Write the failing lifecycle test**

Create a harness that claims three slots, publishes sequences 1 and 2, switches the consumer from sequence 1 to 2, and asserts the old displayed slot is unavailable until its consumer fence value is marked complete. Add tests for empty-ring and stale-sequence behavior.

- [ ] **Step 2: Run the red test**

Run:

```powershell
cmake -S native/prototypes/d3d11_qtquick_bridge -B native/prototypes/d3d11_qtquick_bridge/build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build native/prototypes/d3d11_qtquick_bridge/build --target slot_ring_test
```

Expected: configuration or compilation fails because `slot_ring.h` has no implementation.

- [ ] **Step 3: Implement the minimal state machine**

Use four explicit states—`Free`, `Producing`, `Ready`, `Displaying`, `Retiring`—with three fixed slots, monotonically increasing producer sequences, and per-slot consumer fence values. Guard public transitions with one mutex; return `std::optional<size_t>` rather than blocking inside the state object.

- [ ] **Step 4: Run the green test and static contract**

Expected output:

```text
slot_ring_test: PASS
prototype_contract_test: PASS
```

- [ ] **Step 5: Commit Task 1 by explicit pathspec**

Commit message: `[Agent 4 (Codex), player] Add D3D11 bridge slot contract`.

---

### Task 2: Stage 1 shared D3D11 bridge

**Files:**
- Create: `native/prototypes/d3d11_qtquick_bridge/src/shared_bridge.h`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/shared_bridge.cpp`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/frame_producer.h`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/frame_producer.cpp`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/video_bridge_item.h`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/video_bridge_item.cpp`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/main.cpp`
- Create: `native/prototypes/d3d11_qtquick_bridge/qml/Main.qml`

**Interfaces:**
- `SharedBridge::initializeConsumer(ID3D11Device*)` returns false unless the Qt and producer adapter LUIDs match and D3D11.4 fences open successfully.
- `FrameProducer::startSynthetic(double fps)` writes only free slots and calls `SharedBridge::publish()` after signalling the producer fence.
- `VideoBridgeItem::updatePaintNode()` waits on the published producer fence and selects a `QSGD3D11Texture::fromNative()` wrapper.
- `SharedBridge::afterFrameSubmitted()` signals the consumer fence and retires the prior slot.

- [ ] **Step 1: Extend the contract test so the target must use Direct3D11 and public native texture import**

Assert source contains `QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11)`,
`QNativeInterface::QSGD3D11Texture::fromNative`, `D3D11_RESOURCE_MISC_SHARED_NTHANDLE`,
`CreateFence`, `OpenSharedFence`, `Signal` and `Wait`. Assert it does not contain
`QQuickFramebufferObject`, `OpenGL`, `av_hwframe_transfer_data` or `CopyResource`.

- [ ] **Step 2: Run the red static test**

Expected: FAIL listing the missing bridge symbols.

- [ ] **Step 3: Implement adapter-locked devices and shared resources**

On scene-graph initialization, obtain Qt's `ID3D11Device` through
`QSGRendererInterface::DeviceResource`, query its adapter LUID, and create the producer device on
that exact `IDXGIAdapter` with `D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT`.
Create three `DXGI_FORMAT_B8G8R8A8_UNORM` render-target/shader-resource textures with NT handles.
Create one shared producer fence and one shared consumer fence. Open all resources on the opposite
device and retain every COM interface for the lifetime of the scene graph.

- [ ] **Step 4: Implement the synthetic producer and render item**

Generate a changing background plus a moving rectangular bar with `ID3D11DeviceContext1::ClearView`.
Signal the producer fence after commands are queued. On the Qt render thread, queue a wait before
sampling, display the native texture through a `QSGSimpleTextureNode`, and signal retirement only
after the replacement frame has been submitted. Expose immutable snapshot metrics to QML.

- [ ] **Step 5: Build and run Gate A**

Run the executable with `--source synthetic --duration 60 --report <absolute-json-path>`. Exercise
window resize and fullscreen during the run. Expected report fields:

```json
{"graphicsApi":"Direct3D11","adapterMatch":true,"sharedFences":true,"cpuTransfers":0,"deviceErrors":0}
```

The visual must show a moving pattern underneath live translucent QML text.

- [ ] **Step 6: Commit Task 2 by explicit pathspec**

Commit message: `[Agent 4 (Codex), player] Prove shared D3D11 textures in Qt Quick`.

---

### Task 3: Stage 2 FFmpeg D3D11VA and video processing

**Files:**
- Create: `native/prototypes/d3d11_qtquick_bridge/src/ffmpeg_hevc_source.h`
- Create: `native/prototypes/d3d11_qtquick_bridge/src/ffmpeg_hevc_source.cpp`
- Modify: `native/prototypes/d3d11_qtquick_bridge/src/frame_producer.h`
- Modify: `native/prototypes/d3d11_qtquick_bridge/src/frame_producer.cpp`
- Modify: `native/prototypes/d3d11_qtquick_bridge/src/main.cpp`
- Modify: `native/prototypes/d3d11_qtquick_bridge/CMakeLists.txt`
- Modify: `native/prototypes/d3d11_qtquick_bridge/tests/prototype_contract_test.ps1`

**Interfaces:**
- `FfmpegHevcSource::open(const QString&, ID3D11Device*)` accepts only a video stream whose decoder selects `AV_PIX_FMT_D3D11`.
- `FfmpegHevcSource::nextFrame(DecodedD3D11Frame&)` returns an AddRef-held decoder texture, array slice, PTS, duration, dimensions, DXGI format and color metadata.
- `FrameProducer::startHevc(const QString&)` converts each returned frame into a free ring slot with `VideoProcessorBlt` and publishes it at source PTS.

- [ ] **Step 1: Extend the contract test for honest hardware-only decoding**

Require `AV_HWDEVICE_TYPE_D3D11VA`, `AV_PIX_FMT_D3D11`, `ID3D11VideoProcessorInputView`,
`ID3D11VideoProcessorOutputView` and `VideoProcessorBlt`. Reject
`av_hwframe_transfer_data`, `sws_scale`, `UpdateSubresource` and a software fallback message.

- [ ] **Step 2: Run the red static test**

Expected: FAIL because the FFmpeg source and video processor are absent.

- [ ] **Step 3: Create FFmpeg's hardware context from the producer device**

Allocate an `AV_HWDEVICE_TYPE_D3D11VA` context, assign an AddRef'd producer `ID3D11Device` to
`AVD3D11VADeviceContext::device`, initialize it, and attach it to the decoder. The `get_format`
callback must accept only `AV_PIX_FMT_D3D11`; failure terminates Gate B. Read the texture from
`AVFrame::data[0]` and array slice from `AVFrame::data[1]` without transferring it.

- [ ] **Step 4: Convert NV12/P010 with the D3D11 video processor**

Create an enumerator for the decoded dimensions/frame rate. Build an input view for the decoder
array slice and an output view for the free BGRA ring slot. Set progressive frame format, source and
destination rectangles, nominal-range and BT.709 color-space metadata from the stream, then call
`VideoProcessorBlt`. Signal/publish through the already-proven ring.

- [ ] **Step 5: Pace video from PTS and record counters**

Use `av_q2d(stream->time_base)` and a monotonic elapsed timer. Wait until each target PTS, count a
frame late when it misses its target by more than one source-frame duration, and skip only when no
slot is safely writable. Record decoded, converted, published, presented, repeated, late, dropped
and CPU-transfer counters.

- [ ] **Step 6: Build and run Gate B on S4E13**

Run `--source hevc --file <S4E13> --duration 300 --report <absolute-json-path>`. Expected hard
facts: codec `hevc`, hardware format `d3d11`, input `NV12` or `P010`, `cpuTransfers=0`,
`softwareFallback=false`, five-minute completion without device error. Capture the actual frame
counters rather than imposing a zero-drop result in advance.

- [ ] **Step 7: Commit Task 3 by explicit pathspec**

Commit message: `[Agent 4 (Codex), player] Drive Qt Quick bridge with D3D11VA HEVC`.

---

### Task 4: Reproducibility and architectural decision

**Files:**
- Create: `native/prototypes/d3d11_qtquick_bridge/README.md`
- Create: `docs/superpowers/specs/2026-07-20-kodi-windows-video-architecture-decision.md`

**Interfaces:**
- README consumes the final CLI and produces exact build/run/interpretation instructions.
- Decision document consumes source citations and Gate A/B JSON reports and produces a ranked architectural recommendation.

- [ ] **Step 1: Write the README from commands proven in this wake**

Include prerequisites, exact configure/build commands, DLL staging, synthetic and HEVC invocations,
counter definitions, failure meanings and the statement that the prototype is not production code.

- [ ] **Step 2: Write the decision document**

Trace Kodi's decoder-surface pool, true-shared/copy fallback, shared fences, D3D11 video processor,
render queue and cadence contracts with local file/line citations. Compare those contracts with
Colosseum/mpvqt and Tankoban's prior sidecar. Record both experiment reports, distinguish GPU-to-GPU
conversion from zero-copy, rank retain-mpvqt versus native-player options, and state the next
smallest production step.

- [ ] **Step 3: Run documentation and scope checks**

Run placeholder scans, `git diff --check`, the slot test, contract test, a fresh build, and both
runtime gates. Confirm the scoped diff does not contain production player files or unrelated user
work.

- [ ] **Step 4: Commit Task 4 by explicit pathspec**

Commit message: `[Agent 4 (Codex), player] Decide Kodi-inspired Colosseum video architecture`.

---

### Task 5: Committed-artifact verification and handoff

**Files:**
- Modify only if evidence correction is required: `docs/superpowers/specs/2026-07-20-kodi-windows-video-architecture-decision.md`

- [ ] **Step 1: Kill only the prototype PID and remove its build directory**

Record the exact PID before stopping it. Delete only
`native/prototypes/d3d11_qtquick_bridge/build` after resolving and checking that path is inside the
prototype directory.

- [ ] **Step 2: Reconfigure and rebuild from the committed tree**

Expected: clean configure, `slot_ring_test: PASS`, `prototype_contract_test: PASS`, and successful
prototype link with the documented Qt/FFmpeg versions.

- [ ] **Step 3: Re-run short committed synthetic and HEVC smokes**

Run 15 seconds synthetic and 60 seconds HEVC from the rebuilt executable. Preserve JSON reports
outside Git and compare their hard invariants with the five-minute evidence.

- [ ] **Step 4: Perform Brotherhood cross-substrate self-review**

Check every written definition-of-done item as MET/PARTIAL/NOT-MET and issue APPROVE or
REQUEST-CHANGES. Correct any documentation claim unsupported by measured output.

- [ ] **Step 5: Write the Agent 4 end-of-wake recap**

Include commits, exact report paths, measurements, architectural decision, known prototype limits
and a next-wake prompt.
