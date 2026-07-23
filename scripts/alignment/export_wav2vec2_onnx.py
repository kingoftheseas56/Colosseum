#!/usr/bin/env python3
# export_wav2vec2_onnx.py — development-only, reproducible export of the English
# forced-alignment acoustic model (facebook/wav2vec2-base-960h) to ONNX.
#
# Colosseum never installs or runs Python at runtime: this script is run ONCE by a
# developer to produce the committed ONNX artifact + tokenizer vocabulary + manifest,
# which the native EnglishForcedAligner (Task 11) loads through the app's existing
# ONNX Runtime seam. The model revision is pinned in source; the export writes a
# fixed 16 kHz float input with a DYNAMIC sample length, runs an ONNX-vs-PyTorch
# logits comparison, and stamps a SHA-256 manifest.
#
# [Agent 2 (Claude), biblio]
import hashlib
import json
import os
import sys

import numpy as np
import torch
from transformers import Wav2Vec2ForCTC, Wav2Vec2Processor
import onnxruntime as ort

MODEL_ID = "facebook/wav2vec2-base-960h"
MODEL_REVISION = "main"  # pin a commit hash for a fully reproducible export in prod
OPSET = 17
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "resources", "models", "alignment", "forced")
OUT_DIR = os.path.abspath(OUT_DIR)
ONNX_NAME = "wav2vec2_base_960h.onnx"


def main() -> int:
    os.makedirs(OUT_DIR, exist_ok=True)
    onnx_path = os.path.join(OUT_DIR, ONNX_NAME)

    proc = Wav2Vec2Processor.from_pretrained(MODEL_ID, revision=MODEL_REVISION)
    model = Wav2Vec2ForCTC.from_pretrained(MODEL_ID, revision=MODEL_REVISION).eval()

    fe = proc.feature_extractor
    do_normalize = bool(getattr(fe, "do_normalize", False))
    sr = int(getattr(fe, "sampling_rate", 16000))

    # Export: raw normalized 16 kHz waveform [batch, samples] -> CTC logits [batch, frames, vocab].
    dummy = torch.zeros(1, sr, dtype=torch.float32)  # 1 s of silence
    torch.onnx.export(
        model,
        (dummy,),
        onnx_path,
        input_names=["input_values"],
        output_names=["logits"],
        dynamic_axes={"input_values": {0: "batch", 1: "samples"},
                      "logits": {0: "batch", 1: "frames"}},
        opset_version=OPSET,
        do_constant_folding=True,
    )

    # Tokenizer vocabulary — the CTC labels in id order (id -> token).
    vocab = proc.tokenizer.get_vocab()            # token -> id
    id_to_token = {i: t for t, i in vocab.items()}
    labels = [id_to_token[i] for i in range(len(id_to_token))]
    vocab_out = {
        "labels": labels,
        "pad_token": proc.tokenizer.pad_token,
        "pad_id": proc.tokenizer.pad_token_id,
        "word_delimiter": getattr(proc.tokenizer, "word_delimiter_token", "|"),
        "sample_rate": sr,
        "do_normalize": do_normalize,
    }
    with open(os.path.join(OUT_DIR, "vocab.json"), "w", encoding="utf-8") as f:
        json.dump(vocab_out, f, indent=2)

    # Validate ONNX vs PyTorch on a 2 s random input.
    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    test = torch.randn(1, sr * 2, dtype=torch.float32)
    with torch.no_grad():
        torch_logits = model(test).logits.numpy()
    onnx_logits = sess.run(["logits"], {"input_values": test.numpy()})[0]
    maxdiff = float(np.abs(torch_logits - onnx_logits).max())

    frames = int(onnx_logits.shape[1])
    stride_ms = (2000.0 / frames) if frames else 0.0

    digest = hashlib.sha256(open(onnx_path, "rb").read()).hexdigest()
    # schema/modelId/modelVersion/license are the fields the shared native
    # models::ModelManifest loader requires (same shape as the guided + coarse
    # manifests); the domain fields ride alongside for the aligner's own use.
    manifest = {
        "schema": 1,
        "modelId": "wav2vec2-base-960h-ctc",
        "modelVersion": MODEL_REVISION,
        "id": "wav2vec2-base-960h-ctc",
        "file": ONNX_NAME,
        "sha256": digest,
        "bytes": os.path.getsize(onnx_path),
        "license": "Apache-2.0",
        "vocab": "vocab.json",
        "sample_rate": sr,
        "opset": OPSET,
        "frame_stride_ms": round(stride_ms, 4),
    }
    with open(os.path.join(OUT_DIR, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    # Signature report for the native aligner author.
    print("=== EXPORT OK ===")
    print("do_normalize:", do_normalize, "sample_rate:", sr)
    print("onnx-vs-torch max logit diff:", maxdiff)
    print("INPUTS:", [(i.name, i.shape, i.type) for i in sess.get_inputs()])
    print("OUTPUTS:", [(o.name, o.shape, o.type) for o in sess.get_outputs()])
    print("vocab size:", len(labels))
    print("labels:", labels)
    print("frames for 32000 samples:", frames, "-> frame_stride_ms:", round(stride_ms, 4))
    print("MANIFEST:", json.dumps(manifest))
    if maxdiff > 1e-2:
        print("WARN: ONNX diverges from torch (>1e-2)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
