# Panel-Aware Guided Comic Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **GROUNDWORK APPLIED (2026-07-22, Agent 0 — see `2026-07-22-background-work-spine-groundwork.md`):**
> The shared spine this plan assumed now EXISTS on master. Before executing, apply these deltas:
> - **Task 1:** the ONNX dependency half is DONE — `native/cmake/OnnxRuntime.cmake` and the fetch
>   script exist (fetch script lives at `scripts/native/fetch_onnxruntime.ps1`, NOT `scripts/guided/`).
>   Configure ONNX-linking targets with `-DCOLOSSEUM_ENABLE_ONNX=ON`. Task 1 shrinks to
>   `GuidedTypes.h/.cpp` + its harness only.
> - **Task 2:** DONE ENTIRELY — `work::BackgroundWorkCoordinator` is live in `native/work/` with a
>   green harness. Do not recreate it; consume it. Semantics addition: no job is dequeued while
>   pressure is `Suspended`.
> - **Task 6:** do NOT create `native/guided/ModelManifest.h/.cpp`. Use `models::ModelManifest`
>   from `native/models/` (generic core + `extra` for detector fields). Its error codes already
>   emit `model_missing` / `model_checksum_failed`.
> - **Task 7:** do NOT construct a private coordinator in `main.cpp`. One app-owned
>   `work::BackgroundWorkCoordinator *backgroundWork` already exists there (one worker, shared
>   with audiobook alignment) — inject THAT into `PanelAnalysisService`.
> - **DownloadsPage (Task 10 / file-structure entry):** the unified activity row already exists.
>   Do not edit `qml/DownloadsPage.qml`. Publish job state into the `BackgroundActivity` context
>   property (`work::BackgroundActivityRegistry`: `publish/remove` + `pauseRequested/resumeRequested`
>   signals) and your row appears. Required keys: title, stage, progress, paused, canPause.
> - **Priority convention** (shared): current=100, next=90.., previous=80, remainder=10.

**Goal:** Add an offline Guided Reader Style that keeps comic/manga pages intact while Panel Step and Auto Read move a smooth, deterministic camera through safely detected panels.

**Architecture:** A small bundled ONNX detector emits panel/text rectangles; pure C++ planning turns them into one immutable normalized path with conservative whole-page fallback. A resumable native analysis service persists page-incremental results, while a separate native camera controller drives focused QML presentation and leaves `MangaReader.qml` responsible only for reader integration.

**Tech Stack:** C++17, Qt 6.11.1 (Core/Gui/Quick/Qml/Sql/Concurrent), ONNX Runtime 1.25.0 CPU x64, SQLite, QML, PowerShell harnesses, NSIS, Git LFS.

## Global Constraints

- Owner boundaries: Agent 1 owns comic-reader/QML work; Agent 0 owns shared scheduling, build, dependency, `main.cpp`, and `native/CMakeLists.txt` changes.
- Producer != reviewer for every non-trivial slice; reviewer checks the task's written interfaces and done-when evidence.
- English-only product copy; inference itself is visual and language-independent.
- Fully offline at runtime. No cloud inference, Python, PyTorch, Ultralytics, or model downloads may occur in the installed app.
- Bundle ONNX Runtime `v1.25.0` CPU x64 from `onnxruntime-win-x64-1.25.0.zip`, SHA-256 `da753f762bf2400e7191ec594086b186a7051d5af8dc886f6e2020c2403df738`.
- Export from Hugging Face model revision `535bbe1fc1e922d2108f918cd1bce29ba3516196`; source `manga_panel_detector_fp32.pt` SHA-256 is `73e0fb587ea3afe0d17aa9f0c3b1f5a8001b3ecbc3c77091e0730654b0da9146`.
- Development-only export environment pins `ultralytics==8.4.102`; its AGPL tooling never ships or links with Colosseum.
- Model input is one letterboxed `640x640` RGB tensor; classes are `0=panel`, `1=text`; initial detector threshold is `0.25`.
- Guided is the fifth style after Long Strip, Single Page, Double Page, and MangaPlus.
- RTL manga and LTR western comics ship together.
- Source images remain intact. The product must never extract or substitute cropped panel images.
- A confirmed two-page pair is one normalized wide canvas; a stitched wide page remains one canvas.
- Every trusted canvas path is `overview -> ordered panel/internal steps -> overview`.
- Ambiguous, splash, borderless, failed, or low-confidence canvases publish one whole-canvas overview step.
- Panel Step and Auto Read consume the same serialized `GuidedPath`; no second ordering implementation is allowed.
- Manual wheel, drag, zoom, pinch, page turn, scrub, thumbnail/bookmark/chapter jump interrupts animation in the same event and reduces automation UI to `Resume Auto Read`.
- Auto Read reopens paused, continues only into locally available entries, and stops at existing acquisition boundaries.
- Analysis starts automatically, runs one canvas at a time off the GUI thread, prioritizes current -> next four -> previous -> remainder, and is pausable/resumable.
- Planner timing profile is exactly `guided-v1` from the approved design; changing a coefficient changes the profile version and invalidates generated paths.
- Performance gate on Intel UHD 620: warm Guided p95 presented-frame interval <=20 ms; no two consecutive intervals >33.3 ms; interruption stationary by next frame; cached overview starts <=100 ms after texture readiness.
- Preserve all existing reader styles, pairing memory, reading progress, archives, downloads, and unrelated dirty work.
- Every C++ harness snippet below uses the Task 1 `CHECK(condition, message)` macro and returns exit 0 only after printing its named sentinel; every QML harness defines `pass(message)` and `fail(message)` to print the sentinel/error and call `Qt.exit(0/1)`.

## File Structure

