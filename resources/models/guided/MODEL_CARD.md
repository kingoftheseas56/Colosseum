# Guided panel detector — model card

## Summary

- **File:** `manga_panel_detector_fp32.onnx` (9.4 MB)
- **Architecture:** YOLO26-nano (2.375M params, 5.2 GFLOPs)
- **Task:** detect comic/manga **panels** and **text** regions in a page image
- **Classes:** `0 = panel`, `1 = text`
- **Input:** 1×3×640×640 NCHW RGB, values normalized to `0..1` (letterbox to preserve aspect)
- **Output:** `output0` shape `[1, 300, 6]` — end-to-end NMS, each row `[x1, y1, x2, y2, confidence, classId]` in 640×640 tensor space
- **Default confidence threshold:** 0.25
- **License:** Apache-2.0 — see `MODEL_LICENSE.txt`

## Provenance

- **Upstream:** https://huggingface.co/leoxs22/manga-panel-detector-yolo26n
- **Revision:** `535bbe1fc1e922d2108f918cd1bce29ba3516196`
- **Upstream weights:** `manga_panel_detector_fp32.pt`, SHA-256 `73e0fb587ea3afe0d17aa9f0c3b1f5a8001b3ecbc3c77091e0730654b0da9146`
- **Export:** `scripts/guided/export_panel_model.py` (ultralytics 8.4.102, onnx 1.21.0, opset 18) — PyTorch→ONNX format conversion only; no retraining or weight modification.
- **Exported ONNX SHA-256:** `1acfa7a7225ffb9f0bc71a5e55662e1c780c6bf44374a6f58ae628b80c09c94f` (also recorded in `manifest.json`, validated at runtime by `models::ModelManifest`).

## Runtime

- Consumed by `native/guided/PanelDetectorOnnx` via ONNX Runtime 1.25.0 CPU (intra-op threads = 1, `ORT_ENABLE_ALL`). Fully offline; nothing here contacts the network at app runtime.
- The model produces detections only — **no reading order.** Panel ordering is the planner's job (`native/guided/PanelPlanner`).

## Open provenance note (for Task 12/13 installer notices)

The upstream model is declared Apache-2.0. Its **training-data** provenance (the plan references Manga109-s) is the upstream author's; if Manga109-s or another dataset with its own usage terms was used, that attribution belongs in `THIRD_PARTY_NOTICES.md` when the installer bundles this model. This has NOT been independently cleared here — flag for review before public release.
