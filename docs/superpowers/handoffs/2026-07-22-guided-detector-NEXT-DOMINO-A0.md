# NEXT DOMINO — Agent 0: finish the Guided panel DETECTOR (Task 6 → Step B)

**From:** Agent 1 (comics) · **To:** Agent 0 (foundation — owns build, native deps, `main.cpp`)
**Date:** 2026-07-22 · **Plan:** `docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md` **Task 6** (full spec)
**Chains into:** `docs/superpowers/handoffs/2026-07-22-guided-reader-main-cpp-registration-A0.md` **Step B**

## The payoff this unlocks
Guided is live in the app today as a whole-page shell (Step A shipped, `80036c8`). The ONE thing
between that and the real experience Hemanth wants to watch — the camera **hopping panel to panel** —
is this detector. When it lands and Step B wires it, opening a downloaded comic in Guided and hitting
**Auto Read** plays a genuine guided read. This is the next domino.

## What is already DONE (build against it, don't redo)
- **Model + manifest, verified sound.** `resources/models/guided/manga_panel_detector_fp32.onnx`
  (9.8 MB, real ONNX, LFS) + `manifest.json`. I re-checked: the file's SHA-256
  (`1acfa7a7…c09c94f`) matches `manifest.sha256` exactly. Manifest declares input `640×640 NCHW 0..1`,
  output `NMS [1,300,6]` = `x1,y1,x2,y2,confidence,classId`, classes `0=panel 1=text`, threshold `0.25`.
  Export half committed `a0761ba` (also `scripts/guided/export_panel_model.py`, `MODEL_CARD`, `MODEL_LICENSE`, `.gitattributes` LFS).
- **The detector SEAM (mine, shipped).** `native/guided/PanelDetector.h`:
  ```cpp
  struct DetectorResult { QVector<Detection> detections;  // normalized to SOURCE coords; NO ordering
                          FallbackCode error = FallbackCode::None; QSize inputTensorSize; QSize sourceSize; };
  class IPanelDetector { virtual DetectorResult detect(const QImage& source, work::WorkContext& context) = 0; };
  ```
  `detect()` runs on a background WORKER thread → the impl must be self-contained + thread-safe, and
  cooperate with pause/cancel via `context.checkpoint()` / `context.shouldYield()`.
- **ONNX Runtime plumbing.** `native/cmake/OnnxRuntime.cmake` + `scripts/native/fetch_onnxruntime.ps1`
  (ORT 1.25.0 CPU x64), gated by `-DCOLOSSEUM_ENABLE_ONNX=ON`.
- **Generic manifest loader.** `native/models/ModelManifest.{h,cpp}` — use THIS (groundwork delta), not a
  guided-local one. It already emits `model_missing` / `model_checksum_failed`.
- **The downstream consumers are ready and green:** `PanelPlanner` (ordering/framing), `PanelMapStore`
  (persist), `PanelAnalysisService` (orchestration), `GuidedCameraController` (registered, live). They
  all consume `DetectorResult` through the seam. The whole guided QML surface (Tasks 9–11) is built.

## What is LEFT — in order
1. **`native/guided/PanelDetectorOnnx.h/.cpp`** implementing `IPanelDetector` (plan Task 6 Step 4):
   - Load `manifest.json` via `models::ModelManifest::load()`; call `validateChecksum()` BEFORE creating
     the `Ort::Session`. Read detector fields (input shape, `[1,300,6]` output contract, classes,
     threshold `0.25`) from `manifest.extra`. Reject a missing/mismatched model with `model_missing` /
     `model_checksum_failed` before inference. Require the frozen `[1,300,6]` shape; reject any other.
   - Preprocess: `letterbox` the source to `640×640` RGB, `NCHW`, `0..1`.
   - Infer: `Ort::Session` CPU, intra-op threads = 1, `ORT_ENABLE_ALL`.
   - Postprocess: decode the NMS `[1,300,6]` output, REVERSE the letterbox back to **normalized source
     coordinates** (this is the contract — `DetectorResult.detections` are source-normalized, no ordering),
     drop below threshold, tag class 0=panel / 1=text. Deterministic CPU output. NO planner logic here.
   - `inference_failed` on any Ort throw.
2. **`tests/panel_detector_harness.cpp`** + fixtures `tests/fixtures/guided/detector/regular_grid.png`
   + `expected.json` (plan Task 6 Steps 3 & 5): fixture inference finds 4 panels, letterbox reversal
   returns normalized source boxes, CPU output is deterministic, and a copied model with **one flipped
   byte returns `model_checksum_failed` before session creation**.
3. **CMake:** add `guided/PanelDetectorOnnx.*` to the app target and a `panel_detector_harness` target,
   both under the `COLOSSEUM_ENABLE_ONNX` gate; link `onnxruntime::onnxruntime`. Also add the guided
   DOMAIN sources the app target still lacks: `guided/PanelAnalysisService.cpp`, `guided/PanelMapStore.cpp`,
   `guided/PanelPlanner.cpp`, `native/models/ModelManifest.cpp` (only `GuidedCameraController` + `GuidedTypes`
   are in the app target so far, from Step A).
4. **Step B wiring** (the linked handoff): construct `PanelAnalysisService(backgroundWork, guidedDetector,
   new PanelPlanner, guidedStore, &app)` and `setContextProperty("GuidedAnalysis", …)`. Inject the SHARED
   `backgroundWork` (main.cpp ~line 614), not a new coordinator. Drop the `Q_UNUSED(backgroundWork)`.

## ⚠ Build constraint (coordination)
`-DCOLOSSEUM_ENABLE_ONNX=ON` on the SHARED `native/build-msvc` makes sibling agents' builds FATAL
(`OnnxRuntime.cmake` FATAL_ERRORs if ORT isn't installed). Run `scripts/native/fetch_onnxruntime.ps1`
first, and do the ONNX-on work in an **isolated build dir** (or keep the default app build OFF and flip
ON only for your build) until the detector is proven — so you never redden A1/A2/A4 mid-arc. The default
production build should stay ONNX-OFF (Guided whole-page shell) until you're ready to ship the detector.

## Acceptance (definition of done for this domino)
- `panel_detector_harness` green: 4 panels on the fixture, normalized-source boxes, deterministic,
  flipped-byte → `model_checksum_failed`.
- App builds with `COLOSSEUM_ENABLE_ONNX=ON`, links `onnxruntime.dll`, no QML warnings.
- `tests/test_guided_manga_reader.ps1` + `tests/test_manga_tankoban_mode.ps1` still green (no reader regression).
- **Eyes-on:** open a downloaded comic → Guided → the camera frames real detected panels and Auto Read
  steps through them (Hemanth's checkpoint — plan's Task 9/13 camera-grammar-on-fixtures gate).

Everything on the reader side is done and waiting on exactly this. Tip it.
