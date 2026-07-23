# Read-Along — Registration + Bundling Handoff (Agent 0 / packaging applies)

**From:** Agent 2 (Claude), biblio — 2026-07-23 (woven-cadence wake continued)
**Status:** Tasks 3–12 of `docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md` are BUILT, TDD-verified, and pushed to Colosseum master. The read-along feature ships **DORMANT** — a single `readAlongAvailable` gate in `qml/reader2/ReaderShell.qml` keeps the live reader byte-for-byte today's reader until the native context props are registered. This doc is the remaining work to turn it ON, which is **fenced (main.cpp / CMakeLists shared regions / packaging) = Agent 0's to apply.**

## What's shipped (all on master, all green harnesses)
| Task | Commit | Component |
|---|---|---|
| 3 | e71fcf9 | `AudioTextAlignmentService` — scheduler over the shared coordinator (has the `ChapterProcessor` seam) |
| 5 | 6b8b707 | `ReadAlongController` — bidirectional, exactly-one-seek |
| 4 | 91ae190 | paper read-along paint (Overlayer wash, node-tested) |
| 6 | e3339ea | Reader2 live integration (dormant-gated) |
| 7 | ade2bd3 | Text Sync status |
| 10 | a778cd1 | `EpubSequenceMatcher` |
| 8 | 44c99f8 | `AudiobookAnalysisDecoder` (bundled ffmpeg) |
| 9 | 1f56794 | `CoarseTranscriber` (CPU whisper.cpp — NOT ffmpeg-whisper; see note) |
| 11 | 2be911e | `EnglishForcedAligner` (wav2vec2 CTC via ONNX Runtime) |
| 12 | (pending A2 commit) | `AlignmentPipeline` — composes all 5 stages into the real `ChapterProcessor`; end-to-end harness proves audio→Ready chapter |

## STEP 1 — main.cpp registration (the fenced step)
Construct once, at app startup (after the coordinator/registry/pairing/downloader exist — they already do, see `main.cpp` ~line 620–691):

```cpp
// dbPath: a stable per-user file, e.g. <AppDataLocation>/alignment/readalong.db (NEVER ":memory:").
auto *readStore = new alignment::AlignmentStore(dbPath);              // main-thread reads
auto *pipeline  = new alignment::AlignmentPipeline(                    // holds decoder+transcriber+matcher+aligner
    /*whisperModel=*/ appDir + "/models/alignment/coarse/ggml-base.en.bin",
    /*wav2vecModel=*/ appDir + "/models/alignment/forced/wav2vec2_base_960h.onnx");
auto *alignSvc  = new alignment::AudioTextAlignmentService(
    backgroundWork, backgroundActivity, readStore, dbPath, audioPairing,
    /*localFiles=*/ [audiobooks](const QString& pairKey){ return audiobooks->localFiles(pairKey); },
    pipeline->makeProcessor());
auto *readAlong = new alignment::ReadAlongController(readStore);
engine.rootContext()->setContextProperty("AudioTextAlignment", alignSvc);
engine.rootContext()->setContextProperty("ReadAlong", readAlong);
```
- **Lifetime:** the `BackgroundWorkCoordinator` must be torn down (worker joined) BEFORE `alignSvc` — same ordering the shipped `guided::PanelAnalysisService` relies on. Parent them so the coordinator destructs first, or drain it explicitly. (A running WorkFn holds `this`.)
- Registering these two props flips `readAlongAvailable` true → Reader2's read-along wiring (Task 6/7) goes live. Nothing else in QML changes.
- Confirm exact ctor signatures against the committed headers (`native/alignment/AlignmentPipeline.h`, `AudioTextAlignmentService.h`, `ReadAlongController.h`).

## STEP 2 — flip the shipped build to ONNX-ON (coordinate with Agent 1 / guided)
The aligner links ONNX Runtime, compiled only under `-DCOLOSSEUM_ENABLE_ONNX=ON`. **Agent 1's guided panel detector needs the SAME flip** (their kindled-lens recap: "Task 12 bundle ORT→flip ONNX ON"). So this is ONE shared milestone: the shipped `colosseum.exe` must build ONNX-ON and bundle `onnxruntime.dll` (ORT 1.25.0, `C:/tools/onnxruntime-win-x64-1.25.0`, fetched by `scripts/native/fetch_onnxruntime.ps1`). The `AlignmentPipeline` + service registration in main.cpp must therefore also sit behind `#ifdef COLOSSEUM_ENABLE_ONNX` (or the app won't link with ONNX off) — with the props simply not registered when off (read-along stays dormant, which is already safe).

