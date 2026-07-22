#!/usr/bin/env python3
"""One-time PyTorch -> ONNX export for the guided panel detector (Task 6).

Downloads the pinned upstream weights from Hugging Face, verifies their SHA-256,
exports a fixed 640x640 fp32 ONNX with embedded NMS, and writes the manifest
that native/models/ModelManifest validates at runtime. Run once on a dev machine:

    py -3.12 -m venv <venv> && <venv>/Scripts/pip install -r scripts/guided/requirements-export.txt
    <venv>/Scripts/python scripts/guided/export_panel_model.py

Outputs land in resources/models/guided/ and are committed (the ONNX via Git LFS).
Nothing here ships in the installed app.
"""
from __future__ import annotations

import json
import pathlib
import sys
from hashlib import sha256

REPO = "leoxs22/manga-panel-detector-yolo26n"
REV = "535bbe1fc1e922d2108f918cd1bce29ba3516196"
PT_FILE = "manga_panel_detector_fp32.pt"
PT_SHA = "73e0fb587ea3afe0d17aa9f0c3b1f5a8001b3ecbc3c77091e0730654b0da9146"

# repo root = two levels up from this file (scripts/guided/export_panel_model.py)
ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "resources" / "models" / "guided"


def main() -> int:
    from huggingface_hub import hf_hub_download
    from ultralytics import YOLO

    OUT.mkdir(parents=True, exist_ok=True)

    print(f"[export] downloading {REPO}@{REV[:7]}:{PT_FILE}")
    pt = pathlib.Path(hf_hub_download(REPO, PT_FILE, revision=REV))
    got = sha256(pt.read_bytes()).hexdigest()
    if got != PT_SHA:
        print(f"[export] FATAL: PT SHA mismatch\n  expected {PT_SHA}\n  got      {got}",
              file=sys.stderr)
        return 1
    print("[export] PT SHA verified")

    print("[export] exporting PyTorch -> ONNX (640x640, fp32, embedded NMS, opset 18)")
    exported = pathlib.Path(
        YOLO(str(pt)).export(format="onnx", imgsz=640, simplify=True,
                             dynamic=False, nms=True, opset=18, device="cpu"))
    target = OUT / "manga_panel_detector_fp32.onnx"
    target.write_bytes(exported.read_bytes())
    onnx_sha = sha256(target.read_bytes()).hexdigest()
    print(f"[export] wrote {target.name} ({target.stat().st_size} bytes), sha256={onnx_sha}")

    manifest = {
        "schema": 1,
        "modelId": "panel-yolo26n",
        "modelVersion": REV[:7],
        "file": target.name,
        "sha256": onnx_sha,
        "license": "Apache-2.0",
        "sourceRepo": REPO,
        "sourceRevision": REV,
        "input": {"width": 640, "height": 640, "layout": "NCHW", "range": "0..1"},
        "output": {"layout": "NMS", "shape": [1, 300, 6],
                   "fields": ["x1", "y1", "x2", "y2", "confidence", "classId"]},
        "classes": {"0": "panel", "1": "text"},
        "confidenceThreshold": 0.25,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("[export] wrote manifest.json")
    print("PANEL_MODEL_EXPORT_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
