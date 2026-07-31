# Cross-model review handoff — Colosseum video-stutter fix (commit 842aff2, was 9073f8c)

**Requested by:** Agent 0 (ZCode), player lane
**Reviewer needed:** Codex (different substrate than the ZCode/GLM author — producer ≠ reviewer, gov-v14 / Review Gate)
**Commit:** `842aff2` on branch `agent4/player2-chrome-port` (worktree `.worktrees/player2-chrome-port`). Supersedes the reviewed `9073f8c`.
**⚠ REVIEW TARGET IS NOW THE BRANCH TIP `2ed6811`, NOT `842aff2` alone.** A4 (Claude) picked the arc up on 2026-07-29, verified the state, and added one **comment-only** commit `2ed6811` on top — see *A4 pickup* below. `842aff2` is untouched beneath it. Review `git show 842aff2 && git show 2ed6811`, or equivalently `git diff 60d2a28..2ed6811`.
**Status:** Committed locally. **Not pushed** (Colosseum remote, not Tankoban-3; awaiting A4/Hemanth push direction).

## A4 pickup (2026-07-29) — what changed and why

- **Independently verified before trusting the handoff:** branch/worktree state, `842aff2` contents, `main.cpp` clean (last touched by A4's own `316b7ae`), A4's Player-2 WIP intact and unstaged (10 files, no overlap with the fix's 3), and **both progress harnesses re-run from the committed tree: 20/20 green each.** (The 127-exit Qt-DLL trap in note 4 below is real — it cost a first 0/20 run before Qt bin went on PATH.)
- **One defect found and fixed in `2ed6811` (comments only, no code):** prior finding **C** asks the reviewer to confirm *no comment still promises coalescing/debounce that the code does not implement*. It was only **half** fixed in `842aff2` — the `ProgressDiskWriter` class comment was corrected, but the top-of-file PERSISTENCE THREADING block still said *"coalescing bursts so only the latest snapshot is written."* A re-review would have bounced on it. Same block also still attributed the `+25/60s` post-cascade residual to the disk write, which the Option-A re-measure falsified (0–112 spread regardless of write policy). Both corrected, plus an explicit **NOT-claimed** line so the falsified story cannot be re-derived by the next reader.
- **Out-of-scope finding, NOT fixed here (recorded for A4's lane):** the Player-2 in-app path carries the *same* cascade the fix removes from the mpv path — `qml/player2/Player2Shell.qml:83` ticks `reportProgress` on a 5s cadence → `qml/player2host/ColosseumHostServices.qml:509` calls `Progress.record(...)`, which notifies. It is behind `COLOSSEUM_PLAYER2_IN_APP` and is not Hemanth's daily driver, so it is not a regression in this diff and is **not** part of this DoD. It becomes live if P2 is ever re-flipped. **Do not flag it as a defect in this review.**

**Review round-trip:** `9073f8c` was reviewed (Codex, REQUEST-CHANGES) — see **First review outcome** below. All findings were addressed in `842aff2`. **A re-review of `842aff2` is requested** (paste-ready block below); `codex` CLI / `scripts/engines/engine.py` are not on PATH in the ZCode harness, so the block is for Hemanth to drop into his Codex desktop GUI.

**Why review is gating:** non-trivial threaded change (`ProgressDiskWriter`: `moveToThread`, `BlockingQueuedConnection` flush barrier, `isRunning()` guards, lazily-created worker-thread `QSettings`, two `QSettings` instances on the same location). gov-v10 clause 3 + the gov-v12 merge-gate reconciliation make a reviewer pass part of *ready-to-merge* for engine-authored non-trivial work.

## First review outcome (9073f8c → REQUEST-CHANGES) + how 842aff2 addresses each

| # | Finding (9073f8c) | Verdict | Fix in 842aff2 |
|---|---|---|---|
| 1 | Lifecycle comments promised stop/**seek** notify; seek/pause never called recordProgress | PARTIAL | Comments corrected to the true coverage (notify on stop / stream-death / playback-failure / episode-advance / EOF; seek+pause intentionally do not — pre-existing, scrubbing fires often). NOT a regression: seek never notified before 9073f8c either. Behavior unchanged; the lying comment was the defect. |
| 2 | Worker `QSettings` constructed on GUI thread (member, not moved) → cross-thread QObject violation | PARTIAL | **Fixed.** `QSettings` is now created lazily inside the worker slot (`ensureSettings()`), so it is born on the worker thread. Args stored as plain members. |
| 3 | Queued writes precede the blocking shutdown barrier | MET | Unchanged (now via `flush()`). |
| 4 | isRunning() guard + finished→deleteLater reasonable; foreign-affinity QSettings blocked thread-safety | PARTIAL | Resolved by #2's affinity fix. finished→deleteLater retained. |
| 5 | `progress_store_harness` 20/20 FAIL (removals persist across reload) — async broke the sync reload contract | NOT-MET | **Fixed.** Added public `flush()` (post-current-map + BlockingQueuedConnection drain), called in destructor + aboutToQuit. Harness now calls `store.flush()` before reloading — explicit deterministic async contract. **Both harnesses re-run 20/20 green** (reviewer methodology matched). |
| 6 | Commit is exactly ProgressStore.h + PlayerPage.qml; main.cpp absent | MET | Still MET (now also includes tests/progress_store_harness.cpp — the test the fix+contract live in). |
| 7 | No qDebug/trace left | MET | Still MET. |
| + | Comment claimed a 0ms debounce/coalescing that does not exist | — | Comment corrected: no debounce; each scheduleSave() enqueues one full snapshot. |



---

## How to run the review

Paste the **REVIEW BLOCK** below into a Codex desktop session that has this repo loaded (worktree path: `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\player2-chrome-port`). Codex reads the DoD, reads the diff (it can `git show 9073f8c` itself, or use the inline patch), and returns APPROVE / REQUEST-CHANGES. Then post the outcome back here.

---

## REVIEW BLOCK (paste into Codex) — re-review of 842aff2

```
Cross-model RE-review for the Colosseum video-stutter fix (requested by Agent 0 (ZCode), carried by Agent 4 (Claude)). You are a DIFFERENT model than the author (Z.ai GLM 5.2). You previously reviewed 9073f8c with REQUEST-CHANGES; this is 842aff2 plus one comment-only follow-up 2ed6811. Your job is to confirm each prior finding is resolved AND re-check the diff against the Definition of Done.

REVIEW THE BRANCH TIP, NOT 842aff2 ALONE: `git diff 60d2a28..2ed6811` (equivalently `git show 842aff2` then `git show 2ed6811`). 842aff2 is the fix (three files: native/ProgressStore.h, qml/PlayerPage.qml, tests/progress_store_harness.cpp). 2ed6811 is Agent 4's comment-only correction to native/ProgressStore.h: it closes the HALF-FIXED part of prior finding C (the top-of-file block still claimed "coalescing bursts so only the latest snapshot is written", contradicting both the ProgressDiskWriter class comment and scheduleSave()), and it removes a claim the Option-A re-measure falsified (that the off-thread disk move reduced dropped frames). Verify 2ed6811 changed ONLY comment lines and that the header now matches the code.

PRIOR REQUEST-CHANGES FINDINGS — confirm RESOLVED in 842aff2:
A. Worker QSettings was constructed on the GUI thread (member not moved with moveToThread) → cross-thread QObject violation. Confirm it is now created lazily inside the worker slot (ensureSettings()) so affinity is the worker thread.
B. progress_store_harness 20/20 FAILED (removals persist across reload) because the async writer made reload nondeterministic. Confirm: a public flush() barrier exists, the harness calls store.flush() before reloading, and the contract is now an explicit deterministic async one. (Both harnesses were re-run 20/20 green by the author; you may re-run to verify.)
C. Comments promised "stop/seek" notify but seek/pause never called recordProgress. Confirm comments now state the TRUE coverage (notify on stop / stream-death / playback-failure / episode-advance / EOF; seek+pause intentionally do not — pre-existing, not a regression) and no comment still promises coalescing/debounce that the code does not implement.
D. Destructor thread-safety: confirm the destructor flushes THEN stops the thread (quit+wait), so no QThread is destroyed while running, and flush() guards on isRunning() so BlockingQueuedConnection never deadlocks a stopped thread.

DEFINITION OF DONE (verify the diff against EACH item):
1. The reactive-cascade root cause is fixed: the 5s tick calls recordSilent (persist without changed()), so Continue delegates are not re-rendered every 5s. Lifecycle writes (stop / stream-death / playback-failure / episode-advance / EOF) still notify via record(). seek/pause intentionally do not (documented, not a regression).
2. Disk persistence does not block the GUI thread: serialize + QSettings::setValue + sync() run on ProgressDiskWriter's own QThread, posted via Qt::QueuedConnection. GUI thread only mutates the map and posts snapshots.
3. Shutdown/destruction correctness: flush() posts the current map then blocks on a BlockingQueuedConnection drain, so the latest state lands on disk at aboutToQuit and in the destructor (which then stops the thread).
4. Thread-lifecycle safety: no deadlock (isRunning() guards), no QThread destroyed while running (destructor quit+wait), worker QObject moved to thread + finished->deleteLater.
5. Worker QSettings has correct affinity (born on the worker thread via lazy construction). GUI-thread QSettings (load/lastSeason/watchedMark, sync) and worker QSettings (continue/entries) are distinct instances on the same location, which Qt supports.
6. No Continue-feature regression: recent()/get()/forget()/lastSeason()/watchedMark behave as before; forget() and the >=90%-finished path still persist + notify. Both progress harnesses green.
7. main.cpp is NOT in the diff. The commit touches exactly the three files above. No qDebug/debug-trace in a measured run path.

DIFF UNDER REVIEW: `git diff 60d2a28..2ed6811` — 842aff2 (native/ProgressStore.h, qml/PlayerPage.qml, tests/progress_store_harness.cpp) + 2ed6811 (native/ProgressStore.h, comments only).

DO NOT FLAG (known, out of scope for this DoD): the Player-2 in-app path (Player2Shell.qml:83 -> ColosseumHostServices.qml:509) still calls the notifying Progress.record on a 5s cadence. It is behind COLOSSEUM_PLAYER2_IN_APP, is not the default backend, is untouched by this diff, and is tracked in Agent 4's lane.

YOUR REVIEW — do all four:
1. For each PRIOR finding A-D: state RESOLVED / NOT-RESOLVED with one line of evidence.
2. For each DoD item 1-7: state MET / NOT-MET / PARTIAL with one line of evidence.
3. Correctness + threading pass: real bugs, races, regressions. Re-scrutinize the lazy QSettings (any first-use race on the worker thread?), the flush() BlockingQueuedConnection against a running loop, and the dual-QSettings same-location visibility.
4. Flag anything the diff does that the DoD never asked for.

END with exactly one line: APPROVE or REQUEST-CHANGES, plus a one-sentence reason. Be terse; default to REQUEST-CHANGES if any item is NOT-MET or you are unsure.
```

---

## Author's self-notes for the reviewer (context, not part of the DoD)

- **Write policy is Option B (5s off-thread writes), not Option A (lifecycle-only).** An Option-A variant was built and measured but gave no smoothness gain: once the cascade is killed, residual output drops are variance-dominated (same-window runs spanned 0–112 regardless of write policy). Option B was kept for better crash-resume granularity at equal cost. This is a product call Hemanth made; it is not a defect.
- **The off-thread trace was confirmed by the prior author (Claude):** `scheduleSave tid=0x40c4` (GUI) vs `writeSnapshot tid=0xd2f4` (worker) — different threads.
- **Measurement (842aff2, post-correction):** +6 output drops/60s (variance band; standalone mpv is ~1/300s, the gold floor). The gate's strict zero-drop throws on any nonzero, but that is NOT the goal — the goal is low drops vs the +70–100 original baseline, which is met with margin.
- **Harness re-runs (842aff2):** both `progress_store_harness` and `progress_watched_override_harness` run **20/20 green** — the reviewer's exact methodology. (Run with Qt bin on PATH + `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`, else the exe exits 127 missing Qt DLLs — environment only, not a code issue.)
- **Known mines (do not flag as bugs):** (1) build mtime trap — must touch ProgressStore.h + main.cpp before rebuilding or ninja ships stale; (2) qDebug to the redirected gate pipe blocks the GUI thread → fake drop counts; (3) bare MpvItem windows don't paint.

## Outcome

- **First review (9073f8c):** REQUEST-CHANGES — 4 PARTIAL / 1 NOT-MET / 3 MET, plus the false-coalescing comment. Addressed in 842aff2 (see table above) — **except** the false-coalescing claim, which survived in the top-of-file block and was closed by A4 in `2ed6811`.
- **A4 verification (2026-07-29):** both progress harnesses re-run from the committed tree by Agent 4 (Claude), independent of the author — **20/20 green each**. Branch/WIP/main.cpp state confirmed as described.
- **Re-review (branch tip `2ed6811`):** PENDING — paste the block above into Codex desktop.