Dependency provenance is frozen against the [official ONNX Runtime releases](https://github.com/microsoft/onnxruntime/releases/tag/v1.25.0), the [published model card and files](https://huggingface.co/leoxs22/manga-panel-detector-yolo26n), and the [Ultralytics 8.4.102 package record](https://pypi.org/project/ultralytics/8.4.102/). These are implementation-time tooling/runtime inputs; the installed application remains offline.

### Shared native foundation

- Create `native/work/BackgroundWorkCoordinator.h/.cpp`: domain-neutral single-worker scheduler, pressure yielding, pause/cancel tokens, and presentation status.
- Create `native/guided/GuidedTypes.h/.cpp`: normalized geometry, detector records, paths, statuses, serialization, and stable error codes.
- Create `native/guided/ModelManifest.h/.cpp`: model/runtime location, manifest parsing, and SHA-256 validation.

### Comic analysis domain

- Create `native/guided/PanelDetector.h`: injectable detector interface.
- Create `native/guided/PanelDetectorOnnx.h/.cpp`: 640x640 preprocessing, ONNX CPU inference, and source-coordinate restoration.
- Create `native/guided/PanelPlanner.h/.cpp`: NMS, ordering, association, confidence, framing, timing, and fallback.
- Create `native/guided/PanelMapStore.h/.cpp`: SQLite schema, atomic per-canvas publication, cache lookup, checkpoints, overrides, and progress.
- Create `native/guided/PanelAnalysisService.h/.cpp`: QML facade and background job orchestration.
- Create `native/guided/GuidedCameraController.h/.cpp`: path navigation, animation targets, Auto Read deadlines, interruption, resume, and persisted session state.

### Reader presentation

- Create `qml/guided/GuidedViewport.qml`: intact single/spread canvas with center+scale animation.
- Create `qml/guided/GuidedControls.qml`: Panel Step/Auto Read, speed, status, pause/resume, overrides, and Exit Guided.
- Create `qml/guided/GuidedAnalysisDetails.qml`: per-canvas status and failure reasons.
- Modify `qml/MangaReader.qml`: fifth-style plumbing, stable canvas model, controller/service bindings, input interruption, scrub integration, and style restoration.
- Modify `qml/DownloadsPage.qml`: unified analysis activity row backed by the same service job.

### Build, model, packaging, and proof

- Create `native/cmake/OnnxRuntime.cmake`: imported CPU runtime target pinned to 1.25.0.
- Create `scripts/guided/fetch_onnxruntime.ps1`: verified developer dependency bootstrap.
- Create `scripts/guided/export_panel_model.py` and `scripts/guided/requirements-export.txt`: reproducible PT -> ONNX conversion and generated manifest.
- Create `resources/models/guided/manifest.json`, `MODEL_LICENSE.txt`, `MODEL_CARD.md`, and generated `manga_panel_detector_fp32.onnx`.
- Modify `.gitattributes`, `native/CMakeLists.txt`, `native/main.cpp`, `scripts/installer/package_release.sh`, and `THIRD_PARTY_NOTICES.md`.
- Create focused C++/QML harnesses under `tests/`, fixture manifests under `tests/fixtures/guided/`, and one umbrella `tests/test_guided_reader.ps1`.

---

### Task 1: Pin ONNX Runtime and establish the typed Guided boundary

**Ownership:** Agent 0 (shared build/dependency seam)

**Files:**
- Create: `native/cmake/OnnxRuntime.cmake`
- Create: `scripts/guided/fetch_onnxruntime.ps1`
- Create: `native/guided/GuidedTypes.h`
- Create: `native/guided/GuidedTypes.cpp`
- Create: `tests/guided_types_harness.cpp`
- Modify: `native/CMakeLists.txt:1-16,73-219,220-223`

**Interfaces:**
- Produces: `guided::NormalizedRect`, `Detection`, `CanvasSpec`, `PathStep`, `GuidedPath`, `CanvasStage`, `FallbackCode`, `ReadingDirection`, and JSON serialization functions.
- Produces: imported CMake target `onnxruntime::onnxruntime` with `ONNXRUNTIME_ROOT` defaulting to `C:/tools/onnxruntime-win-x64-1.25.0`.

- [ ] **Step 1: Write the failing type/serialization harness**

```cpp
// tests/guided_types_harness.cpp
#include "guided/GuidedTypes.h"
#include <QJsonDocument>
#include <cstdio>

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

int main() {
    using namespace guided;
    GuidedPath p;
    p.canvasFingerprint = "sha256:abc";
    p.modelVersion = "panel-yolo26n-535bbe1";
    p.plannerVersion = "guided-v1";
    p.outcome = PlanOutcome::Trusted;
    p.steps = {
        {StepKind::Overview, -1, {0, 0, 1, 1}, 0.8, 0.35, 1.0},
        {StepKind::Panel, 7, {0.5, 0, 0.5, 0.5}, 2.4, 0.62, 0.91},
        {StepKind::Overview, -1, {0, 0, 1, 1}, 0.8, 0.62, 1.0}
    };
    const QByteArray bytes = serializePath(p);
    const auto roundTrip = deserializePath(bytes);
    CHECK(roundTrip.has_value(), "path JSON round-trip");
    CHECK(roundTrip->steps.size() == 3, "three immutable steps");
    CHECK(roundTrip->steps[1].camera.contains({0.75, 0.25}), "normalized geometry");
    CHECK(toCode(FallbackCode::LayoutAmbiguous) == "layout_ambiguous", "stable error code");
    std::puts("GUIDED_TYPES_OK");
    return 0;
}
```

- [ ] **Step 2: Register and run the harness to verify it fails**

```cmake
add_executable(guided_types_harness
    ../tests/guided_types_harness.cpp
    guided/GuidedTypes.cpp guided/GuidedTypes.h)
target_include_directories(guided_types_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(guided_types_harness PRIVATE Qt6::Core)
```

Run: `cmake --build native/build-msvc --target guided_types_harness`

Expected: FAIL because `guided/GuidedTypes.h` does not exist.

- [ ] **Step 3: Implement the value types and strict JSON parser**

```cpp
// native/guided/GuidedTypes.h (public shape)
namespace guided {
enum class ReadingDirection { Ltr, Rtl };
enum class CanvasKind { SinglePage, Spread };
enum class DetectionKind { Panel, Text };
enum class StepKind { Overview, Panel, InternalStop };
enum class PlanOutcome { Trusted, Fallback, Failed };
enum class CanvasStage { Waiting, Decoding, Detecting, Planning, Ready, Fallback, Failed };
enum class FallbackCode { None, NoPanels, LayoutAmbiguous, SpreadUncertain,
                          ImageDecodeFailed, ModelMissing, ModelChecksumFailed,
                          InferenceFailed, StoreFailed };

struct NormalizedPoint { double x = 0; double y = 0; };
struct NormalizedRect {
    double x = 0; double y = 0; double width = 0; double height = 0;
    bool isValid() const;
    bool contains(NormalizedPoint point) const;
    NormalizedPoint center() const;
    double area() const;
};
struct Detection { int id = -1; DetectionKind kind; NormalizedRect box; double confidence = 0; };
struct CanvasSpec {
    QString entryId; int canvasIndex = 0; CanvasKind kind = CanvasKind::SinglePage;
    QStringList localFiles; QVector<int> sourcePageIndices; QString fingerprint; QSize sourceSize;
    ReadingDirection direction = ReadingDirection::Rtl;
};
struct PathStep {
    StepKind kind; int sourcePanelId = -1; NormalizedRect camera;
    double holdSecondsAt1x = 0; double transitionSecondsAt1x = 0;
    double plannerConfidence = 0;
};
struct GuidedPath {
    QString canvasFingerprint; QString modelVersion; QString plannerVersion;
    PlanOutcome outcome = PlanOutcome::Failed; FallbackCode reason = FallbackCode::None;
    QVector<PathStep> steps;
};
QByteArray serializePath(const GuidedPath& path);
std::optional<GuidedPath> deserializePath(const QByteArray& bytes);
QString toCode(FallbackCode code);
}
```

- [ ] **Step 4: Add the verified ONNX Runtime bootstrap and imported target**

```powershell
# scripts/guided/fetch_onnxruntime.ps1
$ErrorActionPreference = 'Stop'
$version = '1.25.0'
$expected = 'da753f762bf2400e7191ec594086b186a7051d5af8dc886f6e2020c2403df738'
$zip = Join-Path $env:TEMP "onnxruntime-win-x64-$version.zip"
$dest = "C:\tools\onnxruntime-win-x64-$version"
Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-win-x64-$version.zip" -OutFile $zip
if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'ONNX Runtime checksum mismatch' }
Expand-Archive -LiteralPath $zip -DestinationPath 'C:\tools' -Force
if (!(Test-Path "$dest\lib\onnxruntime.lib")) { throw 'onnxruntime.lib missing after extraction' }
Write-Host "ONNXRUNTIME_READY $dest"
```

```cmake
# native/cmake/OnnxRuntime.cmake
set(ONNXRUNTIME_ROOT "C:/tools/onnxruntime-win-x64-1.25.0" CACHE PATH "ONNX Runtime CPU x64 root")
if(NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h" OR
   NOT EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    message(FATAL_ERROR "ONNX Runtime 1.25.0 missing; run scripts/guided/fetch_onnxruntime.ps1")
endif()
add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
```

- [ ] **Step 5: Build and run the passing harness**

Run:

```powershell
cmake --build native/build-msvc --target guided_types_harness
native/build-msvc/guided_types_harness.exe
```

Expected: `GUIDED_TYPES_OK`, exit 0.

- [ ] **Step 6: Commit only this slice**

```powershell
git add native/cmake/OnnxRuntime.cmake scripts/guided/fetch_onnxruntime.ps1 native/guided/GuidedTypes.h native/guided/GuidedTypes.cpp tests/guided_types_harness.cpp native/CMakeLists.txt
git commit -m "feat(guided): establish typed native foundation"
```

### Task 2: Add the generic resumable background-work coordinator

**Ownership:** Agent 0 (shared architecture)

**Files:**
- Create: `native/work/BackgroundWorkCoordinator.h`
- Create: `native/work/BackgroundWorkCoordinator.cpp`
- Create: `tests/background_work_coordinator_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `work::BackgroundWorkCoordinator::submit(const WorkSpec&, WorkFn)`.
- Produces: `pause`, `resume`, `cancel`, `reprioritize`, and `setPressure(Pressure)`.
- `PanelAnalysisService` later consumes `WorkContext::checkpoint()` and `WorkContext::shouldYield()`.

- [ ] **Step 1: Write a deterministic failing scheduler harness**

```cpp
// tests/background_work_coordinator_harness.cpp
#include "work/BackgroundWorkCoordinator.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QStringList>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    work::BackgroundWorkCoordinator q(1);
    QStringList order;
    QEventLoop loop;
    QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished,
                     &loop, [&](const QString&) { if (order.size() == 3) loop.quit(); });
    auto fn = [&](QString name) { return [&, name](work::WorkContext& c) {
        if (!c.checkpoint()) return work::WorkResult::Cancelled;
        order << name; return work::WorkResult::Completed;
    }; };
    q.submit({"remainder", 10}, fn("remainder"));
    q.submit({"current", 100}, fn("current"));
    q.submit({"next", 90}, fn("next"));
    loop.exec();
    if (order != QStringList{"current", "next", "remainder"}) return 1;
    std::puts("BACKGROUND_WORK_OK");
}
```

- [ ] **Step 2: Build to prove the interface is absent**

Run: `cmake --build native/build-msvc --target background_work_coordinator_harness`

Expected: FAIL on missing `work/BackgroundWorkCoordinator.h`.

- [ ] **Step 3: Implement one-worker priority scheduling with bounded checkpoints**

```cpp
// native/work/BackgroundWorkCoordinator.h (public contract)
namespace work {
enum class Pressure { Normal, LatencySensitive, Suspended };
enum class WorkResult { Completed, Paused, Cancelled, Failed };
enum class Status { Queued, Running, Paused, Completed, Cancelled, Failed };
struct WorkSpec { QString id; int priority = 0; };
class WorkContext {
public:
    bool checkpoint();       // false for cancel; blocks/yields for pause or pressure
    bool shouldYield() const;
private:
    friend class BackgroundWorkCoordinator;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::shared_ptr<std::atomic_bool> paused;
    std::function<Pressure()> pressure;
};
using WorkFn = std::function<WorkResult(WorkContext&)>;
class BackgroundWorkCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit BackgroundWorkCoordinator(int maxWorkers = 1, QObject* parent = nullptr);
    void submit(const WorkSpec&, WorkFn);
    Q_INVOKABLE void pause(const QString& id);
    Q_INVOKABLE void resume(const QString& id);
    Q_INVOKABLE void cancel(const QString& id);
    void reprioritize(const QString& id, int priority);
    void setPressure(Pressure pressure);
    Status status(const QString& id) const;
signals:
    void workStarted(const QString& id);
    void workFinished(const QString& id);
    void workPaused(const QString& id);
    void workFailed(const QString& id, const QString& reason);
};
}
```

Implementation rule: dequeue highest priority only when the current bounded stage ends; never preempt ONNX inside `Run()`. `checkpoint()` waits in 25 ms increments under `LatencySensitive`, blocks under `Suspended`, and wakes immediately for cancel/resume.

- [ ] **Step 4: Add pause/cancel/pressure cases and run the harness**

```cpp
q.pause("next");
CHECK(q.status("next") == work::Status::Paused, "pause visible");
q.resume("next");
q.setPressure(work::Pressure::LatencySensitive);
CHECK(context.shouldYield(), "video/decode pressure reaches worker");
```

Run: `native/build-msvc/background_work_coordinator_harness.exe`

Expected: `BACKGROUND_WORK_OK`, exit 0.

- [ ] **Step 5: Commit**

```powershell
git add native/work tests/background_work_coordinator_harness.cpp native/CMakeLists.txt
git commit -m "feat(work): add resumable background coordinator"
```

### Task 3: Persist page-incremental maps atomically

**Ownership:** Agent 1 (comic analysis domain)

**Files:**
- Create: `native/guided/PanelMapStore.h`
- Create: `native/guided/PanelMapStore.cpp`
- Create: `tests/panel_map_store_harness.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `guided::CanvasSpec`, `Detection`, and `GuidedPath` from Task 1.
- Produces: `lookup`, `publishCanvas`, `saveCheckpoint`, `setOverride`, `retryCanvas`, `rebuildForDirection`, and `jobSummary`.

