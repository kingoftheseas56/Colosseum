#!/usr/bin/env python3
# gen_ground_truth.py — the PARITY ORACLE for the native EnglishForcedAligner (Task 11).
#
# A synthetic clip can never give research-grade "true" word timings, and the plan
# forbids hand-tuning expected timings to make a test pass. So instead of inventing
# ground truth, this script produces a REFERENCE forced-alignment computed on the
# EXACT same model + the exact same algorithm the C++ aligner runs: load the exported
# wav2vec2 ONNX via onnxruntime, decode tests/fixtures/alignment/audio/speech.wav to
# 16 kHz mono f32 with the same bundled ffmpeg the native decoder uses, do_normalize
# the waveform, run inference, log-softmax, and CTC force-align the KNOWN transcript
# ("the quick brown fox jumps over the lazy dog"). The result is written to
# tests/fixtures/alignment/ground_truth.json.
#
# Because C++ and this reference consume identical logits and run the identical
# blank-interleaved Viterbi, their per-word onsets should agree within ~1 frame — so
# the harness asserts PARITY (median onset error <= 250 ms, p95 <= 600 ms), not
# fabricated absolute timings.
#
# Run ONCE to (re)generate the committed oracle:  PYTHONUTF8=1 python scripts/alignment/gen_ground_truth.py
# [Agent 2 (Claude), biblio]
import json
import math
import os
import subprocess
import sys

import numpy as np
import onnxruntime as ort

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
MODEL_DIR = os.path.join(REPO, "resources", "models", "alignment", "forced")
ONNX_PATH = os.path.join(MODEL_DIR, "wav2vec2_base_960h.onnx")
VOCAB_PATH = os.path.join(MODEL_DIR, "vocab.json")
WAV_PATH = os.path.join(REPO, "tests", "fixtures", "alignment", "audio", "speech.wav")
OUT_PATH = os.path.join(REPO, "tests", "fixtures", "alignment", "ground_truth.json")

TEXT = "the quick brown fox jumps over the lazy dog"
WINDOW_START_MS = 0
WINDOW_END_MS = 4000


def resolve_ffmpeg() -> str:
    for cand in (
        os.environ.get("ALIGNMENT_FFMPEG"),
        "C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin/ffmpeg.exe",
        os.path.join(REPO, "native", "build-msvc", "tools", "ffmpeg.exe"),
    ):
        if cand and os.path.exists(cand):
            return cand
    return "ffmpeg"


def decode_window(wav: str, start_ms: int, end_ms: int) -> np.ndarray:
    """Decode [start_ms, end_ms) to 16 kHz mono f32 — the SAME ffmpeg invocation the
    native AudiobookAnalysisDecoder uses, so the samples are byte-identical."""
    args = [
        resolve_ffmpeg(), "-nostdin", "-v", "error",
        "-ss", f"{start_ms / 1000.0:.3f}", "-to", f"{end_ms / 1000.0:.3f}",
        "-i", wav, "-ac", "1", "-ar", "16000", "-f", "f32le", "pipe:1",
    ]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").astype(np.float64)


def normalize(x: np.ndarray) -> np.ndarray:
    """wav2vec2 do_normalize=true: zero-mean/unit-variance over the window,
    population variance, eps 1e-7 (matches Wav2Vec2FeatureExtractor)."""
    mean = x.mean()
    var = ((x - mean) ** 2).mean()  # population variance (ddof=0)
    return (x - mean) / math.sqrt(var + 1e-7)


def log_softmax(logits: np.ndarray) -> np.ndarray:
    m = logits.max(axis=1, keepdims=True)
    lse = m + np.log(np.exp(logits - m).sum(axis=1, keepdims=True))
    return logits - lse


def load_vocab(path: str):
    obj = json.load(open(path, encoding="utf-8"))
    labels = obj["labels"]
    tok = {t: i for i, t in enumerate(labels)}
    return labels, tok, obj


