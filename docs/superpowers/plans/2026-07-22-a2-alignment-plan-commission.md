# Commission: task-level implementation plan for Audiobook-EPUB Read-Along (A2 arc)

**To be handed as a PROMPT to the planning substrate (Codex high-reasoning or Fable),
not fired as an MCP call. Paste everything below the line into the planner.**

---

Write the full task-level implementation plan (superpowers writing-plans format: bite-sized
TDD steps, exact paths, complete code, checkbox steps, frequent commits) for the approved
design `docs/superpowers/specs/2026-07-21-audiobook-epub-read-along-design.md` in the
Colosseum repo (`C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`).

**The shared spine already EXISTS on master (built 2026-07-22 by Agent 0 — see
`docs/superpowers/plans/2026-07-22-background-work-spine-groundwork.md`). Your plan MUST
consume it, not rebuild it:**

1. **Scheduling:** the design's `AlignmentScheduler` is a THIN DOMAIN WRAPPER over the existing
   `work::BackgroundWorkCoordinator` (`native/work/BackgroundWorkCoordinator.h`). One chapter =
   one submitted work unit; use `WorkContext::checkpoint()` at safe stage boundaries and
   `shouldYield()` inside long loops. Do not create threads or a second scheduler.
   The app-owned instance `backgroundWork` in `native/main.cpp` is shared with guided comic
   analysis (one worker total, by design) — inject it into `AudioTextAlignmentService`.
   Priority convention: current chapter=100, next=90, previous=80, remainder=10.
2. **Status surfaces:** the "global background activity surface" from the design is DONE.
   Publish presentation-shaped state into `work::BackgroundActivityRegistry` (context property
   `BackgroundActivity`): keys title, stage, progress (0..1), paused, canPause; listen on
   `pauseRequested`/`resumeRequested`. Do NOT edit `qml/DownloadsPage.qml` — the row renders
   automatically. The Reader2 Audio-panel Text Sync row remains yours to build.
3. **Model bundling:** use `models::ModelManifest` (`native/models/ModelManifest.h`) for every
   bundled speech model (whisper base.en, wav2vec2 ONNX export). Its stable codes
   `model_missing`/`model_checksum_failed` match the design's failure table. Domain fields
   (language, engine compatibility, input requirements) go in the manifest JSON and are read
   from `.extra`.
4. **ONNX linking:** gate every ONNX-linking target behind the existing
   `COLOSSEUM_ENABLE_ONNX` option; the runtime stages via `scripts/native/fetch_onnxruntime.ps1`
   (pinned 1.25.0). whisper.cpp vendoring is yours to plan.
5. **Ownership fences:** you own the alignment domain dirs + Reader2 surfaces. You do NOT touch
   `native/main.cpp`, `native/CMakeLists.txt` shared regions, `qml/DownloadsPage.qml`, or
   `native/work/` / `native/models/` — service registration lines in `main.cpp` are written as
   an Agent 0 handoff step in your plan (mark them "Agent 0 applies").
6. Follow the repo's harness style: `require(cond, msg)` C++ harnesses printing a named sentinel;
   QML logic harnesses ride the EXIT CODE (`Qt.exit(0)` pass / `Qt.exit(1)` fail, accumulate
   failures and exit ONCE — `Qt.exit()` does not halt the function; qml.exe stdout is not
   reliably capturable here so never grep its console output); grep contracts for QML wiring shape.

Deliver the plan to `docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md`.