- [ ] **Step 1: Write failing SQLite cache/override tests**

```cpp
QTemporaryDir dir;
guided::PanelMapStore store(dir.filePath("panel-maps.sqlite"));
CHECK(store.open(), "store opens and migrates");
CHECK(store.beginJob(job), "job stored");
CHECK(store.publishCanvas(canvas, detections, path), "atomic canvas publish");
CHECK(store.lookup(canvas.cacheKey()).path == path, "cache hit returns exact path");
CHECK(store.setOverride(canvas.fingerprint, guided::OverrideKind::WholePage), "override saved");
CHECK(store.lookup(canvas.cacheKey()).path.steps.size() == 1, "whole-page override wins");
store.retryCanvas(canvas.fingerprint);
CHECK(!store.lookup(canvas.cacheKey()).generated.has_value(), "retry clears generated rows only");
std::puts("PANEL_MAP_STORE_OK");
```

- [ ] **Step 2: Build and confirm failure**

Run: `cmake --build native/build-msvc --target panel_map_store_harness`

Expected: FAIL because `PanelMapStore` is undefined.

- [ ] **Step 3: Implement schema version 1 and transaction boundaries**

```sql
CREATE TABLE guided_jobs(
  job_id TEXT PRIMARY KEY, entry_identity TEXT NOT NULL, entry_fingerprint TEXT NOT NULL,
  direction INTEGER NOT NULL, model_version TEXT NOT NULL, planner_version TEXT NOT NULL,
  state INTEGER NOT NULL, paused INTEGER NOT NULL DEFAULT 0,
  priority_canvas INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);
CREATE TABLE guided_canvases(
  canvas_id INTEGER PRIMARY KEY, job_id TEXT NOT NULL, canvas_index INTEGER NOT NULL,
  page_indices TEXT NOT NULL, canvas_kind INTEGER NOT NULL, fingerprint TEXT NOT NULL,
  width INTEGER NOT NULL, height INTEGER NOT NULL, stage INTEGER NOT NULL,
  confidence REAL NOT NULL DEFAULT 0, fallback_code TEXT NOT NULL DEFAULT '',
  override_kind INTEGER NOT NULL DEFAULT 0, UNIQUE(job_id, canvas_index));
CREATE TABLE guided_detections(
  canvas_id INTEGER NOT NULL, detection_id INTEGER NOT NULL, kind INTEGER NOT NULL,
  x REAL NOT NULL, y REAL NOT NULL, w REAL NOT NULL, h REAL NOT NULL,
  confidence REAL NOT NULL, accepted INTEGER NOT NULL, PRIMARY KEY(canvas_id, detection_id));
CREATE TABLE guided_paths(
  canvas_id INTEGER PRIMARY KEY, serialized_json BLOB NOT NULL, published_at INTEGER NOT NULL);
CREATE INDEX guided_canvas_fingerprint_idx ON guided_canvases(fingerprint);
```

Use one named SQLite connection per calling thread. Wrap detections + path + final stage in one transaction; roll back on any failed statement so a partial page never becomes Ready.

- [ ] **Step 4: Add invalidation and direction-rebuild cases**

```cpp
CHECK(store.lookup(key.withPlanner("guided-v2")).cacheMiss(), "planner version invalidates path");
CHECK(store.rawDetections(key.withDirection(ReadingDirection::Ltr)).size() == detections.size(),
      "direction change reuses raw detections");
CHECK(store.overrideFor(changedFingerprint) == OverrideKind::None,
      "source byte change invalidates override");
```

- [ ] **Step 5: Run and commit**

Run: `native/build-msvc/panel_map_store_harness.exe`

Expected: `PANEL_MAP_STORE_OK`, exit 0.

```powershell
git add native/guided/PanelMapStore.* tests/panel_map_store_harness.cpp native/CMakeLists.txt
git commit -m "feat(guided): persist atomic panel maps"
```

### Task 4: Implement deterministic ordering, confidence, and fallback

**Ownership:** Agent 1

**Files:**
- Create: `native/guided/PanelPlanner.h`
- Create: `native/guided/PanelPlanner.cpp`
- Create: `tests/panel_planner_order_harness.cpp`
- Create: `tests/fixtures/guided/geometry_cases.json`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: plain `CanvasSpec` and `QVector<Detection>` only.
- Produces: `PanelPlanner::plan(const CanvasSpec&, const QVector<Detection>&) -> GuidedPath`.
- Produces: `PanelPlanner::rebuild(const CanvasSpec&, const QVector<Detection>&, OverrideKind)` for direction and override changes.

- [ ] **Step 1: Write exact RTL/LTR/NMS/fallback fixture cases**

```json
{
  "cases": [
    {"name":"ltr_grid","direction":"ltr","panels":[[.05,.05,.4,.4,.95],[.55,.05,.4,.4,.94],[.05,.55,.4,.4,.93],[.55,.55,.4,.4,.92]],"order":[0,1,2,3]},
    {"name":"rtl_grid","direction":"rtl","panels":[[.05,.05,.4,.4,.95],[.55,.05,.4,.4,.94],[.05,.55,.4,.4,.93],[.55,.55,.4,.4,.92]],"order":[1,0,3,2]},
    {"name":"duplicate","direction":"ltr","panels":[[.1,.1,.8,.8,.95],[.11,.11,.79,.79,.70]],"order":[0]},
    {"name":"ambiguous_overlap","direction":"ltr","panels":[[.1,.1,.7,.7,.51],[.2,.2,.7,.7,.50]],"fallback":"layout_ambiguous"}
  ]
}
```

- [ ] **Step 2: Write harness assertions before implementation**

```cpp
const GuidedPath ltr = planner.plan(canvas(ReadingDirection::Ltr), gridDetections());
CHECK(panelIds(ltr) == QVector<int>({0,1,2,3}), "LTR top-left order");
const GuidedPath rtl = planner.plan(canvas(ReadingDirection::Rtl), gridDetections());
CHECK(panelIds(rtl) == QVector<int>({1,0,3,2}), "RTL top-right order");
CHECK(rtl.steps.front().kind == StepKind::Overview && rtl.steps.back().kind == StepKind::Overview,
      "overview bookends");
CHECK(planner.plan(canvas(), ambiguous()).outcome == PlanOutcome::Fallback,
      "ambiguity never emits speculative path");
```

- [ ] **Step 3: Run to verify failure**

Run: `cmake --build native/build-msvc --target panel_planner_order_harness`

Expected: FAIL on missing `PanelPlanner`.

- [ ] **Step 4: Implement the pure planner pipeline**

```cpp
GuidedPath PanelPlanner::plan(const CanvasSpec& canvas, const QVector<Detection>& raw) const {
    auto accepted = suppressInvalidAndDuplicates(raw, 0.60); // IoU NMS
    auto panels = only(accepted, DetectionKind::Panel, 0.25);
    if (panels.isEmpty()) return wholePage(canvas, FallbackCode::NoPanels);
    if (!coveragePlausible(panels) || hasUnresolvedOverlap(panels))
        return wholePage(canvas, FallbackCode::LayoutAmbiguous);
    const auto rows = partitionRows(panels, 0.18); // normalized vertical-overlap tolerance
    const auto ordered = orderRows(rows, canvas.direction);
    if (!everyPanelExactlyOnce(ordered, panels))
        return wholePage(canvas, FallbackCode::LayoutAmbiguous);
    return buildTrustedPath(canvas, accepted, ordered);
}
```

Whole-page fallback must serialize exactly one `Overview` step; trusted paths must bookend panel steps with two distinct overview entries.

- [ ] **Step 5: Run deterministic repetition and commit**

Run the harness 20 times and compare its emitted SHA-256:

```powershell
1..20 | ForEach-Object { native/build-msvc/panel_planner_order_harness.exe --digest }
```

Expected: one identical digest and `PANEL_PLANNER_ORDER_OK` every run.

```powershell
git add native/guided/PanelPlanner.* tests/panel_planner_order_harness.cpp tests/fixtures/guided/geometry_cases.json native/CMakeLists.txt
git commit -m "feat(guided): plan safe RTL and LTR panel order"
```

### Task 5: Add camera framing, tall/wide traversal, and guided-v1 timing

**Ownership:** Agent 1