def tokenize_words(text: str, tok: dict, delim_id: int):
    """Split into words on non-[A-Za-z'] runs; each word -> uppercased letter token
    ids + its canonical [start,end) char span. Chars not in the vocab are dropped.
    Returns (words, target) where words[i] = {text,startChar,endChar,tokenIdx:[..]}
    and target is the flat token id sequence with a delimiter between words."""
    words = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isalpha() or c == "'":
            j = i
            while j < n and (text[j].isalpha() or text[j] == "'"):
                j += 1
            ids = []
            for k in range(i, j):
                up = text[k].upper()
                if up in tok:
                    ids.append(tok[up])
            if ids:
                words.append({"text": text[i:j], "startChar": i, "endChar": j, "ids": ids})
            i = j
        else:
            i += 1

    target = []       # flat token ids
    owner = []        # target index -> word index, or -1 for delimiters
    for wi, w in enumerate(words):
        if wi > 0:
            target.append(delim_id)
            owner.append(-1)
        w["tokenIdx"] = []
        for tid in w["ids"]:
            w["tokenIdx"].append(len(target))
            target.append(tid)
            owner.append(wi)
    return words, target, owner


def split_sentences(text: str):
    """Split on sentence-final punctuation, keeping canonical [start,end) spans. No
    such punctuation -> the whole passage is one sentence."""
    spans, start = [], 0
    for i, c in enumerate(text):
        if c in ".!?":
            end = i + 1
            if text[start:end].strip():
                spans.append((start, end))
            start = end
    if start < len(text) and text[start:].strip():
        spans.append((start, len(text)))
    if not spans:
        spans = [(0, len(text))]
    return spans


def forced_align(logp: np.ndarray, target, blank_id: int):
    """Standard CTC forced alignment: Viterbi over the blank-interleaved state graph
    [blank, t0, blank, t1, ..., t_{K-1}, blank]. Transitions: stay, advance, and
    skip-a-blank between two DIFFERENT labels. Returns path[t] = state index."""
    T = logp.shape[0]
    K = len(target)
    S = 2 * K + 1
    label = [blank_id] * S
    for k in range(K):
        label[2 * k + 1] = target[k]

    NEG = -1e30
    dp = np.full((T, S), NEG)
    bp = np.full((T, S), -1, dtype=np.int64)

    # t = 0: may start on the first blank (0) or the first token (1).
    dp[0, 0] = logp[0, label[0]]
    if S > 1:
        dp[0, 1] = logp[0, label[1]]

    for t in range(1, T):
        for s in range(S):
            # candidates in a FIXED priority so ties are deterministic across langs:
            # stay (s) > advance (s-1) > skip (s-2).
            best_prev, best_val = s, dp[t - 1, s]
            if s - 1 >= 0 and dp[t - 1, s - 1] > best_val:
                best_prev, best_val = s - 1, dp[t - 1, s - 1]
            if (s - 2 >= 0 and s % 2 == 1 and label[s] != label[s - 2]
                    and dp[t - 1, s - 2] > best_val):
                best_prev, best_val = s - 2, dp[t - 1, s - 2]
            dp[t, s] = best_val + logp[t, label[s]]
            bp[t, s] = best_prev

    # Backtrack from the better terminal state (last blank or last token).
    end_state = S - 1
    if S >= 2 and dp[T - 1, S - 2] > dp[T - 1, S - 1]:
        end_state = S - 2
    path = [0] * T
    s = end_state
    for t in range(T - 1, -1, -1):
        path[t] = s
        s = int(bp[t, s])
        if s < 0:
            s = path[t]
    return path, label


def token_spans(path, K):
    """For each target token k, [firstFrame, lastFrame] assigned to state 2k+1
    (lastFrame is inclusive). Tokens are never skipped, so every k gets >= 1 frame
    in a well-formed path; a missing token yields (-1, -1)."""
    spans = [[-1, -1] for _ in range(K)]
    for t, s in enumerate(path):
        if s % 2 == 1:
            k = (s - 1) // 2
            if spans[k][0] < 0:
                spans[k][0] = t
            spans[k][1] = t
    return spans


def frame_prob(logp: np.ndarray, path, label):
    """Per-frame probability of the label the Viterbi path assigned (exp of the
    log-softmax at the aligned state) — the honest 'how sure was the model on the
    forced path' signal."""
    return np.array([math.exp(logp[t, label[path[t]]]) for t in range(len(path))])


