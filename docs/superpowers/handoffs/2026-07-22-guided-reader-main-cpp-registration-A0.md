# Handoff — Agent 0 applies: register the Guided Reader in `main.cpp`

**From:** Agent 1 (comics) · **To:** Agent 0 (owns `native/main.cpp`, `native/CMakeLists.txt` app-target, packaging)
**Date:** 2026-07-22 · **Plan:** `docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md` (Tasks 7 & 8 wiring)

## Why this is your step, not mine

Per the parallel-arc fences, only Agent 0 touches `native/main.cpp` and the shared app-target
regions of `native/CMakeLists.txt`. Tasks 9–10 shipped the Guided **fifth reading style** in
`qml/MangaReader.qml`, but the reader reaches the native side through a `Loader` on
`qml/guided/GuidedCameraHost.qml`, which imports the `Colosseum.Guided` QML module. **That module
is not registered yet**, so today Guided degrades safely to a static whole-page view (reading is
never blocked). Two registration steps light it up. They are independent — **Step A alone makes
Guided fully live for a real eyes-on** (whole-page camera + Panel Step + Auto Read on the
conservative fallback path); Step B adds real panel detection and needs your Task 6 ONNX detector.

## Step A — register the camera controller (cheap, unblocks eyes-on, do this first)

`GuidedCameraController` + `GuidedTypes` are currently only in the *harness* targets, NOT the
`colosseum` app target — so Step A is three small changes:

1. Add the two sources to the `colosseum` target in `native/CMakeLists.txt` (they pull in only
   Qt Core/Gui/Quick, already linked):
   ```cmake
   guided/GuidedCameraController.cpp guided/GuidedCameraController.h
   guided/GuidedTypes.cpp guided/GuidedTypes.h
   ```
2. Include, near the other guided/player includes at the top of `native/main.cpp`:
   ```cpp
   #include "guided/GuidedCameraController.h"
   ```
3. Register the QML type, beside the existing `qmlRegisterType` cluster (currently ~lines 269–270,
   the `Colosseum.Player` registrations), BEFORE `engine.load(...)`:
   ```cpp
   qmlRegisterType<guided::GuidedCameraController>("Colosseum.Guided", 1, 0, "GuidedCameraController");
   ```

After Step A: selecting **Guided** in the reader loads the real controller. Every canvas plays the
whole-page overview path (one Overview step) — Panel Step / Auto Read / speed / Exit all work; there
is just no panel-to-panel motion yet (that is Step B). Nothing else needs to change for Step A;
`GuidedAnalysis` stays undefined and MangaReader already guards for that (`guidedService` is null →
whole-page fallback).

## Step B — register the analysis service `GuidedAnalysis` (needs your Task 6 detector)

This is what produces real detected-panel paths. It depends on **`guided::PanelDetectorOnnx`
(Task 6)**, which is not on master yet — it is yours. Once it lands:

1. Confirm the app (`colosseum`) target compiles the guided domain sources (currently only the
   harness targets list them): `guided/PanelAnalysisService.cpp`, `guided/PanelMapStore.cpp`,
   `guided/PanelPlanner.cpp`, `guided/PanelDetectorOnnx.cpp`, `guided/GuidedTypes.cpp`
   (+ `models/ModelManifest`). Add any missing ones to the `colosseum` target in
   `native/CMakeLists.txt`.
2. Construct the service against the SHARED `backgroundWork` coordinator (already at ~line 614 of
   `native/main.cpp`) and publish it as the `GuidedAnalysis` context property, near the other
   `setContextProperty` calls:
   ```cpp
   auto *guidedStore = new guided::PanelMapStore(
       QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/guided/panel-maps.sqlite");
   auto *guidedDetector = new guided::PanelDetectorOnnx(/* models::ModelManifest bundle */, &app);
   auto *guidedAnalysis = new guided::PanelAnalysisService(
       backgroundWork, guidedDetector, new guided::PanelPlanner, guidedStore, &app);
   engine.rootContext()->setContextProperty(QStringLiteral("GuidedAnalysis"), guidedAnalysis);
   ```
   Constructor is `PanelAnalysisService(work::BackgroundWorkCoordinator* work, IPanelDetector* detector,
   PanelPlanner* planner, PanelMapStore* store, QObject* parent)`. Use `backgroundWork` (do NOT `new` a
   second coordinator — one worker, shared with audiobook alignment). `Q_UNUSED(backgroundWork)` at
   line 615 can then be dropped.

After Step B: `guidedService` becomes non-null in MangaReader; `openEntry` / `setVisibleCanvas` /
`pathForCanvas` drive real detected-panel paths, and the reader-side + Downloads activity surfaces
(Task 11) show live analysis progress.

## What is already safe (no action needed to avoid breakage)

- MangaReader has **no** top-level `import Colosseum.Guided` — it will keep loading if either step is
  skipped or the module ever fails to register. Confirmed by `tests/test_guided_manga_reader.ps1`
  (uses a QML mock of the controller) and the untouched `tests/test_manga_tankoban_mode.ps1`.
- `qml/guided/GuidedCameraHost.qml` is the ONE file importing the native module; it is only reached
  through the `Loader`, and a failed load leaves Guided as a static whole-page view.

## Verify after applying

```
cmake --build native/build-msvc --target colosseum
powershell -NoProfile -File tests/test_guided_manga_reader.ps1   # still green
powershell -NoProfile -File tests/test_manga_tankoban_mode.ps1   # still green (no reader regression)
```
Then eyes-on: open a downloaded comic/manga, pick **Guided** in the reading-mode menu.