**Files:**
- Modify: `native/guided/PanelPlanner.h`
- Modify: `native/guided/PanelPlanner.cpp`
- Create: `tests/panel_planner_camera_harness.cpp`
- Modify: `tests/fixtures/guided/geometry_cases.json`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `PlannerProfile PanelPlanner::guidedV1()`.
- Produces camera rectangles normalized to the combined canvas and maximum two `InternalStop` entries per source panel.

- [ ] **Step 1: Write failing exact timing/framing tests**

```cpp
const auto profile = PanelPlanner::guidedV1();
CHECK(profile.version == "guided-v1", "versioned profile");
CHECK(fuzzy(profile.overviewHold, 0.8), "overview hold");
CHECK(fuzzy(profile.panelHold(0.25, 2, 0.10), 1.5 + 1.2*0.5 + 0.65*2 + 2.0*0.10),
      "approved area/text equation");
CHECK(profile.transition(1.0, 1.0) == 0.8, "distance clamp");
CHECK(internalStops(planTallPanel()).size() == 1, "tall panel has one traversal");
CHECK(internalStops(planOversizedSeparatedText()).size() <= 2, "hard internal-stop cap");
CHECK(allInsideCanvas(planSpread()), "spread rectangles stay in wide canvas");
```

- [ ] **Step 2: Build and observe the intended failures**

Run: `cmake --build native/build-msvc --target panel_planner_camera_harness`

Expected: FAIL because `guidedV1()` and subdivision are not implemented.

- [ ] **Step 3: Implement the frozen timing profile**

```cpp
double PlannerProfile::panelHold(double areaRatio, int textCount, double textAreaRatio) const {
    const double areaWeight = 1.2 * std::sqrt(std::clamp(areaRatio, 0.0, 1.0));
    const double textWeight = std::min(4.5, 0.65 * textCount
                                            + 2.0 * std::clamp(textAreaRatio, 0.0, 1.0));
    return std::clamp(1.5 + areaWeight + textWeight, 1.5, 8.0);
}
double PlannerProfile::transition(double normalizedCenterTravel, double scaleRatio) const {
    const double d = std::min(1.0, normalizedCenterTravel
                                   + 0.35 * std::abs(std::log2(std::max(1.0, scaleRatio))));
    return std::clamp(0.35 + 0.45 * d, 0.35, 0.8);
}
```

- [ ] **Step 4: Implement restrained framing rules**

```cpp
QVector<NormalizedRect> PanelPlanner::cameraRects(const Panel& p, const QVector<Detection>& text) const {
    if (p.isTiny() && p.shouldMergeWithNeighbor) return {};
    if (p.aspect() >= 2.2) return horizontalTraversal(p.box, p.direction, 2);
    if (p.aspect() <= 1.0 / 2.2) return verticalTraversal(p.box, 2);
    if (separatedTextClusters(p, text) >= 3) return cappedTextTraversal(p.box, text, 3); // panel + max 2 internal
    return {containWithMargin(p.box, 0.045)};
}
```

`horizontalTraversal` follows LTR/RTL publication direction. Vertical traversal always starts at the top. Never create more than three camera rectangles for one panel (panel step plus two internal stops).

- [ ] **Step 5: Run and commit**

Expected: `PANEL_PLANNER_CAMERA_OK`, exit 0.

```powershell
git add native/guided/PanelPlanner.* tests/panel_planner_camera_harness.cpp tests/fixtures/guided/geometry_cases.json native/CMakeLists.txt
git commit -m "feat(guided): frame panels with guided-v1 timing"
```

### Task 6: Export, validate, and run the bundled detector

**Ownership:** Agent 0 for dependency/build; Agent 1 for detector semantics

**Files:**
- Create: `native/guided/PanelDetector.h`
- Create: `native/guided/PanelDetectorOnnx.h`
- Create: `native/guided/PanelDetectorOnnx.cpp`
- Create: `native/guided/ModelManifest.h`
- Create: `native/guided/ModelManifest.cpp`
- Create: `scripts/guided/requirements-export.txt`
- Create: `scripts/guided/export_panel_model.py`
- Create: `resources/models/guided/manifest.json`
- Create: `resources/models/guided/MODEL_LICENSE.txt`
- Create: `resources/models/guided/MODEL_CARD.md`
- Create: `resources/models/guided/manga_panel_detector_fp32.onnx`
- Create: `tests/panel_detector_harness.cpp`
- Create: `tests/fixtures/guided/detector/regular_grid.png`
- Create: `tests/fixtures/guided/detector/expected.json`
- Modify: `.gitattributes`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Produces: `IPanelDetector::detect(const QImage&, const work::WorkContext&) -> DetectorResult`.
- `DetectorResult` contains detections, input/output tensor metadata, and stable failure code; it contains no ordering.

- [ ] **Step 1: Pin the export environment and source revision**

```text
# scripts/guided/requirements-export.txt
ultralytics==8.4.102
onnx==1.21.0
onnxslim==0.1.82
huggingface-hub==0.34.4
```

```python
# scripts/guided/export_panel_model.py (essential path)
from hashlib import sha256
from huggingface_hub import hf_hub_download
from ultralytics import YOLO
import json, pathlib

REV = "535bbe1fc1e922d2108f918cd1bce29ba3516196"
PT_SHA = "73e0fb587ea3afe0d17aa9f0c3b1f5a8001b3ecbc3c77091e0730654b0da9146"
out = pathlib.Path("resources/models/guided")
pt = pathlib.Path(hf_hub_download("leoxs22/manga-panel-detector-yolo26n",
                                  "manga_panel_detector_fp32.pt", revision=REV))
assert sha256(pt.read_bytes()).hexdigest() == PT_SHA
exported = pathlib.Path(YOLO(str(pt)).export(format="onnx", imgsz=640, simplify=True,
                                             dynamic=False, nms=True, opset=18, device="cpu"))
target = out / "manga_panel_detector_fp32.onnx"
target.write_bytes(exported.read_bytes())
manifest = {"schema":1,"modelId":"panel-yolo26n","modelVersion":REV[:7],
            "file":target.name,"sha256":sha256(target.read_bytes()).hexdigest(),
            "input":{"width":640,"height":640,"layout":"NCHW","range":"0..1"},
            "output":{"layout":"NMS","shape":[1,300,6],
                      "fields":["x1","y1","x2","y2","confidence","classId"]},
            "classes":{"0":"panel","1":"text"},"confidenceThreshold":0.25}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
```

- [ ] **Step 2: Track only the generated ONNX binary through LFS and export it**

```gitattributes
resources/models/guided/*.onnx filter=lfs diff=lfs merge=lfs -text
```

Run:

```powershell
py -3.12 -m venv .venv-guided-export
.venv-guided-export/Scripts/pip.exe install -r scripts/guided/requirements-export.txt
.venv-guided-export/Scripts/python.exe scripts/guided/export_panel_model.py
git lfs ls-files resources/models/guided/manga_panel_detector_fp32.onnx
```

Expected: one LFS entry and a manifest containing the generated file's real SHA-256.

- [ ] **Step 3: Write the failing native detector contract**

```cpp
class IPanelDetector {
public:
    virtual ~IPanelDetector() = default;
    virtual DetectorResult detect(const QImage& source, work::WorkContext& context) = 0;
};

const auto result = detector.detect(QImage(fixture("regular_grid.png")), context);
CHECK(result.error == FallbackCode::None, "fixture inference succeeds");
CHECK(count(result.detections, DetectionKind::Panel) == 4, "four panels");
CHECK(allNormalized(result.detections), "letterbox reversal returns normalized source boxes");
CHECK(result.detections == detector.detect(image, context).detections, "CPU output deterministic");
```

- [ ] **Step 4: Implement manifest validation, preprocessing, inference, and postprocessing**

```cpp
DetectorResult PanelDetectorOnnx::detect(const QImage& source, work::WorkContext& context) {
    if (!context.checkpoint()) return cancelledResult();
    const Letterbox box = letterbox(source.convertToFormat(QImage::Format_RGB888), QSize(640,640));
    QVector<float> nchw = toNchw01(box.image);
    auto outputs = m_session.Run(Ort::RunOptions{nullptr}, m_inputNames.data(),
                                 &makeTensor(nchw, {1,3,640,640}), 1,
                                 m_outputNames.data(), m_outputNames.size());
    return decodeNms300x6(outputs.at(0), box, source.size(), 0.25);
}
```

Load `manifest.json` before creating `Ort::Session`; require the frozen `[1,300,6]` output contract and reject any other tensor shape before inference. Reject missing/mismatched files with `model_missing` or `model_checksum_failed`. Set intra-op threads to 1 and graph optimization to `ORT_ENABLE_ALL`. Do not run planner logic here.

- [ ] **Step 5: Run corruption and coordinate-round-trip tests**

Run: `native/build-msvc/panel_detector_harness.exe resources/models/guided tests/fixtures/guided/detector`

Expected: `PANEL_DETECTOR_OK`; a copied model with one flipped byte must return `model_checksum_failed` before session creation.

- [ ] **Step 6: Commit**

```powershell
git add .gitattributes native/guided/PanelDetector* native/guided/ModelManifest* scripts/guided resources/models/guided tests/panel_detector_harness.cpp tests/fixtures/guided/detector native/CMakeLists.txt
git commit -m "feat(guided): bundle offline panel detector"
```

### Task 7: Orchestrate automatic page-first analysis

**Ownership:** Agent 1