def main() -> int:
    if not os.path.exists(ONNX_PATH):
        print("MISSING model:", ONNX_PATH, file=sys.stderr)
        return 2
    labels, tok, vocab_obj = load_vocab(VOCAB_PATH)
    blank_id = vocab_obj.get("pad_id", 0)
    delim_id = tok.get(vocab_obj.get("word_delimiter", "|"), 4)

    x = decode_window(WAV_PATH, WINDOW_START_MS, WINDOW_END_MS)
    n_samples = x.shape[0]
    xn = normalize(x)

    # Single intra-op thread — matches the native aligner's SessionOptions so the
    # logits (and thus the forced path) are as close to bit-identical as ORT allows.
    so = ort.SessionOptions()
    so.intra_op_num_threads = 1
    sess = ort.InferenceSession(ONNX_PATH, sess_options=so, providers=["CPUExecutionProvider"])
    inp = xn.astype(np.float32).reshape(1, -1)
    logits = sess.run(["logits"], {"input_values": inp})[0][0].astype(np.float64)
    frames = logits.shape[0]
    window_ms = n_samples * 1000.0 / 16000.0
    stride_ms = window_ms / frames

    logp = log_softmax(logits)

    words, target, owner = tokenize_words(TEXT, tok, delim_id)
    path, label = forced_align(logp, target, blank_id)
    spans = token_spans(path, len(target))
    fprob = frame_prob(logp, path, label)

    def span_to_ms(f0, f1):
        return (WINDOW_START_MS + f0 * stride_ms, WINDOW_START_MS + (f1 + 1) * stride_ms)

    # Words: merge each word's letter tokens; confidence = mean aligned-path prob over
    # the frames assigned to this word's letter states.
    word_out = []
    for w in words:
        idxs = w["tokenIdx"]
        f0 = min(spans[i][0] for i in idxs)
        f1 = max(spans[i][1] for i in idxs)
        state_set = {2 * i + 1 for i in idxs}
        frames_in = [t for t, s in enumerate(path) if s in state_set]
        conf = float(np.mean([fprob[t] for t in frames_in])) if frames_in else 0.0
        s_ms, e_ms = span_to_ms(f0, f1)
        word_out.append({
            "word": w["text"], "startMs": round(s_ms, 2), "endMs": round(e_ms, 2),
            "startChar": w["startChar"], "endChar": w["endChar"],
            "confidence": round(conf, 4),
        })

    # Sentences: aggregate the words whose start char falls in the sentence span.
    sent_spans = split_sentences(TEXT)
    sent_out = []
    for si, (cs, ce) in enumerate(sent_spans):
        members = [wi for wi, w in enumerate(words) if cs <= w["startChar"] < ce]
        if not members:
            continue
        f0 = min(spans[i][0] for wi in members for i in words[wi]["tokenIdx"])
        f1 = max(spans[i][1] for wi in members for i in words[wi]["tokenIdx"])
        state_set = {2 * i + 1 for wi in members for i in words[wi]["tokenIdx"]}
        frames_in = [t for t, s in enumerate(path) if s in state_set]
        conf = float(np.mean([fprob[t] for t in frames_in])) if frames_in else 0.0
        s_ms, e_ms = span_to_ms(f0, f1)
        sent_out.append({
            "text": TEXT[cs:ce].strip(),
            "startMs": round(s_ms, 2), "endMs": round(e_ms, 2),
            "startChar": cs, "endChar": ce, "confidence": round(conf, 4),
        })

    out = {
        "text": TEXT,
        "sampleRate": 16000,
        "windowStartMs": WINDOW_START_MS,
        "windowEndMs": WINDOW_END_MS,
        "samples": int(n_samples),
        "frames": int(frames),
        "strideMs": round(stride_ms, 6),
        "words": word_out,
        "sentences": sent_out,
    }
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)

    print("=== ORACLE OK ===")
    print(f"samples={n_samples} frames={frames} stride_ms={stride_ms:.4f}")
    print(f"target tokens={len(target)} words={len(words)} sentences={len(sent_out)}")
    for w in word_out:
        print(f"  {w['word']:>7}  {w['startMs']:8.1f} -> {w['endMs']:8.1f} ms   conf={w['confidence']:.3f}")
    print("wrote", OUT_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