## STEP 3 — pressure wiring (Task 12 Step 4)
Connect video playback / high-resolution page decode / startup → `backgroundWork->setPressure(work::Pressure::LatencySensitive)` while media plays, `Suspended` on heavy load (blocks analysis, NOT lookup/playback/UI), `Normal` otherwise. The transcriber's ~25 s CPU whisper pass per chapter makes this matter — it must yield to playback.

## STEP 4 — three deferred fixes (small, flagged by the component tasks)
1. **ReaderChrome bookId** (Task 7): `qml/reader2/ReaderShell.qml` must thread `bookId: shell.bookId` into its `ReaderChrome` instantiation, so the Text Sync card resolves its book. Until then the card stays dormant even with the service registered.
2. **AlignmentStore `busy_timeout`** (Task 3): `retry()`/`restart()` write from the main thread while a worker may be mid-write (WAL, separate connections). Add `PRAGMA busy_timeout=<ms>` on store open (`native/alignment/AlignmentStore.cpp`) so a concurrent write waits instead of `SQLITE_BUSY`. Not triggered in harnesses (drained first) but real once the live pipeline runs.
3. **Real chapter durations** (Task 6/12): the service's `ensurePair` currently stamps NOMINAL 10-min chapter bounds; Reader2's `sessionToAbsMs` uses the same nominal convention. Wire `AudiobookAnalysisDecoder::probeChapters(localFiles)` into the service so chapter bounds (and the reader's absolute-time mapping) use real durations. Single-file m4b is already exact; multi-file needs this.

## STEP 5 — Task 13 bundling (packaging)
- **LFS:** `.gitattributes` already claims `resources/models/alignment/**/*.onnx`, `*.onnx.data`, `*.bin` for LFS. Run `git lfs install` + push the three model binaries (NOT yet committed): whisper `ggml-base.en.bin` (141 MB, SHA in `coarse/manifest.json`), `wav2vec2_base_960h.onnx` (1.7 MB) + `wav2vec2_base_960h.onnx.data` (377 MB, SHA in `forced/manifest.json`). They live locally now; the harnesses run against them.
- **Installer** (`scripts/installer/package_release.sh`): stage into the app tree — `models/alignment/{coarse,forced}/*` (models + manifests + vocab), `tools/whisper/` (whisper-cli.exe + ggml*.dll + whisper.dll via `scripts/native/fetch_whispercpp.ps1`, pinned v1.9.1 `whisper-bin-x64.zip` SHA `7d8be46e…3539`), `onnxruntime.dll`, and the existing `tools/ffmpeg.exe`. Validate model SHA-256 (via `models::ModelManifest`) before NSIS runs; fail closed on missing licenses.
- **THIRD_PARTY_NOTICES.md:** whisper.cpp (MIT), ONNX Runtime (MIT), `facebook/wav2vec2-base-960h` model (Apache-2.0), miniz (already present from Task 2).

## Model provenance (reproducible)
- **whisper:** `ggerganov/whisper.cpp` `ggml-base.en.bin`, fetched by `scripts/native/fetch_whispercpp.ps1` (which also fetches the CPU runtime binary). CPU-only (the bundled ffmpeg's own whisper filter is Adreno-GPU-only and heap-corrupts on desktop GPUs — that's why Task 9 uses a standalone CPU whisper.cpp).
- **wav2vec2:** exported by `scripts/alignment/export_wav2vec2_onnx.py` (deps pinned in `scripts/alignment/requirements-export.txt`; onnx-vs-torch max diff 4e-5). Ground-truth oracle regen: `scripts/alignment/gen_ground_truth.py`.

## Verification
- Non-ONNX harnesses: `%TEMP%/a2_build_align.bat <target>` (store/index/service/controller/matcher/decoder/transcriber), run from `native/build-msvc/`.
- ONNX harnesses: `%TEMP%/a2_build_onnx.bat <target>` (english_forced_aligner_harness, alignment_pipeline_harness), run from `native/build-onnx/`.
- **The pipeline harness is the end-to-end proof:** real audio → decode → transcribe → match → align → word cues → Ready chapter (confidence >0.99), and a mismatched edition → CouldntSync.

## Remaining after this handoff
- Task 12 crash-recovery + pressure-yield harnesses (interrupt-every-stage/reopen/resume) — could be A0 or a follow-up A2 wake.
- **Task 14 — acceptance gate + eyes-on smoke: Hemanth's.** The whole feature is provable headless up to here; the live read-along reading experience (wash tracks glyphs, scrub-to-seek feel, enlargement) needs his eyes once STEP 1–2 make it live.
- A cross-substrate (Codex) review of the batch — the plan mandates it at the Task 3/7/11/12 checkpoints — is advisable before this goes live.