**Files:**
- Create: `native/guided/PanelAnalysisService.h`
- Create: `native/guided/PanelAnalysisService.cpp`
- Create: `tests/panel_analysis_service_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp:35-60,370-423`

**Interfaces:**
- Consumes: `BackgroundWorkCoordinator`, `IPanelDetector`, `PanelPlanner`, and `PanelMapStore` by constructor injection.
- Produces QML object `GuidedAnalysis` with `openEntry`, `setVisibleCanvas`, `pathForCanvas`, `pauseJob`, `resumeJob`, `retryCanvas`, `useWholePage`, `useDetectedPanels`, `reverseOrder`, `activeJobs`, and `canvasDetails`.

- [ ] **Step 1: Write a fake-detector lifecycle harness**

```cpp
FakePanelDetector detector;
PanelAnalysisService service(&coordinator, &detector, &planner, &store);
service.openEntry("chapter-1", canvases(12), ReadingDirection::Rtl);
service.setVisibleCanvas("chapter-1", 6);
CHECK(detector.waitForCalls(6), "analysis starts automatically");
CHECK(detector.callOrder().mid(0, 7) == QVector<int>({6,7,8,9,10,5,0}),
      "current next-four previous remainder priority");
service.pauseJob("chapter-1");
CHECK(service.jobSummary("chapter-1").paused, "pause visible");
CHECK(store.lookup(canvasKey(6)).ready(), "finished page published immediately");
```

- [ ] **Step 2: Build and verify failure**

Expected: missing `PanelAnalysisService`.

- [ ] **Step 3: Define the QML-facing API exactly**

```cpp
Q_INVOKABLE void openEntry(const QString& entryId, const QVariantList& canvasModel, bool rtl);
Q_INVOKABLE void closeEntry(const QString& entryId); // analysis continues; drops only reader interest
Q_INVOKABLE void setVisibleCanvas(const QString& entryId, int canvasIndex);
Q_INVOKABLE QVariantMap pathForCanvas(const QString& entryId, int canvasIndex) const;
Q_INVOKABLE QVariantMap jobSummary(const QString& entryId) const;
Q_INVOKABLE QVariantList activeJobs() const;
Q_INVOKABLE QVariantList canvasDetails(const QString& entryId) const;
Q_INVOKABLE void pauseJob(const QString& entryId);
Q_INVOKABLE void resumeJob(const QString& entryId);
Q_INVOKABLE void retryCanvas(const QString& entryId, int canvasIndex);
Q_INVOKABLE void useWholePage(const QString& entryId, int canvasIndex);
Q_INVOKABLE void useDetectedPanels(const QString& entryId, int canvasIndex);
Q_INVOKABLE void reverseOrder(const QString& entryId, int canvasIndex);
signals:
    void jobChanged(const QString& entryId);
    void canvasChanged(const QString& entryId, int canvasIndex);
```

`canvasModel` entries are `{canvasIndex, pageIndices, readingPageIndices, localFiles, kind, width, height}`. `pageIndices` matches physical left-to-right file order; `readingPageIndices` preserves anchor/partner reading order. Reject non-`file:` URLs and nonexistent files; Guided never analyzes remote/undownloaded content. During `Decoding`, compute the fingerprint as SHA-256 over `schema byte + canvas kind + ordered physical page index + page byte count + page bytes` for every source file. QML never invents or persists fingerprints.

- [ ] **Step 4: Implement bounded stages and non-seizing publication**

```cpp
WorkResult PanelAnalysisService::analyze(CanvasSpec canvas, WorkContext& c) {
    publishStage(canvas, CanvasStage::Decoding);
    if (!c.checkpoint()) return WorkResult::Cancelled;
    QImage image = decodeCombinedCanvas(canvas);
    if (image.isNull()) return publishFailure(canvas, FallbackCode::ImageDecodeFailed);
    publishStage(canvas, CanvasStage::Detecting);
    DetectorResult raw = m_detector->detect(image, c);
    if (raw.error != FallbackCode::None) return publishFailure(canvas, raw.error);
    if (!c.checkpoint()) return WorkResult::Cancelled;
    publishStage(canvas, CanvasStage::Planning);
    GuidedPath path = m_planner->plan(canvas, raw.detections);
    return m_store->publishCanvas(canvas, raw.detections, path)
        ? WorkResult::Completed : publishFailure(canvas, FallbackCode::StoreFailed);
}
```

Signals announce readiness but never command a camera move. QML/controller decides when a newly ready path becomes active.

- [ ] **Step 5: Register one application-owned service**

```cpp
auto* guidedWork = new work::BackgroundWorkCoordinator(1, &app);
auto* guidedStore = new guided::PanelMapStore(
    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/guided/panel-maps.sqlite");
auto* guidedDetector = new guided::PanelDetectorOnnx(guided::ModelManifest::defaultBundle(), &app);
auto* guidedAnalysis = new guided::PanelAnalysisService(
    guidedWork, guidedDetector, new guided::PanelPlanner, guidedStore, &app);
engine.rootContext()->setContextProperty(QStringLiteral("GuidedAnalysis"), guidedAnalysis);
```

- [ ] **Step 6: Run and commit**

Expected: `PANEL_ANALYSIS_SERVICE_OK`, including recovery after reconstructing the service against the same temporary DB.

```powershell
git add native/guided/PanelAnalysisService.* tests/panel_analysis_service_harness.cpp native/main.cpp native/CMakeLists.txt
git commit -m "feat(guided): analyze comics in resumable page-first jobs"
```

### Task 8: Build the shared Panel Step/Auto Read camera state machine

**Ownership:** Agent 1

**Files:**
- Create: `native/guided/GuidedCameraController.h`
- Create: `native/guided/GuidedCameraController.cpp`
- Create: `tests/guided_camera_controller_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/main.cpp`

**Interfaces:**
- Consumes immutable serialized paths from `PanelAnalysisService`.
- Produces QML type `Colosseum.Guided 1.0 GuidedCameraController` with `cameraRect`, `transitionMs`, `stepIndex`, `autoRead`, `interrupted`, `speed`, `advance`, `retreat`, `interrupt`, and `resumeAutoRead`.

- [ ] **Step 1: Write failing state-machine tests using a fake clock**

```cpp
FakeClock clock;
GuidedCameraController c(&clock);
c.setPath(pathWithOverviewPanelsOverview());
CHECK(c.stepIndex() == 0 && !c.autoRead(), "opens paused on overview");
c.advance();
CHECK(c.stepIndex() == 1, "Panel Step uses serialized path");
c.setSpeed(2.0);
c.startAutoRead();
clock.advanceMs(expectedHoldMs(path.steps[1], 2.0));
CHECK(c.stepIndex() == 2, "Auto Read advances same path");
c.interrupt(InterruptionReason::Wheel, {0.72,0.21});
CHECK(!c.autoRead() && c.interrupted(), "manual input pauses immediately");
c.resumeAutoRead();
CHECK(c.stepIndex() == nearestPanel(path, {0.72,0.21}), "resume selects nearest panel");
```

- [ ] **Step 2: Implement the exact public controller contract**

```cpp
Q_PROPERTY(QRectF cameraRect READ cameraRect NOTIFY cameraTargetChanged)
Q_PROPERTY(int transitionMs READ transitionMs NOTIFY cameraTargetChanged)
Q_PROPERTY(int stepIndex READ stepIndex NOTIFY stepChanged)
Q_PROPERTY(bool autoRead READ autoRead NOTIFY autoReadChanged)
Q_PROPERTY(bool interrupted READ interrupted NOTIFY interruptedChanged)
Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
Q_PROPERTY(int stopAnimationGeneration READ stopAnimationGeneration NOTIFY stopAnimationGenerationChanged)
Q_ENUM(InterruptionReason)
Q_INVOKABLE void setPath(const QVariantMap& serializedPath, int preferredStep = 0);
Q_INVOKABLE void advance();
Q_INVOKABLE void retreat();
Q_INVOKABLE void startAutoRead();
Q_INVOKABLE void pauseAutoRead();
Q_INVOKABLE void interrupt(int reason, const QPointF& viewportCenter);
Q_INVOKABLE void resumeAutoRead();
signals:
    void requestNextCanvas();
    void requestPreviousCanvas();
    void cameraTargetChanged();
    void stopAnimationGenerationChanged();
```

- [ ] **Step 3: Implement deadline and transition rules**

```cpp
void GuidedCameraController::scheduleCurrentHold() {
    m_timer.stop();
    if (!m_autoRead || m_interrupted || m_path.steps.isEmpty()) return;
    const auto& step = m_path.steps[m_stepIndex];
    m_timer.start(qRound(1000.0 * step.holdSecondsAt1x / m_speed));
}
void GuidedCameraController::interrupt(InterruptionReason why, QPointF center) {
    m_timer.stop(); m_autoRead = false; m_interrupted = true;
    m_manualCenter = center; ++m_stopAnimationGeneration;
    emit stopAnimationGenerationChanged(); emit stateChanged();
}
```

Clamp speed to `[0.5, 2.0]`. Scale both hold and transition duration by `1/speed`. Never auto-start after construction or restored progress.

- [ ] **Step 4: Cover boundaries and persistence payload**

```cpp
CHECK(c.sessionState() == QVariantMap{{"canvasIndex",2},{"stepIndex",4},{"submode","auto"},{"speed",1.25},{"guided",true}},
      "stable persisted state");
c.restoreSession(saved);
CHECK(!c.autoRead() && c.stepIndex() == 4, "restored Auto Read is paused");
```

