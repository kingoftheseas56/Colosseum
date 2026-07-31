# Handoff — Agent 0 (Claude) → Agent 0 (ZCode): Colosseum video-stutter fix

**Date:** 2026-07-29
**From:** Agent 0 (Claude), player lane — executing A4's diagnostic
**Lane:** A4 (player). Worktree: `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\player2-chrome-port`, branch `agent4/player2-chrome-port`.
**Status:** Root cause CONCLUSIVELY found. Cascade fixed. Disk write off-thread. **One product decision open (Hemanth's call). Not yet committed/reviewed.**

---

## TL;DR — where this stands

Hemanth's reported video stutter (a clustered 3–4-frame hitch every 4–8s on Tenet, HEVC 10-bit, Intel UHD 620) is **caused by two application-side progress-writing operations, NOT by mpv, copy-back hwdec, the QtQuick video surface, or the render backend.** The original motivation for Player 2 / a direct-present rewrite was this stutter, and **that motivation is now dead** — neither is justified by this issue. mpv is capable of standalone-level smoothness here (proven: 0–3 drops/60s once the app stops disturbing it).

The **reactive `changed()` cascade is fixed** (silent-record). The **disk write is off the GUI thread** (background writer, trace-confirmed). A residual **+19–40 drops/60s** remains because writing to disk *at all* during playback contends with rendering on this iGPU. That residual is a **product tradeoff Hemanth must decide** (5s crash-resume vs lifecycle-only smoothness) — see §5.

## 1. The conclusive root cause (proven by a 5-step in-session isolation A/B)

The 5-second watch-position path in `PlayerPage.qml` → `Progress.record()` → `ProgressStore` was disturbing playback. Two independent culprits, separated by surgical env-gated switches (all now reverted):

1. **The reactive `changed()` cascade** (the BIG one — Hemanth's reported clustered hitch). Every 5s, `record()` → `bump()` → `emit changed()` → every Continue-row binding re-evaluates → `recent()` re-sorts/re-dedupes the whole map → all Continue delegates re-render. ~+45–97 of the drops.
2. **Synchronous disk persistence** — `save()` serializes the whole map to JSON (~6 ms CPU) + `QSettings::setValue` + `QSettings::sync()` (disk flush, ~5 ms) **on the GUI thread**. ~+22 drops.

The isolation table (all 60s, in-session, same exe lineage, Tenet, `display-resample`+interpolation, `hwdec=d3d11va-copy`):

| version | disk during playback | `changed` cascade | output drops/60s |
|---|---|---|---|
| standalone mpv (gold standard, own D3D11 window) | — | — | ~1–2 over **300s** |
| writer-off (5s timer OFF) | none | none | **0** |
| bump-only (timer on, disk skipped, notify off) | none | none | **3** |
| silent-record (disk on GUI, notify off) | yes, GUI thread | no | 25 |
| writer — coalesced (disk on worker thread) | yes, worker | no | 19 |
| writer — direct (disk on worker thread) | yes, worker | no | 40 |
| split (disk off, notify ON) | none | yes | 100 |
| original full app (disk on GUI, notify on) | yes, GUI | yes | 70–100 |
| spec's earlier run | yes, GUI | yes | +469 / 300s (~1.56/s) |

**Cleared by this evidence:** mpv, `d3d11va-copy` (copy-back), QtQuick/`MpvItem` presentation, the disk write mechanism itself, and general background work (poster fetching ran heavily during every zero/low-drop run).

## 2. What is fixed (in code, uncommitted)

**Silent-record (kills the cascade):**
- `native/ProgressStore.h`: `record()` refactored to `persist()` (private) + `record()` (persist + `bump()`/notify, for lifecycle) + `recordSilent()` (persist, NO notify, for the 5s tick).
- `qml/PlayerPage.qml`: `recordProgress(silent)` — the 5s `Timer` calls `recordProgress(true)` (silent); lifecycle callers (stop/seek/close, ~5 sites) call `recordProgress()` (notify). Comment updated.

**Background writer (disk off the GUI thread):**
- `native/ProgressStore.h`: new `ProgressDiskWriter` (Q_OBJECT) on its own `QThread`. `scheduleSave()` posts a snapshot via `Qt::QueuedConnection` (cheap, shared copy); the worker serializes + `setValue` + `sync()` on its thread. `flushSync()` is a no-op shutdown barrier invoked over `BlockingQueuedConnection` at `aboutToQuit` so the latest snapshot lands; destructor guards on `isRunning()` to avoid deadlock.
- **Trace-confirmed off-thread:** `[pw] scheduleSave (GUI) tid=0x40c4` vs `writeSnapshot tid=0xd2f4` — different threads.

**Current (direct-write, no debounce) variant measured +40; the earlier timer-coalesced variant measured +19.** Both uncommitted iterations exist in history; the file currently has the direct-write variant.

## 3. Code state — READ THIS BEFORE ANYTHING

- **Branch:** `agent4/player2-chrome-port` (A4's branch). **Do NOT push without A4/Hemanth's say-so.**
- **Committed (2 commits, on the branch):**
  - `6092c04` — `test(player): add bare-QtQuick MpvItem isolation probe + gate -QmlEntry` (the gate `-QmlEntry`/`-ResolveQmlEntry` work + `tests/mpv_qtquick_tenet_probe.qml` + parser tests). GREEN.
  - `60d2a28` — `fix(gate): repair mpv_zero_drop_gate summary -f formatting` (PowerShell `-f` precedence bug; standalone, surgical).
- **Uncommitted (the fix — NOT yet committed, by design, pending Hemanth's decision + Codex review):**
  - `native/ProgressStore.h` — `ProgressDiskWriter` + `persist`/`record`/`recordSilent` + `scheduleSave`/`setupWriter` + destructor.
  - `qml/PlayerPage.qml` — `recordProgress(silent)` + the 5s tick → `recordProgress(true)`.
  - `native/main.cpp` — should be **clean** (the 4 diagnostic context props were added then fully reverted back to just `DevAbbaClip`). **Verify with `git diff native/main.cpp` — expect empty.**
- **All four diagnostic env switches are REVERTED** (grep-clean: no `COLOSSEUM_NO_PROGRESS_*`, no `DevDisableProgress*`, no `bumpIfNotify`). Production is byte-identical to upstream when the fix is absent.
- **Artifacts** (the measurement data + per-step traces): `artifacts/mpv-zero-drop/<stamp>/run-1.stderr.log` in the worktree (gitignored — use `cmd dir`, not Glob). Newest stamps hold the writer runs.

## 4. The one open decision (Hemanth's call — the reason this isn't committed)

Disk writes during playback cost **+19–40 drops even off the GUI thread** (serialize ~6 ms CPU + `sync()` disk flush contend with the render thread on the iGPU; plus real run-to-run variance from background poster fetching). The cascade (Hemanth's reported clustered hitch) is gone; this residual is lower-grade and spread out. Three options:

- **(A) Lifecycle-only disk writes** (RECOMMENDED): the 5s tick updates memory only (no disk); disk writes happen on pause/stop/seek/close/shutdown via the off-thread writer. → ~+3 drops (smooth-tier, matches the no-disk floor). Crash-resume = last pause. **This is how VLC/mpv/PotPlayer behave.** Implement by giving `persist()` a `persistToDisk` flag: `recordSilent()`→`persist(entry, false)`, `record()`→`persist(entry, true)`. Tiny change.
- **(B) 5s disk writes, off-thread (current):** ~+19–40 drops, crash-resume within 5s.
- **(C) Middle (~15–30s interval / longer debounce):** ~+3–10 drops, crash-resume within 15–30s.

Hemanth had NOT yet given eyes on the writer builds nor picked A/B/C when this was handed off. **Get his eyes first** (is +19–40 even perceptible now that it's spread, not clustered?), then his tradeoff call.

## 5. Mines & lessons (read before building/running)

- **Build mtime trap:** after editing `ProgressStore.h`, `native/_reconf2.bat` sometimes reports `ninja: no work to do` (stale mtime/deps) and ships the OLD binary. **Force it:** touch the header + `main.cpp` first —
  `powershell -NoProfile -Command "Get-Item '<worktree>\native\ProgressStore.h','<worktree>\native\main.cpp' | ForEach-Object { $_.LastWriteTime = Get-Date }"` — then `cmd /c native\_reconf2.bat`. Require `BUILD_OK` AND that the compile steps actually ran (`[2/4] ... mocs_compilation`, `[3/4] main.cpp.obj`).
- **`qDebug` to a redirected pipe BLOCKS the GUI thread.** The gate redirects colosseum's stdout/stderr to pipes it only drains after exit; once the 4 KB buffer fills, every `qDebug` stalls the GUI → fake huge drop counts (we saw +112 that was pure trace artifact). **Never leave debug `qDebug` in a measured run.** If you must trace, write to a file, not stderr.
- **The gate's strict zero-drop check is NOT the goal.** `Test-ProbeResult` throws on any nonzero `frame-drop-count`. The real goal is "smooth to Hemanth's eyes + low drops vs the +70–100 baseline." A run that "fails" the gate at +3 is a win. Read the delta from the throw line or the artifact JSON.
- **Bare `MpvItem` window does NOT paint video** (known; the ABBA team documented it — `tests/player2/player2_efficiency_abba.ps1:62-66`). Don't try the bare-window isolation again; it decodes audio but never paints, and it ran the full `main.cpp` service stack anyway (not isolated). The full-app `Main.qml` path (auto-plays `COLOSSEUM_ABBA_CLIP`) is the only valid harness.
- **RHI is NOT the issue.** `main.cpp` boots OpenGL when `COLOSSEUM_MPV` is set / `COLOSSEUM_PLAYER2` isn't (the gate sets `COLOSSEUM_MPV=1`). Same as the full app. The bare-window no-paint is a hosting quirk, not RHI.
- **Run-to-run variance is real** (background poster fetching loads the machine differently). Don't conclude from a single 60s run; the trend across the table is what's solid.

## 6. Build & run commands (this stripped MSYS bash has no `git`/`head`/`powershell` on PATH — use absolute paths)

- **Git:** `"/c/Program Files/Git/cmd/git.exe" -C "<worktree>" ...`
- **PowerShell:** `"/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe" ...`
- **tasklist (no `/FI` mangling):** `MSYS_NO_PATHCONV=1 "/c/Windows/System32/tasklist.exe" /FI "IMAGENAME eq colosseum.exe" /FO CSV /NH`
- **Build:** `cd <worktree> && MSYS_NO_PATHCONV=1 "/c/Windows/System32/cmd.exe" /c "native\_reconf2.bat"` (the `_reconf2.bat` filename has a leading underscore; builds with `-DCOLOSSEUM_PLAYER2_IN_APP=ON`, target `colosseum`). Touch first (see §5).
- **Run (clean production):**
  ```
  cd <worktree> && "/c/.../powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "tests/mpv_zero_drop_gate.ps1" -Exe "native/build-msvc/colosseum.exe" -Clip "C:/Users/Suprabha/Downloads/Colosseum/Tenet - 20260726_184029.mp4" -WarmupSeconds 30 -MeasureSeconds 60 -Runs 1
  ```
  (Default `-QmlEntry` is `qml/Main.qml` = full app, which auto-plays the clip. Keep the window VISIBLE — Hemanth's eyes are the gate.)
- **Constraint carried from the original task:** don't kill pre-existing Colosseum processes; don't touch/stage dirty Player 2 files / `qml/Main.qml` / artifacts / helper `.bat`s. Editing `PlayerPage.qml` + `ProgressStore.h` is in scope (A4's lane, not Main.qml/Player-2).

## 7. Recommended next steps (in order)

1. **Verify code state:** `git -C <worktree> status --short` + `git diff native/main.cpp` (expect main.cpp clean). Confirm the uncommitted fix is exactly `ProgressStore.h` + `PlayerPage.qml`.
2. **Get Hemanth's eyes** on the current writer build (is +19–40 perceptible?).
3. **Get his tradeoff decision** (A lifecycle-only / B 5s / C middle). Recommend A.
4. **Implement the chosen variant** (if A: add `persistToDisk` flag — ~5 lines). Keep the off-thread writer regardless (lifecycle writes stay non-blocking).
5. **Measure** (clean, no trace; touch-then-build). Target: A→~+3, B→+19–40, C→~+3–10. Hemanth's eyes confirm smooth.
6. **Commit** the fix by explicit pathspec (`native/ProgressStore.h qml/PlayerPage.qml`; verify `--cached` is exactly those; main.cpp stays clean). Sign `[Agent 0 (ZCode), player lane]`.
7. **Codex cross-review** (Brotherhood reflex — non-trivial threaded change). The `ProgressDiskWriter` threading (moveToThread, BlockingQueuedConnection shutdown, isRunning() destructor guard, two-QSettings-instances-for-different-keys) deserves a second substrate's eyes.
8. **Record a recap** to `~/.claude/recaps/agent-0/` (workspace-stamped for the haven) + update MEMORY.md's A4 ⭐NEXT so the next wake knows the stutter is fixed and Player 2 is deprioritized for this issue.

## 8. The bigger conclusion to carry forward

**Do not invest further in Player 2 or a direct-present mpv rewrite for this stutter.** The engine, copy-back hwec, and QtQuick surface are all exonerated. The entire deficit was two app-side progress operations. The fix is small and surgical (silent record + off-thread/lifecycle disk). This also means the earlier "native D3D11 is the genuine solution" framing — triggered by ChatGPT's taunt — was wrong twice over: Harbor uses mpv (not native D3D11), and native D3D11 was never the lever here.

— **[Agent 0 (Claude), player lane — executing A4 diagnostic]**