On final overview advance, emit `requestNextCanvas`; the reader decides whether that is a local next canvas, local next chapter, or an acquisition boundary.

- [ ] **Step 5: Register, run, and commit**

Register the creatable controller once before loading QML:

```cpp
qmlRegisterType<guided::GuidedCameraController>("Colosseum.Guided", 1, 0,
                                                "GuidedCameraController");
```

Expected: `GUIDED_CAMERA_CONTROLLER_OK`, exit 0.

```powershell
git add native/guided/GuidedCameraController.* tests/guided_camera_controller_harness.cpp native/main.cpp native/CMakeLists.txt
git commit -m "feat(guided): drive panel step and auto read from one path"
```

### Task 9: Render the intact canvas with Guided Glide

**Ownership:** Agent 1

**Files:**
- Create: `qml/guided/GuidedViewport.qml`
- Create: `tests/guided_viewport_harness.qml`
- Create: `tests/test_guided_viewport.ps1`

**Interfaces:**
- Consumes: `canvas` map, normalized `cameraRect`, `transitionMs`, and `stopAnimationGeneration`.
- Produces: `manualInterrupted(int reason, point center)` and `textureReady()`; never computes reading order.

- [ ] **Step 1: Write a QML harness that proves intact sources and camera math**

```qml
GuidedViewport {
    id: view
    width: 800; height: 600
    canvas: ({ kind: "spread", files: [leftUrl, rightUrl], sourceWidths: [400,400], sourceHeights:[600,600] })
    cameraRect: Qt.rect(0.5, 0.0, 0.5, 1.0)
    transitionMs: 350
    onTextureReady: {
        if (sourceItemCount !== 2) fail("spread must retain two original Images")
        if (combinedCanvasWidth !== 800) fail("spread is one wide coordinate space")
        if (cropItemCount !== 0) fail("no panel crop substitution")
        pass("GUIDED_VIEWPORT_OK")
    }
}
```

- [ ] **Step 2: Run and verify failure**

Run: `powershell -NoProfile -File tests/test_guided_viewport.ps1`

Expected: FAIL because `qml/guided/GuidedViewport.qml` is missing.

- [ ] **Step 3: Implement center+scale animation against one canvas item**

```qml
Item {
    id: root
    property var canvas
    property rect cameraRect: Qt.rect(0,0,1,1)
    property int transitionMs: 350
    property int stopAnimationGeneration: 0
    signal textureReady()
    signal manualInterrupted(int reason, point center)

    Item {
        id: sourceCanvas
        width: root.width / Math.max(0.0001, root.cameraRect.width)
        height: root.height / Math.max(0.0001, root.cameraRect.height)
        x: root.width/2 - (root.cameraRect.x + root.cameraRect.width/2) * width
        y: root.height/2 - (root.cameraRect.y + root.cameraRect.height/2) * height
        Behavior on x { NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Behavior on y { NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Behavior on width { NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Behavior on height { NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Repeater { model: root.canvas.files; delegate: Image { source: modelData; fillMode: Image.PreserveAspectFit } }
    }
}
```

Lay spread images side by side in physical left/right order supplied by `canvas`. `stopAnimationGeneration` disables behaviors for one binding turn and freezes current geometry so interruption is stationary by the next frame.

- [ ] **Step 4: Add wheel/drag/pinch interruption reporting**

```qml
WheelHandler { onWheel: (e) => root.manualInterrupted(1, root.viewportCenterNormalized()) }
DragHandler { onActiveChanged: if (active) root.manualInterrupted(2, root.viewportCenterNormalized()) }
PinchHandler { onActiveChanged: if (active) root.manualInterrupted(3, root.viewportCenterNormalized()) }
```

Handlers report input; `MangaReader` decides whether to pan/zoom and controller decides automation state.

- [ ] **Step 5: Run and commit**

Expected: `GUIDED_VIEWPORT_OK`, exit 0.

```powershell
git add qml/guided/GuidedViewport.qml tests/guided_viewport_harness.qml tests/test_guided_viewport.ps1
git commit -m "feat(guided): render intact pages with guided glide"
```

### Task 10: Integrate Guided as the fifth MangaReader style

**Ownership:** Agent 1 (exclusive reader surface)

**Files:**
- Create: `qml/guided/GuidedControls.qml`
- Create: `qml/guided/GuidedAnalysisDetails.qml`
- Modify: `qml/MangaReader.qml:65-157,324-401,450-495,780-1060,1272-1346,1360-1540,1720-1735`
- Create: `tests/guided_manga_reader_harness.qml`
- Create: `tests/guided_manga_reader_harness_main.cpp`
- Create: `tests/test_guided_manga_reader.ps1`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes QML context `GuidedAnalysis` and registered `GuidedCameraController`.
- Produces `buildGuidedCanvases()`, `enterGuided()`, `exitGuided()`, `activateGuidedCanvas()`, `interruptGuided(reason)`, `guidedAdvanceCanvas()`, and `guidedRetreatCanvas()`.

- [ ] **Step 1: Expand the reader harness fake with exact Guided APIs**

```qml
component FakeGuidedAnalysis: QtObject {
    property var opened: null
    property var paths: ({})
    signal jobChanged(string entryId)
    signal canvasChanged(string entryId, int canvasIndex)
    function openEntry(id, canvases, rtl) { opened = {id:id, canvases:canvases, rtl:rtl} }
    function closeEntry(id) {}
    function setVisibleCanvas(id, index) {}
    function pathForCanvas(id, index) { return paths[index] || wholePagePath(index) }
    function jobSummary(id) { return {stage:"planning",ready:1,total:3,paused:false} }
    function pauseJob(id) {}
    function resumeJob(id) {}
    function retryCanvas(id,index) {}
    function useWholePage(id,index) {}
    function useDetectedPanels(id,index) {}
    function reverseOrder(id,index) {}
}
```

`guided_manga_reader_harness_main.cpp` registers `GuidedCameraController`, exposes the fake as QML context property `GuidedAnalysis`, and loads the harness offscreen. Harness assertions: the selector has five entries; enter/exit preserves page and previous style; RTL/LTR canvas order is correct; an ordinary two-page `Engine.getTwoPagePair` pair becomes one spread canvas; stitched-wide `kind:"spread"` stays one-file/one-canvas.

- [ ] **Step 2: Run first to verify the fifth style is absent**

Expected: FAIL `guided style missing`.

- [ ] **Step 3: Add stable style and canvas-model plumbing**

```qml
property string preGuidedStyle: ""
readonly property bool guided: style === "guided"
readonly property bool paged: guided || style === "single_page" || isDouble
property var guidedService: typeof GuidedAnalysis !== "undefined" ? GuidedAnalysis : null
property var guidedCanvases: []
property int guidedCanvasIndex: 0

function buildGuidedCanvases() {
    var result = [], anchor = 0
    while (anchor < max) {
        var p = Engine.getTwoPagePair(anchor, ctx())
        var readOrder = [p.anchorIndex]
        if (p.partnerIndex !== null) readOrder.push(p.partnerIndex)
        var physicalOrder = readOrder.slice(0)
        if (physicalOrder.length === 2 && rtl) physicalOrder.reverse()
        var files = [], widths = [], heights = []
        for (var i = 0; i < physicalOrder.length; ++i) {
            var idx = physicalOrder[i], d = dims[idx] || {w:800,h:1200}
            files.push(pagesModel[idx].url); widths.push(d.w); heights.push(d.h)
        }
        result.push({canvasIndex:result.length, pageIndices:physicalOrder,
                     readingPageIndices:readOrder,
                     localFiles:files, files:files,
                     kind:readOrder.length === 2 ? "spread" : "single",
                     sourceWidths:widths, sourceHeights:heights})
        anchor = readOrder[readOrder.length - 1] + 1
    }
    return result
}

function canvasIndexContainingPage(pageIndex) {
    for (var i = 0; i < guidedCanvases.length; ++i)
        if (guidedCanvases[i].readingPageIndices.indexOf(pageIndex) >= 0) return i
    return 0
}
function firstPageOfCanvas(canvasIndex) {
    var c = guidedCanvases[canvasIndex]
    return c && c.readingPageIndices.length ? c.readingPageIndices[0] : 0
}
function activateGuidedCanvas(canvasIndex, preferredStep) {
    if (!guidedService || canvasIndex < 0 || canvasIndex >= guidedCanvases.length) return
    guidedCanvasIndex = canvasIndex
    page = firstPageOfCanvas(canvasIndex) + 1
    guidedService.setVisibleCanvas(curChapterId, canvasIndex)
    guidedCamera.setPath(guidedService.pathForCanvas(curChapterId, canvasIndex), preferredStep || 0)
}

function enterGuided() {
    preGuidedStyle = style === "guided" ? (prefs.guided_previous_style || "long_strip") : style
    prefs.guided_previous_style = preGuidedStyle
    var keepPage = page
    styleOv = "guided"; prefs.reading_style = "guided"; saveSeriesPrefs()
    guidedCanvases = buildGuidedCanvases() // walks Engine.getTwoPagePair(anchor, ctx())
    guidedCanvasIndex = canvasIndexContainingPage(keepPage - 1)
    if (guidedService) guidedService.openEntry(curChapterId, guidedCanvases, rtl)
    activateGuidedCanvas(guidedCanvasIndex)
}
function exitGuided() {
    var keepPage = firstPageOfCanvas(guidedCanvasIndex) + 1
    if (guidedService) guidedService.closeEntry(curChapterId)
    styleOv = preGuidedStyle.length ? preGuidedStyle : "long_strip"
    prefs.reading_style = styleOv; page = keepPage; saveSeriesPrefs()
}
```

Add `property string guided_previous_style: "long_strip"` to the existing `Settings` block. When persisted `reading_style` is already `guided`, call `enterGuided()` after `load()` has populated `pagesModel`; this restores Guided active state while the controller still restores Auto Read paused.

Add `{v:"guided",t:"Guided"}` after MangaPlus and `modeShort("guided") -> "Guided"`. `setStyle("guided")` calls `enterGuided`; any other style while Guided calls `exitGuided` then applies the requested style.

- [ ] **Step 4: Mount viewport and controls without changing existing surfaces**

```qml
GuidedCameraController {
    id: guidedCamera
    onRequestNextCanvas: reader.guidedAdvanceCanvas()
    onRequestPreviousCanvas: reader.guidedRetreatCanvas()
}
GuidedViewport {
    visible: reader.guided && reader.max > 0
    anchors.fill: parent
    canvas: reader.guidedCanvases[reader.guidedCanvasIndex] || ({files:[]})
    cameraRect: guidedCamera.cameraRect
    transitionMs: guidedCamera.transitionMs
    stopAnimationGeneration: guidedCamera.stopAnimationGeneration
    onManualInterrupted: (reason, center) => guidedCamera.interrupt(reason, center)
}
GuidedControls {
    visible: reader.guided
    controller: guidedCamera
    analysis: reader.guidedService ? reader.guidedService.jobSummary(reader.curChapterId) : ({})
    resumeOnly: guidedCamera.interrupted
}
```

- [ ] **Step 5: Wire every interruption source, including the existing scrub bar**

```qml
function interruptGuided(reason) {
    if (guided) guidedCamera.interrupt(reason, guidedViewport.viewportCenterNormalized())
}
// scrub.seek()
if (reader.guided) reader.interruptGuided(GuidedCameraController.Scrub)
// jumpToPage(), chapter grid, bookmarks, Home/End, turnNext/turnPrev
if (reader.guided) reader.interruptGuided(GuidedCameraController.Navigation)
```

Keep the current page scrub bar. In Guided, its fraction remains canvas/page progress; drag previews page number, then activates the target canvas and whole-page overview. It must not become a panel scrubber.

- [ ] **Step 6: Implement overview/canvas/chapter boundaries**

```qml
function guidedAdvanceCanvas() {
    if (guidedCanvasIndex + 1 < guidedCanvases.length) {
        guidedCanvasIndex++; activateGuidedCanvas(guidedCanvasIndex); return
    }
    if (hasNewer && entryReady(String(chapters[curIndex - 1].id))) {
        pendingGuidedContinue = true; goNextChapter(); return
    }
    guidedCamera.pauseAutoRead()
    showToast("Next chapter is not available offline")
}
function guidedRetreatCanvas() {
    if (guidedCanvasIndex > 0) {
        var target = guidedCanvasIndex - 1
        var path = guidedService.pathForCanvas(curChapterId, target)
        activateGuidedCanvas(target, path.steps && path.steps.length ? path.steps.length - 1 : 0)
        return
    }
    guidedCamera.pauseAutoRead()
    if (hasOlder && entryReady(String(chapters[curIndex + 1].id))) {
        pendingGuidedBacktrack = true; goPrevChapter(true)
    }
}
```

After chapter change, rebuild canvases, open analysis, land at canvas 0 overview, and resume only if continuation was already active. A fresh/reopened reader always remains paused.

- [ ] **Step 7: Persist and restore Guided progress**

Extend the existing per-series record with additive keys:

```qml
m[seriesId].guided = { canvas: guidedCanvasIndex, step: guidedCamera.stepIndex,
                       submode: guidedCamera.autoRead ? "auto" : "step",
                       speed: guidedCamera.speed, active: guided }
```

Ignore missing keys for backward compatibility. Restore canvas/step/speed but call `pauseAutoRead()` unconditionally.

- [ ] **Step 8: Run reader regression harnesses**

Run:

```powershell
powershell -NoProfile -File tests/test_guided_manga_reader.ps1
powershell -NoProfile -File tests/test_manga_tankoban_mode.ps1
powershell -NoProfile -File tests/test_taskbar_immersive_readers_p0.ps1
```

Expected: `GUIDED_MANGA_READER_OK`; all existing tests exit 0.

- [ ] **Step 9: Commit**

```powershell
git add qml/guided/GuidedControls.qml qml/guided/GuidedAnalysisDetails.qml qml/MangaReader.qml tests/guided_manga_reader_harness.qml tests/guided_manga_reader_harness_main.cpp tests/test_guided_manga_reader.ps1 native/CMakeLists.txt
git commit -m "feat(reader): add Guided panel step and auto read"
```

### Task 11: Surface the same job in Reader and global activity

**Ownership:** Agent 1 for reader UI; Agent 5 coordination required for `DownloadsPage.qml`

**Files:**
- Modify: `qml/guided/GuidedControls.qml`
- Modify: `qml/guided/GuidedAnalysisDetails.qml`
- Modify: `qml/DownloadsPage.qml:26-119,392-430`
- Create: `tests/guided_activity_harness.qml`
- Create: `tests/test_guided_activity.ps1`

**Interfaces:**
- Consumes `GuidedAnalysis.activeJobs()` rows: `{id,title,state,paused,ready,total,currentCanvas,failureCode}`.
- Both surfaces invoke the same `pauseJob(id)` and `resumeJob(id)` methods.

- [ ] **Step 1: Write a fake-service dual-surface harness**

```qml
property var jobs: [{id:"chapter-1",title:"Issue 1",state:"detecting",paused:false,ready:3,total:20,currentCanvas:4}]
function activeJobs() { return jobs }
function pauseJob(id) { jobs[0].paused = true; jobChanged(id) }
function resumeJob(id) { jobs[0].paused = false; jobChanged(id) }
```

Assert the Reader Details and Downloads activity row show `3 / 20 pages ready`; pausing either surface updates both after `jobChanged`.

- [ ] **Step 2: Add refresh bindings to DownloadsPage**

```qml
property var guidedJobs: []
function refreshGuidedJobs() {
    guidedJobs = typeof GuidedAnalysis !== "undefined" ? GuidedAnalysis.activeJobs() : []
}
Connections {
    target: typeof GuidedAnalysis !== "undefined" ? GuidedAnalysis : null
    function onJobChanged(entryId) { root.refreshGuidedJobs() }
}
```

- [ ] **Step 3: Add one quiet `Background analysis` group**

```qml
Column {
    visible: root.guidedJobs.length > 0
    Text { text: "Background analysis" }
    Repeater {
        model: root.guidedJobs
        delegate: ActivityRow {
            title: modelData.title
            detail: modelData.ready + " / " + modelData.total + " pages ready · " + modelData.state
            actionText: modelData.paused ? "Resume" : "Pause"
            onAction: modelData.paused ? GuidedAnalysis.resumeJob(modelData.id)
                                       : GuidedAnalysis.pauseJob(modelData.id)
        }
    }
}
```

Operational failures remain visible until Retry or source bytes change. Whole-page fallback is shown as usable, not failed.

- [ ] **Step 4: Run and commit**

Expected: `GUIDED_ACTIVITY_OK`, and `tests/downloads_page_load_harness.qml` still reaches `LOADER READY`.

```powershell
git add qml/guided qml/DownloadsPage.qml tests/guided_activity_harness.qml tests/test_guided_activity.ps1
git commit -m "feat(guided): expose visible background analysis status"
```

### Task 12: Bundle the runtime, model, manifest, and notices in every installer

**Ownership:** Agent 0 (release/build)

**Files:**
- Modify: `scripts/installer/package_release.sh:24-64`
- Modify: `THIRD_PARTY_NOTICES.md`
- Create: `tests/test_guided_installer_payload.ps1`
- Modify: `native/guided/ModelManifest.cpp`

**Interfaces:**
- Produces installed payload paths:
  - `native/build-msvc/onnxruntime.dll`
  - `resources/models/guided/manga_panel_detector_fp32.onnx`
  - `resources/models/guided/manifest.json`
  - `resources/models/guided/MODEL_LICENSE.txt`

- [ ] **Step 1: Write the failing staged-payload contract**

```powershell
$required = @(
  'native/build-msvc/onnxruntime.dll',
  'resources/models/guided/manga_panel_detector_fp32.onnx',
  'resources/models/guided/manifest.json',
  'resources/models/guided/MODEL_LICENSE.txt'
)
foreach ($rel in $required) {
  if (!(Test-Path -LiteralPath (Join-Path $Stage $rel))) { throw "missing guided payload: $rel" }
}
$manifest = Get-Content (Join-Path $Stage 'resources/models/guided/manifest.json') -Raw | ConvertFrom-Json
$hash = (Get-FileHash (Join-Path $Stage "resources/models/guided/$($manifest.file)") -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $manifest.sha256) { throw 'staged model checksum mismatch' }
Write-Host 'GUIDED_INSTALLER_PAYLOAD_OK'
```

- [ ] **Step 2: Make staging copy smudged LFS bytes and ORT explicitly**

```bash
echo "[5/8] bundle ONNX Runtime CPU"
cp "/c/tools/onnxruntime-win-x64-1.25.0/lib/onnxruntime.dll" "$STAGE/native/build-msvc/onnxruntime.dll"

echo "[6/8] bundle Guided model from working tree (not git-archive LFS pointer)"
rm -rf "$STAGE/resources/models/guided"
mkdir -p "$STAGE/resources/models"
cp -r "$REPO/resources/models/guided" "$STAGE/resources/models/"

powershell.exe -NoProfile -File "$REPO/tests/test_guided_installer_payload.ps1" -Stage "$(cygpath -w "$STAGE")"
```

Renumber the existing Stremio and makensis stages to `[7/8]` and `[8/8]`.

- [ ] **Step 3: Document attribution and runtime search paths**

Add notices for ONNX Runtime MIT, the Apache-2.0 model, and Manga109-s training-data attribution. `ModelManifest::defaultBundle()` searches in order:

```text
COLOSSEUM_GUIDED_MODEL_DIR
<repo-or-install-root>/resources/models/guided
<applicationDir>/../../../resources/models/guided
```

Do not silently search user Downloads or the network.

- [ ] **Step 4: Build a staged installer and verify payload**

Run:

```powershell
bash scripts/installer/package_release.sh 0.2-guided-test
powershell -NoProfile -File tests/test_guided_installer_payload.ps1 -Stage dist/stage
```

Expected: `GUIDED_INSTALLER_PAYLOAD_OK` and `dist/Colosseum-0.2-guided-test-setup.exe` exists.

- [ ] **Step 5: Commit**

```powershell
git add scripts/installer/package_release.sh THIRD_PARTY_NOTICES.md tests/test_guided_installer_payload.ps1 native/guided/ModelManifest.cpp
git commit -m "build(guided): bundle offline model and runtime"
```

### Task 13: Lock the full fixture, resilience, and performance gate

**Ownership:** Agent 1 with Agent 0 verification coordination

**Files:**
- Create: `tests/fixtures/guided/manifest.json`
- Create: `tests/guided_fixture_acceptance.cpp`
- Create: `tests/guided_resilience_harness.cpp`
- Create: `tests/guided_performance_harness.qml`
- Create: `tests/test_guided_reader.ps1`
- Create: `docs/verification/guided-reader-acceptance.md`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Umbrella suite owns the release sentinels `GUIDED_FIXTURES_OK`, `GUIDED_RESILIENCE_OK`, `GUIDED_QML_OK`, and `GUIDED_READER_OK`.

- [ ] **Step 1: Add legally redistributable fixture coverage**

`tests/fixtures/guided/manifest.json` must name exact expected outcome/order/camera classes for:

```json
{
  "fixtures": [
    {"file":"rtl-grid.png","direction":"rtl","outcome":"trusted","order":[1,0,3,2]},
    {"file":"ltr-grid.png","direction":"ltr","outcome":"trusted","order":[0,1,2,3]},
    {"file":"tall.png","outcome":"trusted","maxInternalStops":1},
    {"file":"wide.png","outcome":"trusted","maxInternalStops":1},
    {"file":"tiny-adjacent.png","outcome":"trusted","expectsMerge":true},
    {"file":"borderless.png","outcome":"fallback","reason":"layout_ambiguous"},
    {"file":"splash.png","outcome":"fallback","pathSteps":1},
    {"file":"spread-left.png","partner":"spread-right.png","outcome":"trusted","canvasKind":"spread"},
    {"file":"cross-seam.png","outcome":"trusted","crossesSeam":true},
    {"file":"corrupt.bin","outcome":"failed","reason":"image_decode_failed"}
  ]
}
```

- [ ] **Step 2: Enforce planner/detector acceptance**

```cpp
CHECK(conventionalExactOrderRate >= 0.95, "95 percent exact conventional order");
CHECK(ambiguousUnsafePathCount == 0, "all ambiguous fixtures fall back");
CHECK(cachedSecondPassInferenceCount == 0, "cache reopening performs zero inference");
CHECK(panelStepSerializedPath == autoReadSerializedPath, "one path for both modes");
```

- [ ] **Step 3: Enforce recovery and non-seizing behavior**

```cpp
CHECK(reopenAfterKilledPlanning.stage != CanvasStage::Ready, "partial path never published");
CHECK(reopenAfterPublishedReady.path == originalPath, "published page survives restart");
CHECK(cameraMovesOnBackgroundReady == 0, "result arrival never seizes viewport");
CHECK(ordinaryReaderAvailableWhenModelMissing, "model failure cannot block reading");
```

- [ ] **Step 4: Add frame-presentation trace collection**

The QML harness records `FrameAnimation` timestamps during 100 warm transitions and writes JSON. The PowerShell gate calculates:

```powershell
$p95 = ($intervals | Sort-Object)[[math]::Floor(($intervals.Count - 1) * 0.95)]
if ($p95 -gt 20.0) { throw "Guided p95 frame interval $p95 ms exceeds 20 ms" }
for ($i=1; $i -lt $intervals.Count; $i++) {
  if ($intervals[$i-1] -gt 33.3 -and $intervals[$i] -gt 33.3) { throw 'two consecutive missed-frame intervals' }
}
```

Record cached-start latency, input-to-stationary frames, Long Strip wheel trace, and Theatre playback cadence in the same evidence file.

- [ ] **Step 5: Create the umbrella runner**

```powershell
$targets = @('guided_types_harness','background_work_coordinator_harness','panel_map_store_harness',
             'panel_planner_order_harness','panel_planner_camera_harness','panel_detector_harness',
             'panel_analysis_service_harness','guided_camera_controller_harness','guided_fixture_acceptance',
             'guided_resilience_harness')
cmake --build native/build-msvc --target $targets
if ($LASTEXITCODE -ne 0) { throw 'Guided native build failed' }
foreach ($exe in $targets) {
  & "native/build-msvc/$exe.exe"
  if ($LASTEXITCODE -ne 0) { throw "$exe failed" }
}
powershell -NoProfile -File tests/test_guided_viewport.ps1
powershell -NoProfile -File tests/test_guided_manga_reader.ps1
powershell -NoProfile -File tests/test_guided_activity.ps1
Write-Host 'GUIDED_READER_OK'
```

- [ ] **Step 6: Run full existing regression**

Run:

```powershell
powershell -NoProfile -File tests/test_guided_reader.ps1
powershell -NoProfile -File tests/test_manga_tankoban_native.ps1
powershell -NoProfile -File tests/test_manga_tankoban_mode.ps1
powershell -NoProfile -File tests/test_taskbar_immersive_readers_p0.ps1
cmake --build native/build-msvc --target colosseum
```

Expected: all exit 0, final `GUIDED_READER_OK`, and production target links `onnxruntime.dll` without QML warnings.

- [ ] **Step 7: Complete Hemanth's eyes-on matrix**

Record pass/fail and screenshot/trace paths in `docs/verification/guided-reader-acceptance.md` for:

```text
RTL conventional page | LTR conventional page | tall | wide | confirmed spread
deliberate fallback | wheel interruption | drag interruption | Resume Auto Read
scrub interruption | cached reopen | local chapter continuation | unavailable boundary stop
analysis pause/resume | model-missing ordinary-reader fallback
```

- [ ] **Step 8: Commit final evidence**

```powershell
git add tests/fixtures/guided tests/guided_fixture_acceptance.cpp tests/guided_resilience_harness.cpp tests/guided_performance_harness.qml tests/test_guided_reader.ps1 docs/verification/guided-reader-acceptance.md native/CMakeLists.txt
git commit -m "test(guided): lock reader acceptance and performance"
```

## Final Definition-of-Done Review

- [ ] `Guided` is visibly the fifth style and exiting restores the prior style/canvas.
- [ ] Both RTL and LTR exact-order fixtures meet the 95% conventional-page gate.
- [ ] Original images remain the only visual source; no panel crop files/components exist.
- [ ] Trusted paths are overview -> panels/internal stops -> overview; fallback paths are one overview.
- [ ] Double-page pairs and stitched spreads use one wide normalized canvas.
- [ ] Panel Step and Auto Read deserialize byte-identical paths.
- [ ] Guided Glide uses InOutCubic, 0.35-0.8 second base transitions, and `guided-v1` holds.
- [ ] Every manual input class pauses immediately and leaves only `Resume Auto Read`.
- [ ] Existing page scrub remains visible and participates in interruption/seek behavior.
- [ ] Auto Read restores paused, continues across local entries, and stops at acquisition boundaries.
- [ ] Analysis is automatic, one-canvas-at-a-time, page-first, resumable, pausable, cached, and visible in both surfaces.
- [ ] Use whole page, Retry detection, and Reverse panel order persist against exact fingerprints.
- [ ] Model/runtime absence or corruption produces stable visible errors without blocking normal reading.
- [ ] Installer contains the actual LFS model bytes, validated manifest, ORT DLL, licenses, and notices.
- [ ] UHD 620 performance, interruption, cached-start, Long Strip regression, and Theatre cadence gates pass with evidence.
- [ ] A different substrate/model reviews the completed diff against this checklist before shipping.

## Execution Order and Checkpoints

Tasks 1-3 establish shared contracts and persistence. Tasks 4-5 prove deterministic planning before ML output exists. Task 6 enables real inference. Tasks 7-8 connect background work and camera state without UI. Tasks 9-11 expose the approved experience. Tasks 12-13 close packaging, resilience, regression, and visual acceptance.

Stop for Hemanth eyes-on checkpoints after Task 9 (camera grammar on fixtures), Task 10 (complete Guided reader UX), and Task 13 (release candidate on Intel UHD 620). Do not start a later checkpoint group while an earlier P0/P1 review finding remains open.
