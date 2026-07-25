# Comic Reader Final Stretch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every verified parity gap between the new Comic Reader and its two lineage readers (Colosseum Reader 1, Tankoban 2), run the Task-14 acceptance gate, and ship `agent1/comicreader` to master.

**Architecture:** The reader is already cut over (`qml/MangaReader.qml` → `ComicReaderShell`). This plan is closure work in three kinds: QML surface fixes (`qml/comicreader/`), C++ pipeline fixes (`native/comicreader/`), and process gates (rulings → acceptance → review → merge). Every fix is grounded in a specific reference behaviour found by the 2026-07-25 five-audit parity sweep; nothing here is invented.

**Tech Stack:** Qt 6.11 / QML + C++ (MSVC, ninja), PowerShell test gates, offscreen qml.exe harnesses.

---

## Ground rules for the executor (read before Task 1)

- **Worktree:** everything happens in `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\comicreader-isolation` on branch `agent1/comicreader`. Never touch the Colosseum main tree (it is Agent 0's WIP).
- **Machine sharing:** DO NOT launch `colosseum.exe` without Hemanth's word — Agent 4 may be running ABBA benchmarks. Offscreen harnesses and builds are fine.
- **Build commands** (from `native/`, absolute path required — `cmd //c` won't resolve a relative .bat):
  - C++ harness: `cmd //c "<worktree>\native\build-target.bat comicreader_core_harness"` → `TARGET_BUILD_OK`
  - App: `cmd //c "<worktree>\native\build-target.bat colosseum"` → `TARGET_BUILD_OK`. If `LNK1104: cannot open colosseum.exe`, a stale exe is running: `powershell "Get-Process colosseum | Stop-Process -Force"` and retry.
- **Run commands:**
  - C++ harness: from `native/build-msvc`, `PATH="/c/Qt/6.11.1/msvc2022_64/bin:$PATH" ./comicreader_core_harness.exe` → `COMICREADER_CORE_OK`
  - QML gates: `powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comicreader_<gate>.ps1` → `COMICREADER_<GATE>_OK`. Gates: contract, state, shell, surfaces, chrome, overlays, migration; plus `tests/test_guided_manga_reader.ps1` (`GUIDED_FROZEN_OK`) and `tests/test_manga_tankoban_mode.ps1`.
- **Commits:** message prefix `[Agent 1 (Claude), comics]`, commit **by explicit pathspec** (never bare `git commit`), push after every commit (`git push origin agent1/comicreader`). End messages with `Co-Authored-By: Claude <noreply@anthropic.com>`.
- **QML gotchas already paid for:** `font.pixelSize` is an int; harness fakes must be `QtObject`s not JS literals (literals copy); a JS object passed through createObject becomes a QVariantMap; the settings sheet body sizes off the scrim, not anchors.
- **When a harness assertion fails after your change:** instrument before editing either side. Twice this build the code was right and the assertion wrong. Print the actual sequence, then decide.
- **References for ground-truthing:** Reader 1 = `git show ee652dd^:qml/MangaReader.qml` (run inside the worktree). TB2 = `C:\Users\Suprabha\Desktop\Tankoban 2\src\ui\readers\`.

---

## Phase A — The ruling round (MAIN SESSION ONLY, with Hemanth)

### Task A1: Present the ruling menu, record verdicts

**Files:** Modify: this file (fill the Verdict column).

This task is a conversation, not code. Present the table; record each verdict inline; conditional Phase F tasks execute only per verdict.

| # | Ruling | Options (references) | Recommendation | Verdict |
|---|---|---|---|---|
| R1 | Wheel travel per notch | 100px (TB2, ours) vs 168px (Reader 1 — the reader he last used) | 168 | **168** — EXECUTE F1 |
| R2 | Strip page width scope | global (ours, deliberate) vs per-series with global seed (both references) | per-series | **per-series** — EXECUTE F2 |
| R3 | Coupling phase memory | per-entry only (ours) vs per-series seed (TB2) | per-series seed | **per-series seed** — EXECUTE F3 |
| R4 | Cover/lone single width | full width (ours + Reader 1) vs half-width flush to spine (TB2) | keep full width | **keep full width** — no task |
| R5 | Forward turn on last page | silent no-op (ours) vs cross to next entry (Reader 1) vs end overlay (TB2) | toast "End of volume" until the end card lands | **toast** — EXECUTE F5 |
| R6 | Keypress keeps chrome alive | yes (TB2) vs no, keyboard reading is immersive (Reader 1, ours) | keep Reader 1 behaviour | **keep Reader 1** (no poke) — no task |
| R7 | Click-zone flash on page turn | TB2-only flourish | skip | **skip** — no task |
| R8 | No-upscale ceiling | TB2-only; interacts badly with our decode cap (decoded width < true native, so the ceiling would cap at decode size) | skip | **skip** — no task |
| R9 | Zoom persisted per series | both references persist; ours keeps it per session after B1 | defer | **defer** — no task |
| R10 | BackAction law vs HUD back pill | extend BackAction with a raised-label variant / carve out the reader / drop the shadow | extend BackAction (Task F10) | **extend BackAction** — EXECUTE F10 |

- [x] **Step 1:** Presented; Hemanth's verdict 2026-07-25: "take your recommendations" — every recommended option adopted as-is, no deviations.
- [x] **Step 2:** Verdicts written into the table above. EXECUTE: F1, F2, F3, F5, F10. NO TASK: R4, R6, R7, R8. DEFERRED: R9 (zoom per-series persistence — revisit only if he asks for it after living with zoom-survives-the-turn from B1).
- [ ] ~~Step 2 commit~~ (done): `git commit -m "[Agent 1 (Claude), comics] plan: final-stretch rulings recorded" -- docs/superpowers/plans/2026-07-25-comicreader-final-stretch.md`

---

## Phase B — Correctness (Tier 1)

### Task B1: Zoom survives the page turn

**Files:** Modify: `qml/comicreader/ComicReaderDoubleSurface.qml:151`; Test: `tests/comicreader_surfaces_harness.qml`

Both references keep zoom across turns and reset only pan (TB2 `ComicReader.cpp:1374-1380`; Reader 1 `:251`).

- [ ] **Step 1: Failing test.** In the surfaces harness's double-surface section, after the existing unit tests, add:

```qml
// zoom survives a page turn — you must be able to read a whole volume magnified
dblSurface.setZoom(160)
ck(dblSurface.clampedZoom === 160, "double: setZoom(160) takes")
dblSurface.panBy(50, 50)
dblSurface.currentPage = dblSurface.currentPage + 2   // a page turn
ck(dblSurface.clampedZoom === 160, "double: zoom must SURVIVE a page turn, got " + dblSurface.clampedZoom)
ck(dblSurface.panX === 0 && dblSurface.panY === 0, "double: pan resets to origin on a turn (zoom does not)")
dblSurface.setZoom(100)   // restore for later assertions
```

(Adapt `dblSurface` to the harness's actual id for the mounted double surface; keep the restore line so later assertions are unaffected.)

- [ ] **Step 2:** Run surfaces gate → expect FAIL on "zoom must SURVIVE".
- [ ] **Step 3: Fix.** In `_onUnitShown()`:

```qml
// A new unit resets the PAN to origin — never the zoom. Both lineage readers keep
// zoom across turns (TB2 even persists it per series); resetting it made a magnified
// volume unreadable: zoom in, turn, back to 100%, every single turn.
panX = 0; panY = 0
```

(delete `zoomPercent = 100;` from that line, nothing else in the function changes).

- [ ] **Step 4:** Run surfaces gate → `COMICREADER_SURFACES_OK`.
- [ ] **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderDoubleSurface.qml tests/comicreader_surfaces_harness.qml`

### Task B2: Strip restore — one door, per-entry, settle-gated

**Files:** Modify: `qml/comicreader/ComicReaderShell.qml` (load, `_applyResume`, stripRestore), `qml/comicreader/ComicReaderStripSurface.qml` (delete the latch); Test: `tests/comicreader_shell_harness.qml`, `tests/comicreader_surfaces_harness.qml`; C++ test: `tests/comicreader_core_harness.cpp`

Fixes three audit findings at once: resume never scrolls the strip when `scrollFrac==0` but page>1; the surface's `_resumeApplied` latch fires once per **object lifetime** (second book lands at the top); the fraction is applied against an unsettled estimated column. Reference: Reader 1 `:293-298` + `:760-773` (single `stripRestore` door, 300ms settle, fraction-if-present-else-page).

- [ ] **Step 1: Failing shell test.** In the shell harness resume section (after the existing strip-resume assertions):

```qml
// resume routes through the ONE restore door: a pending fraction is armed for the
// settle-gated stripRestore, and a page-only record (scrollFrac 0) still restores.
ck(Math.abs(rShell._pendingStripFrac - 0.5) < 1e-9,
   "resume: the saved scrollFrac must arm _pendingStripFrac, got " + rShell._pendingStripFrac)

// page-only record (written from a paged mode): fraction 0, page 3 — the door must still open
var r2Prog = fakeProgR2
r2Prog.saved = { "resume": { "chapterId": "ch1", "page": 3, "scrollFrac": 0, "maxSeen": 4, "finished": false } }
var r2Store = fakeStoreR2; r2Store.pages = fivePages()
var r2 = makeShell({
    "width": 640, "height": 480, "seriesId": "s3b", "seriesTitle": "Resume2",
    "seriesCover": "file:///f/c.png", "core": fakeCoreR2, "progress": r2Prog,
    "pageStore": r2Store, "persistedMode": "long_strip",
    "entryKind": "manga", "western": false,
    "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
    "chapterId": "ch1", "chapterLabel": "Chapter 1"
})
ck(r2.currentPage === 3, "resume2: page restored to 3")
ck(r2._pendingStripFrac === 0, "resume2: no fraction to arm")
ck(r2._stripRestorePending === true, "resume2: the restore door must still be ARMED for a page-only record")
```

Add `FakeCore { id: fakeCoreR2 } FakeProgress { id: fakeProgR2 } FakePageStore { id: fakeStoreR2 }` to the fake pool.

- [ ] **Step 2:** Run shell gate → FAIL (`_pendingStripFrac`/`_stripRestorePending` don't exist).
- [ ] **Step 3: Shell fix.**

In the property block near `_pendingAtLast`:

```qml
property real _pendingStripFrac: 0        // one-shot: the saved scrollFrac awaiting layout settle
readonly property bool _stripRestorePending: stripRestore.running || _stripRestoreTries > 0
```

In `_applyResume()`, alongside the `stripFraction` line:

```qml
_pendingStripFrac = (mode === "long_strip") ? (Number(r.scrollFrac) || 0) : 0
```

In `load()`, at the end of the non-empty-pages branch (after `maxSeen = Math.max(...)`):

```qml
// Physically move the strip to the restored spot. One door (stripRestore), settle-
// gated: the column positions its delegates a vsync later, and its heights are
// estimates until decodes land — an immediate jump reads y=0 and lands at the top.
if (mode === "long_strip" && (_pendingStripFrac > 0 || currentPage > 1))
    stripRestore.restart()
```

Replace `stripRestore.onTriggered` with:

```qml
onTriggered: {
    if (reader.mode !== "long_strip" || reader.max <= 0) { reader._stripRestoreTries = 0; return }
    var span = stripSurface.contentHeight - stripSurface.height
    if (span <= 0) {   // not laid out yet — retry, a slow decode costs a moment, never your place
        if (reader._stripRestoreTries < 3) { reader._stripRestoreTries += 1; stripRestore.restart() }
        else reader._stripRestoreTries = 0
        return
    }
    reader._stripRestoreTries = 0
    if (reader._pendingStripFrac > 0) {
        stripSurface.haltScrollAt(Math.max(0, Math.min(span, reader._pendingStripFrac * span)))
        reader._pendingStripFrac = 0
    } else if (reader.currentPage > 1) {
        stripSurface.seekToPage(reader.currentPage - 1)   // backend-exact stripPageTop
    }
}
```

- [ ] **Step 4: Surface latch removal.** In `ComicReaderStripSurface.qml` delete: `property real resumeFraction`, `property bool _resumeApplied`, `function _applyResumeFraction()`, `onResumeFractionChanged`, the `onContentHeightChanged: root._applyResumeFraction()` line, and `Component.onCompleted: Qt.callLater(_applyResumeFraction)`. In the shell, delete the `resumeFraction: reader.stripFraction` binding on `stripSurface`. Update the surfaces-harness test that exercised `_applyResumeFraction` to instead assert `seekToPage`/`haltScrollAt` behaviour (drive `haltScrollAt(500)` → `contentY === 500`, `_pendingWheelPx === 0`).
- [ ] **Step 5: C++ test for the seek's geometry.** In `comicreader_core_harness.cpp` after T15:

```cpp
// ── Test 16: stripPageTop is the strip model's own top for that page ─────────
{
    ComicReaderCore core;
    core.openEntry(QStringLiteral("t16"), plainPages, QStringLiteral("ltr"), manualNormal());
    core.setStripViewportWidth(1000);
    QAbstractListModel* m = core.stripModel();
    const double top3 = m->data(m->index(3, 0), ComicReaderStripModel::TopRole).toDouble();
    CHECK(qAbs(core.stripPageTop(3) - top3) < 0.5, "T16 stripPageTop(3) matches the model's TopRole");
    CHECK(core.stripPageTop(-1) == 0.0 && core.stripPageTop(999) == 0.0,
          "T16 out-of-range stripPageTop is 0, never a crash");
}
```

- [ ] **Step 6:** Build + run core harness, run shell + surfaces + migration gates → all OK.
- [ ] **Step 7:** Commit: `git commit -- qml/comicreader/ComicReaderShell.qml qml/comicreader/ComicReaderStripSurface.qml tests/comicreader_shell_harness.qml tests/comicreader_surfaces_harness.qml tests/comicreader_core_harness.cpp`

### Task B3: Strip tracking — any non-programmatic move counts, throttled to 80ms

**Files:** Modify: `qml/comicreader/ComicReaderStripSurface.qml:206-210, 382-397`; Test: `tests/comicreader_surfaces_harness.qml`

Two audits hit this independently: the `_userInteracted` gate means keyboard/scrub reading never updates the counter or the Continue record (read a chapter with Space → Continue says page 1); and `_emitUserScroll` runs every frame (three `indexAt` probes + an array, on the render-critical thread). References: Reader 1 `:983` + `:743-751` (80ms `pageTrack` off any `contentY` change); TB2 `:2396-2410`.

- [ ] **Step 1: Failing test.** In the surfaces harness wheel section:

```qml
// tracking is PROVENANCE-BLIND: a keyboard/scrub move (bare contentY write) must
// update pageInView/scrolled — gated only on _programmatic, throttled to 80ms.
var sawPage = 0
stripSurface.pageInView.connect(function (p) { sawPage = p })
stripSurface.contentY = stripSurface.contentY + 400   // simulates _stripScroll / scrub
// the emit is throttled — pump past the 80ms window
wait(120)                                             // use the harness's existing wait/Timer helper
ck(sawPage > 0, "tracking: a non-wheel contentY move must still reach pageInView, got " + sawPage)
```

(If the harness has no `wait` helper, follow its existing deferred-phase pattern: stash the assertion and run it in a 150ms Timer alongside the others.)

- [ ] **Step 2:** Run surfaces gate → FAIL.
- [ ] **Step 3: Fix.** Replace the emit line and add the throttle:

```qml
onContentYChanged: {
    root._scheduleReport()
    if (!root._draining) root._smoothY = list.contentY   // resync on any external move
    // Tracking is provenance-blind: keyboard, scrub, wheel — a move is a move. Only
    // _programmatic writes (resume, compensation-free halt, layout anchoring) stay
    // silent, so mounting/restoring never clobbers the shell's page.
    if (!root._programmatic) root._scheduleEmit()
}
```

And next to the report timer:

```qml
// 80ms tracking throttle (Reader 1's pageTrack): the emit does three indexAt probes
// plus builds the visible array — per-frame during a glide it is pure overhead on the
// one thread that must hold 60fps.
Timer { id: emitTimer; interval: 80; repeat: false; onTriggered: root._emitUserScroll() }
function _scheduleEmit() { if (!emitTimer.running) emitTimer.start() }
```

`_userInteracted` stays, but now ONLY gates `manualNavigation()` provenance (wheel intake) — delete it from the emit path. Update the surfaces-harness "signals gated" test: construction and `_programmatic` moves still emit nothing; a bare assignment now emits (that is the fix).

- [ ] **Step 4:** Run surfaces + shell + migration gates → OK.
- [ ] **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderStripSurface.qml tests/comicreader_surfaces_harness.qml`

### Task B4: Toast — the reader's one transient-feedback surface

**Files:** Modify: `qml/comicreader/ComicReaderHud.qml`, `qml/comicreader/ComicReaderShell.qml` (zoom + nudge call sites); Test: `tests/comicreader_chrome_harness.qml`

Both references toast zoom steps and pairing nudges (TB2 `:1818-1837, 2131, 1893`; Reader 1 `:806, 809-812, 390`). Ours has no feedback anywhere — at the zoom clamps a keystroke does nothing visible at all.

- [ ] **Step 1: Failing test.** In the chrome harness:

```qml
var toast = byName(hud, "hudToast")
ck(toast !== null, "toast: the HUD must mount a toast surface")
hud.showToast("Zoom 160%")
ck(toast.opacity === 1, "toast: showToast must present it")
ck(byName(hud, "hudToastText").text === "Zoom 160%", "toast: the message is shown verbatim")
```

- [ ] **Step 2:** Run chrome gate → FAIL.
- [ ] **Step 3: Implement.** In `ComicReaderHud.qml`, at root level (independent of `chromeVisible` — feedback must show even with chrome hidden):

```qml
// ---- toast: the one transient-feedback surface (zoom, pairing, bookmarks) ----
// 900ms, Reader 1's number. Independent of chromeVisible: feedback must land even
// when the chrome is away — that is exactly when you need it.
function showToast(msg) { hudToastText.text = msg; hudToast.opacity = 1; hudToastTimer.restart() }
Rectangle {
    id: hudToast
    objectName: "hudToast"
    anchors.horizontalCenter: parent.horizontalCenter
    y: Math.round(parent.height * 0.14)
    width: hudToastText.implicitWidth + 28
    height: 34
    radius: 17
    color: Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.92)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.18)
    opacity: 0
    visible: opacity > 0.001
    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    Text {
        id: hudToastText
        objectName: "hudToastText"
        anchors.centerIn: parent
        color: theme.ink
        font.family: theme.hud
        font.pixelSize: 13
        font.bold: true
    }
    Timer { id: hudToastTimer; interval: 900; onTriggered: hudToast.opacity = 0 }
}
```

In the shell's input wiring, extend two handlers:

```qml
onZoomBy: function (delta) {
    if (doubleSurface) {
        doubleSurface.setZoom(doubleSurface.clampedZoom + delta)
        hud.showToast("Zoom " + doubleSurface.clampedZoom + "%")
    }
}
onNudgeCoupling: {
    reader.nudgeCoupling()
    var phase = (core && core.couplingState) ? String(core.couplingState).split(":")[1] : ""
    hud.showToast(phase === "shifted" ? "Shifted pairing" : "Normal pairing")
}
```

- [ ] **Step 4:** Run chrome + shell gates → OK.
- [ ] **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderHud.qml qml/comicreader/ComicReaderShell.qml tests/comicreader_chrome_harness.qml`

### Task B5: Spread override reachable — right-click cycles the page under the cursor

**Files:** Modify: `qml/comicreader/ComicReaderShell.qml` (new `cycleSpreadOverride` + `onOpenContextMenu`); Test: `tests/comicreader_shell_harness.qml`

`ComicReaderCore::setSpreadOverride` has zero production callers — when detection gets one page wrong, the only escape is `P`, which re-phases the whole book. The approved mock's own caption is the authority: *"spread override stays a direct right-click on the page itself."* Cycle rule from Reader 1 `:382-390`: auto → spread → single → auto, with a toast. `pageInfo()` reports override state as: key absent = auto, `true` = spread, `false` = single (verified `ComicReaderTypes.cpp:41-44`).

- [ ] **Step 1: Failing test.** In the shell harness (FakeCore gains a spy + stateful pageInfo):

```qml
// in FakeCore:
property var lastSpreadOverride: null
property var fakePageInfo: ({})           // what pageInfo returns for any page
function pageInfo(page) { return fakePageInfo }
function setSpreadOverride(page, state) { lastSpreadOverride = { page: page, state: state } }
```

Test body (use a dedicated fake set):

```qml
// right-click spread override cycles auto -> spread -> single -> clear
var oCore = fakeCoreO
oCore.fakePageInfo = {}                                   // absent key = auto
oShell.cycleSpreadOverride(2)
ck(oCore.lastSpreadOverride.state === "spread", "override: auto cycles to spread")
oCore.fakePageInfo = { "spreadOverride": true }           // spread
oShell.cycleSpreadOverride(2)
ck(oCore.lastSpreadOverride.state === "single", "override: spread cycles to single")
oCore.fakePageInfo = { "spreadOverride": false }          // single
oShell.cycleSpreadOverride(2)
ck(oCore.lastSpreadOverride.state === "clear", "override: single cycles back to auto (clear)")
```

- [ ] **Step 2:** Run shell gate → FAIL.
- [ ] **Step 3: Implement.** In the shell, next to `nudgeCoupling()`:

```qml
// Fix ONE page's pairing without re-phasing the book (that is what P does). Cycle:
// auto -> spread -> single -> auto. pageInfo reports the override as absent/true/false.
function cycleSpreadOverride(page0) {
    if (!core || !core.pageInfo || !core.setSpreadOverride) return
    var info = core.pageInfo(page0)
    var cur = info.spreadOverride === true ? "spread"
            : info.spreadOverride === false ? "single" : "auto"
    var nxt = cur === "auto" ? "spread" : (cur === "spread" ? "single" : "clear")
    core.setSpreadOverride(page0, nxt)
    entrySave.restart()                                   // the override is per-book memory
    hud.showToast("Page " + (page0 + 1) + " pairing: " + (nxt === "clear" ? "auto" : nxt))
}
```

Replace the context-menu wiring (`onOpenContextMenu: reader.settingsRequested()`):

```qml
// The approved mock's caption is the authority here: "spread override stays a direct
// right-click on the page itself." In double-page, right-click cycles the page under
// the cursor half (physical mapping: rightIndex sits RIGHT in RTL, LEFT in LTR).
// Everywhere else, right-click still opens Settings.
onOpenContextMenu: function (x, y) {
    if (reader.mode === "double_page" && reader.max > 0 && reader.core) {
        var u = reader.core.unitForPage(reader.currentPage - 1)
        if (u && u.rightIndex !== undefined && u.rightIndex >= 0) {
            var leftHalf = x < reader.width / 2
            var pg = (u.leftIndex >= 0)
                ? (leftHalf ? (reader.rtl ? u.leftIndex : u.rightIndex)
                            : (reader.rtl ? u.rightIndex : u.leftIndex))
                : u.rightIndex
            reader.cycleSpreadOverride(pg)
            return
        }
    }
    reader.settingsRequested()
}
```

- [ ] **Step 4:** Run shell + migration + overlays gates → OK.
- [ ] **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderShell.qml tests/comicreader_shell_harness.qml`

### Task B6: Bookmarks live end-to-end

**Files:** Modify: `native/comicreader/ComicReaderCore.h/.cpp`, `qml/comicreader/ComicReaderShell.qml`; Test: `tests/comicreader_core_harness.cpp`

Found in the chrome audit's margins: nothing writes `m_bookmarks`, `B` reaches an unconsumed signal, and the HUD ticks draw a list bound to a load-time snapshot. The backend field, the persistence, and the ticks all exist — only the writes are missing.

- [ ] **Step 1: Failing C++ test** (after T16):

```cpp
// ── Test 17: toggleBookmark writes, sorts, de-dupes, persists ────────────────
{
    ComicReaderCore core;
    core.openEntry(QStringLiteral("bm"), plainPages, QStringLiteral("ltr"), manualNormal());
    core.toggleBookmark(4);
    core.toggleBookmark(1);
    QVariantList bm = core.bookmarks();
    CHECK(bm.size() == 2 && bm[0].toInt() == 1 && bm[1].toInt() == 4,
          "T17 bookmarks are stored SORTED");
    core.toggleBookmark(4);
    CHECK(core.bookmarks().size() == 1, "T17 a second toggle removes");
    core.toggleBookmark(999);
    CHECK(core.bookmarks().size() == 1, "T17 out-of-range toggles are ignored");
    CHECK(core.persistedState().value(QStringLiteral("bookmarks")).toList().size() == 1,
          "T17 the persisted blob carries the live bookmarks");
}
```

- [ ] **Step 2:** Build → compile FAIL (`toggleBookmark`/`bookmarks` missing).
- [ ] **Step 3: Implement.** Header, next to `nudgeCoupling`:

```cpp
Q_INVOKABLE void toggleBookmark(int page);        // 0-based; insert-sorted / remove
Q_INVOKABLE QVariantList bookmarks() const;       // live view for the HUD ticks
```

signal: `void bookmarksChanged();`. Implementation:

```cpp
void ComicReaderCore::toggleBookmark(int page) {
    if (page < 0 || page >= m_pages.size())
        return;
    const int i = m_bookmarks.indexOf(page);
    if (i >= 0)
        m_bookmarks.removeAt(i);
    else {
        m_bookmarks.append(page);
        std::sort(m_bookmarks.begin(), m_bookmarks.end());
    }
    emit bookmarksChanged();
}

QVariantList ComicReaderCore::bookmarks() const {
    QVariantList out;
    for (int b : m_bookmarks)
        out.append(b);
    return out;
}
```

Shell: root handler + live list + persistence trigger:

```qml
onBookmarkToggleRequested: {
    if (!core || !core.toggleBookmark || max <= 0) return
    core.toggleBookmark(currentPage - 1)
    var on = core.bookmarks().indexOf(currentPage - 1) >= 0
    hud.showToast(on ? "Bookmarked p." + currentPage : "Bookmark removed")
}

property var liveBookmarks: []
function _refreshBookmarks() { liveBookmarks = (core && core.bookmarks) ? core.bookmarks() : [] }
```

Extend the existing `Connections { target: reader.core ... }` block:

```qml
function onBookmarksChanged() { reader._refreshBookmarks(); if (reader._ready) entrySave.restart() }
function onEntryChanged() { reader._refreshBookmarks() }
```

HUD instantiation: replace the `bookmarkPages: (reader.persistedState && ...)` binding with `bookmarkPages: reader.liveBookmarks`.

- [ ] **Step 4:** Build + run core harness; run shell/chrome/migration gates → OK.
- [ ] **Step 5:** Commit: `git commit -- native/comicreader/ComicReaderCore.h native/comicreader/ComicReaderCore.cpp qml/comicreader/ComicReaderShell.qml tests/comicreader_core_harness.cpp`

---

## Phase C — The felt-lag batch (Tier 2)

### Task C1: Cache the render-sized result (`cache: true`)

**Files:** Modify: `qml/comicreader/ComicReaderStripSurface.qml` (page Image), `qml/comicreader/ComicReaderDoubleSurface.qml` (both Images), `tests/test_comicreader_surfaces.ps1`

The top decode-audit finding. With `cache: false`, every delegate rebuild (scroll away 3 pages and back) re-fetches from the provider, which re-runs a full-res `SmoothTransformation` downscale per fetch. The `?rev=` in the URL is exactly why caching is SAFE: the rev bumps only on a genuine re-decode, so the cache key self-busts.

- [ ] **Step 1:** In all three page `Image`s, change `cache: false` to:

```qml
cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on redecode, and
               // without the pixmap cache every delegate rebuild re-pays a full-res
               // smooth downscale in the provider (the "scroll back up = stutter" cost)
```

- [ ] **Step 2:** Extend the static guard in `test_comicreader_surfaces.ps1` (after the sourceSize check):

```powershell
foreach ($pair in @(@("strip", $strip), @("double", $dbl))) {
    if ($pair[1] -match "cache:\s*false") {
        Write-Host ("FAIL: the " + $pair[0] + " surface must keep cache: true on page Images -")
        Write-Host "      cache: false re-runs the provider's full-res downscale on every delegate rebuild."
        exit 1
    }
}
```

- [ ] **Step 3:** Run surfaces + migration gates → OK. (The decode-refresh tests pin that a rev bump re-fetches — they must stay green; if one fails, instrument before touching it.)
- [ ] **Step 4:** Commit: `git commit -- qml/comicreader/ComicReaderStripSurface.qml qml/comicreader/ComicReaderDoubleSurface.qml tests/test_comicreader_surfaces.ps1`

### Task C2: Wire `visiblePages` → `setVisible` (strip pinning)

**Files:** Modify: `qml/comicreader/ComicReaderShell.qml` (stripSurface instantiation); Test: `tests/comicreader_shell_harness.qml`

Two audits found it independently: the strip computes and emits `visiblePages` and nothing consumes it, so in strip mode no page is ever pinned — the page on screen can be evicted under memory pressure. One line closes it (and gives visible strip pages `kPrioVisible`).

- [ ] **Step 1: Failing test.** Shell harness, strip section:

```qml
// the strip's visiblePages must reach core.setVisible (pinning + visible priority)
var vCore2 = ...   // the strip-mode shell's fake core (FakeCore already spies setVisible via lastVisible)
```

Add to FakeCore: `property var lastVisible: null` and in `setVisible(pages)`: `lastVisible = pages`. Then:

```qml
sShell.stripSurfaceItem.visiblePages([2, 3, 4])   // or emit via the surface's signal on the mounted shell
ck(sCore.lastVisible !== null && sCore.lastVisible.length === 3,
   "strip: visiblePages must reach core.setVisible, got " + JSON.stringify(sCore.lastVisible))
```

(The harness needs a handle on the mounted strip surface: add `readonly property alias stripSurfaceItem: stripSurface` to the shell — it is test-only observability, matching the existing alias pattern.)

- [ ] **Step 2:** Run shell gate → FAIL.
- [ ] **Step 3: Fix.** In the shell's `ComicReaderStripSurface` block:

```qml
// Pin what the reader is LOOKING at. Without this the strip never pins anything and
// the LRU can evict the on-screen page mid-read (TB2 pins its whole zone per refresh).
onVisiblePages: function (indices) { if (reader.core && reader.core.setVisible) reader.core.setVisible(indices) }
```

- [ ] **Step 4:** Run shell + surfaces gates → OK.
- [ ] **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderShell.qml tests/comicreader_shell_harness.qml`

### Task C3: Strip decode priority by distance from the viewport centre

**Files:** Modify: `native/comicreader/ComicReaderCore.h/.cpp` (`setStripViewport` + a pure helper); Test: `tests/comicreader_core_harness.cpp`

Today the window's FIRST pages get the highest priority — and the window starts 1.5 screens BEHIND you, so after a seek the decode lanes work backwards through pages you've read while the page under your eyes waits.

- [ ] **Step 1: Failing test:**

```cpp
// ── Test 18: strip decode priority peaks at the viewport centre ──────────────
{
    CHECK(stripDecodePriority(10, 10) == kPrioStripBase, "T18 centre page gets the base priority");
    CHECK(stripDecodePriority(9, 10) == stripDecodePriority(11, 10),
          "T18 priority is symmetric around the centre");
    CHECK(stripDecodePriority(10, 10) > stripDecodePriority(13, 10),
          "T18 priority falls with distance");
    CHECK(stripDecodePriority(0, 200) >= 1, "T18 priority never underflows");
    CHECK(stripDecodePriority(5, -1) == kPrioStripBase, "T18 no centre (empty model) -> flat base");
}
```

Expose in the header (file scope, `namespace comicreader`): `int stripDecodePriority(int page, int centrePage);` and make `kPrioStripBase` visible to the test (move the priority constants from the anonymous namespace in the .cpp to the header as `inline constexpr`, or redeclare the two needed in the header — keep values identical: `kPrioVisible=100, kPrioStripBase=70`).

- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement:**

```cpp
int stripDecodePriority(int page, int centrePage) {
    if (centrePage < 0)
        return kPrioStripBase;
    return qMax(1, kPrioStripBase - qAbs(page - centrePage));
}
```

And in `setStripViewport`:

```cpp
const QVector<int> w = m_strip->window(top, height, 1.5);
// Priority peaks AT the viewport centre and falls off symmetrically. The window
// starts 1.5 screens above the fold, so ordering by window position handed the
// best priority to pages the reader had already finished.
const int centre = m_strip->pageAtCenter(top, height);
for (int p : w)
    m_decode->request(p, stripDecodePriority(p, centre));
flushStripCompensation();
```

- [ ] **Step 4:** Build + run core harness → OK.
- [ ] **Step 5:** Commit: `git commit -- native/comicreader/ComicReaderCore.h native/comicreader/ComicReaderCore.cpp tests/comicreader_core_harness.cpp`

### Task C4: Latest-visible-wins decode ordering (nav coalescing)

**Files:** Modify: `native/comicreader/ComicReaderCore.h/.cpp` (`setVisible`); Test: `tests/comicreader_core_harness.cpp`

Hold the page-turn key through 15 pages: 15 equal-priority decodes queue FIFO, and the page you LAND on decodes last — a blank stare after the input already stopped (TB2 coalesces to the latest target, `ComicReader.cpp:1310-1318`). Fix: each `setVisible` call outranks every earlier one, so the landing page always jumps the queue. Stale pages still decode later (harmless — they are prefetch-adjacent).

- [ ] **Step 1: Failing test.** Use the decode worker hooks (`m_testOnWorkerEnter/Exit` — follow the existing stall pattern in this harness, e.g. the T3/T9 machinery) to hold both workers busy on pages 0 and 1, then:

```cpp
// ── Test 19: the LATEST setVisible outranks every earlier queued request ─────
// (workers stalled on 0,1; queue three visibles; release; the last-requested visible
//  page must be the first non-stalled page to become ready)
core.setVisible(QVariantList{2});
core.setVisible(QVariantList{3});
core.setVisible(QVariantList{4});
// release the stall, collect pageReady order into readyOrder (connect before releasing)
...
CHECK(readyOrder.indexOf(4) < readyOrder.indexOf(2),
      "T19 the most recent visible page decodes BEFORE an older queued one");
```

(Adapt the stall/release helper names to what the harness already uses; the assertion is the contract.)

- [ ] **Step 2:** Build + run → FAIL (FIFO order: 2 before 4).
- [ ] **Step 3: Implement.** Header: `int m_visibleBoost = 0;` (reset in `resetEntryState`). In `setVisible`, before the request loop:

```cpp
// LATEST-WINS: each visible set outranks every earlier one still in the queue, so a
// held page-turn decodes the page you LAND on first instead of last. The boost is
// per-request-wave, monotonic within an entry, and always above the strip band (<=70).
m_visibleBoost += 8;
const int prioVisible = kPrioVisible + m_visibleBoost;
```

then use `prioVisible`, `prioVisible - 3`, `prioVisible - 4`, `prioVisible - 6` in place of `kPrioVisible`, `kPrioNext1`, `kPrioNext2`, `kPrioPrev` respectively (relative order within a wave preserved; every wave beats all prior waves).

- [ ] **Step 4: Pair-aware backwards prefetch (same function, same wave).** TB2 prefetches BOTH halves of the previous pair (`ComicReader.cpp:1621-1626`); ours prefetches one page back, so flipping backwards in RTL manga lands on a pair where one half pops in late. Replace the single `minV - 1` request with:

```cpp
if (minV - 1 >= 0) {
    // pair-aware: flipping BACK lands on a UNIT — prefetch both of its halves or the
    // second one pops in late (most visible re-reading backwards in RTL manga).
    const QVariantMap prevUnit = unitForPage(minV - 1);
    const int pr = prevUnit.value(QStringLiteral("rightIndex"), -1).toInt();
    const int pl = prevUnit.value(QStringLiteral("leftIndex"), -1).toInt();
    if (pr >= 0) m_decode->request(pr, prioVisible - 6);
    if (pl >= 0) m_decode->request(pl, prioVisible - 6);
}
```

Failing test first (T19b, alongside T19): with the two-leading-singles pairing over the 6-page plain fixture the units are `[0][1][2,3][4,5]`; `setVisible({4})` must decode BOTH 2 and 3 (old behaviour: only 3):

```cpp
core.setVisible(QVariantList{4});
const bool both = waitFor([&] { return core.readyCount() >= 4; });   // 2,3,4,5
CHECK(both, "T19b backwards prefetch fetches BOTH halves of the previous unit");
CHECK(core.pageInfo(2).value(QStringLiteral("decoded")).toBool(), "T19b the far half (2) decoded");
```

- [ ] **Step 5:** Build + run core harness (all tests) → OK.
- [ ] **Step 6:** Commit: `git commit -- native/comicreader/ComicReaderCore.h native/comicreader/ComicReaderCore.cpp tests/comicreader_core_harness.cpp`

### Task C5: Early dimension hint from the image header

**Files:** Modify: `native/comicreader/ComicReaderDecode.cpp` (runnable + a queued dims path), `native/comicreader/ComicReaderStripModel.cpp` (lock condition ×2); Test: `tests/comicreader_core_harness.cpp`

TB2 learns a page's true size from the header bytes before the full decode (`DecodeTask.cpp:26-29`), so the column geometry settles in milliseconds; we wait for the whole decode and pay a trickle of anti-jump corrections instead. `QImageReader::size()` is header-only for PNG/JPEG/WebP.

- [ ] **Step 1: Failing test.** A file whose header parses but whose body is truncated: the strip must learn the REAL size even though the decode fails.

```cpp
// ── Test 20: header dims land before (and despite) the full decode ──────────
{
    // a valid 800x1200 png, then truncate the byte stream after the header
    const QString whole = dir.filePath(QStringLiteral("trunc_src.png"));
    CHECK(writeSolidPng(whole, 90, 800, 1200), "setup: t20 source");
    QFile in(whole); in.open(QIODevice::ReadOnly);
    const QByteArray head = in.read(256); in.close();          // IHDR lives in the first ~33 bytes
    const QString trunc = dir.filePath(QStringLiteral("trunc.png"));
    QFile out(trunc); out.open(QIODevice::WriteOnly); out.write(head); out.close();

    ComicReaderCore core;
    core.openEntry(QStringLiteral("t20"),
                   pagesFromPaths(QStringList() << trunc << plain[1] << plain[2]),
                   QStringLiteral("ltr"), manualNormal());
    core.setStripViewportWidth(1000);
    core.setStripViewport(0, 800);
    const bool failed = waitFor([&] {
        return core.pageInfo(0).value(QStringLiteral("error")).toString() != QStringLiteral("none");
    });
    CHECK(failed, "T20 the truncated page fails its decode");
    // 800x1200 at 78% of 1000 => width 780, height 780*1200/800 = 1170 (vs estimate 1170? the
    // ESTIMATE is 1600x2400 => 780*1.5 = 1170 too — use a LANDSCAPE header so they differ):
}
```

**Correction baked into the test:** use `writeSolidPng(whole, 90, 1200, 600)` (landscape 2:1). Estimated height at 780 wide = 1170; real from header = 780 × 600/1200 = 390 (and it detects as a spread → full width 1000, height 500). Assert:

```cpp
    QAbstractListModel* m = core.stripModel();
    const double h0 = m->data(m->index(0, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
    CHECK(qAbs(h0 - 500.0) < 2.0,
          "T20 the strip learned the header's real (landscape/spread) size despite the failed decode");
}
```

- [ ] **Step 2:** Build + run → FAIL (height stays the 1170 estimate; a failed decode never reports a size).
- [ ] **Step 3: Implement.** In `DecodeRunnable::run()`, immediately after `file.close()`:

```cpp
// EARLY DIMENSION HINT (TB2 DecodeTask parity): the header names the true size for
// a fraction of the decode's cost. Publishing it now lets the strip column snap to
// real geometry (and pairing learn a spread) before — or even without — the pixels.
{
    QBuffer probeBuf(&bytes);
    probeBuf.open(QIODevice::ReadOnly);
    QImageReader probe(&probeBuf);
    probe.setDecideFormatFromContent(true);
    probe.setAutoTransform(true);
    const QSize dims = probe.size();          // header-only for png/jpeg/webp
    if (dims.isValid() && dims.width() > 0 && dims.height() > 0)
        reportDims(dims);                     // queued back to the coordinator's thread
}
```

`reportDims` follows exactly the same queued-invoke path the runnable already uses for its result (mirror the existing mechanism — same target object, new slot `onWorkerDims(quint64 gen, int page, QSize dims)`). In `ComicReaderDecode::onWorkerDims`: drop if `gen != m_currentGen`; build the page's `PageMeta` copy with `sourceSize = dims`, `detectedSpread = spreadRatioExceeded(dims)`, `decoded = false`; `emit metaReady(gen, meta)` (the existing Core path handles the rest: strip `updatePage`, spread discovery, `rebuildUnits`).
In `ComicReaderStripModel` change BOTH lock conditions (`rebuild` ~line 99 and `updatePage` ~line 151) from `pages[i].decoded && sourceSize valid` to `sourceSize.width() > 0 && sourceSize.height() > 0` — the class comment already says `ReadyRole` tracks `decoded` separately, so a hint-locked size with `decoded=false` is exactly the documented shape.

- [ ] **Step 4:** Build + run core harness (ALL tests — T4's spread-discovery path and the strip harness's estimate tests must still pass; if one disagrees, instrument first) → OK. Run strip harness too.
- [ ] **Step 5:** Commit: `git commit -- native/comicreader/ComicReaderDecode.cpp native/comicreader/ComicReaderStripModel.cpp tests/comicreader_core_harness.cpp`

### Task C6: `MissingFile` retries after a cooldown

**Files:** Modify: `native/comicreader/ComicReaderDecode.cpp/.h`; Test: `tests/comicreader_core_harness.cpp`

The failure latch is right for corrupt files but wrong for `MissingFile`: a page touched while still being written shows "Page missing" for the life of the generation. TB2 self-heals on the next pass. Keep the latch for decode errors; give MissingFile a 2s cooldown.

- [ ] **Step 1: Failing test:**

```cpp
// ── Test 21: a missing page HEALS once the file appears (cooldown, not a latch) ──
{
    const QString late = dir.filePath(QStringLiteral("late.png"));   // does NOT exist yet
    QVariantList pages = pagesFromPaths(QStringList() << plain[0] << plain[1]);
    QVariantMap pm; pm.insert("index", 2); pm.insert("url", QUrl::fromLocalFile(late).toString()); pm.insert("group", "");
    pages.append(pm);
    ComicReaderCore core;
    core.openEntry(QStringLiteral("t21"), pages, QStringLiteral("ltr"), manualNormal());
    core.setVisible(QVariantList{2});
    // it fails MissingFile (parsePages already stamped it; the decode confirms)
    QThread::msleep(2200);                     // ride out the retry cooldown (kMissingRetryMs = 2000)
    CHECK(writeSolidPng(late, 100), "setup: the late file finally lands");
    core.setVisible(QVariantList{2});          // a fresh request after the cooldown
    const bool healed = waitFor([&] { return core.readyCount() >= 3; });
    CHECK(healed, "T21 the page decodes once the file exists — MissingFile is not a life sentence");
}
```

**Note:** `parsePages` stamps `PageError::MissingFile` for a nonexistent file at open — check whether that stamp alone blocks the decode request (`m_pageByIndex` content). If openEntry filters it out entirely, the test must instead delete a file AFTER open and before first request; adapt while keeping the assertion (a MissingFile page heals on a later request).

- [ ] **Step 2:** Build + run → FAIL (latched forever).
- [ ] **Step 3: Implement.** Header: `QHash<int, qint64> m_missingRetryAt;` + `static constexpr qint64 kMissingRetryMs = 2000;` (clear the hash in `openGeneration`). In `onWorkerResult`'s failure branch:

```cpp
if (error == PageError::MissingFile)
    m_missingRetryAt.insert(page, QDateTime::currentMSecsSinceEpoch() + kMissingRetryMs);
else
    m_failed.insert(page);   // corrupt/unsupported stays latched — re-decoding garbage heals nothing
```

In `request()`, replace the flat `m_failed` early-out with:

```cpp
if (m_failed.contains(page))
    return;
const auto retryIt = m_missingRetryAt.constFind(page);
if (retryIt != m_missingRetryAt.constEnd()) {
    if (QDateTime::currentMSecsSinceEpoch() < retryIt.value())
        return;                       // still cooling down — no per-frame stat storm
    m_missingRetryAt.erase(retryIt);  // cooldown over: one fresh attempt
}
```

- [ ] **Step 4:** Build + run core harness → OK (this test takes ~2.5s; that is acceptable for one gate).
- [ ] **Step 5:** Commit: `git commit -- native/comicreader/ComicReaderDecode.h native/comicreader/ComicReaderDecode.cpp tests/comicreader_core_harness.cpp`

### Task C7: DPR probe (diagnostic only — no code change without evidence)

**Files:** Create: `tests/_dpr_probe.qml` (throwaway, do not commit unless it finds a bug)

The strip audit flagged UNVERIFIED: does Qt multiply `sourceSize` by devicePixelRatio before it reaches the provider? If not, the decode cap renders soft on a 125/150% Windows display.

- [ ] **Step 1:** Write the probe:

```qml
import QtQuick
Item {
    width: 400; height: 400
    Image { id: img; source: ""; sourceSize.width: 1100 }
    Component.onCompleted: {
        console.log("PROBE dpr=" + Screen.devicePixelRatio + " requested sourceSize=" + img.sourceSize.width)
        Qt.exit(0)
    }
}
```

Run twice: plain, and with `QT_SCALE_FACTOR=1.5` in the environment, both via `qml.exe -platform offscreen`. The definitive check is what the PROVIDER receives — if this is inconclusive, add a temporary `qDebug() << "provider requestedSize" << requestedSize;` in `ComicReaderProvider::requestImage`, run `comicreader_core_harness` under `QT_SCALE_FACTOR=1.5`, and read the log.
- [ ] **Step 2:** If requestedSize is NOT scaled by DPR: multiply `srcCapW` by `Screen.devicePixelRatio` in both surfaces (`Math.ceil(base * Screen.devicePixelRatio)` clamped to 2800) and commit with the measurement in the message. If it IS scaled: record "verified, no change" in this plan file and move on. Either way, remove the probe/qDebug.

---

## Phase D — Double-page geometry

### Task D1: Unified pair scale, vertical centring, gutter follows the pages

**Files:** Modify: `qml/comicreader/ComicReaderDoubleSurface.qml` (the spread block); Test: `tests/comicreader_surfaces_harness.qml`

The one substantial item. Both references compute ONE shared scale for a pair (`ReaderEngine.js:144`; TB2 `:1551-1556`) so mismatched trims still meet flush at the spine at true relative size, centred vertically. Ours scales each half independently (tops aligned, bottoms ragged, art scale jumps across the gutter), pins short content to the top, and runs the gutter shadow the full viewport height.

- [ ] **Step 1: Failing test.** In the surfaces harness double section (the harness's fake provider serves images with known implicit sizes — follow its existing fixture pattern; if implicit sizes aren't controllable there, drive the geometry properties directly):

```qml
// unified pair scale: one scale for both halves, heights centred, gutter on the pages
// (fixture: right page 800x1200, left page 780x1240 — mismatched trims)
ck(Math.abs(dbl._pairScale * 800 - dbl.rightImgItem.width) < 1.0, "pair: right width = iw * shared scale")
ck(Math.abs(dbl._pairScale * 780 - dbl.leftImgItem.width) < 1.0, "pair: left width = iw * SAME scale")
ck(Math.abs((dbl.rightImgItem.width / 800) - (dbl.leftImgItem.width / 780)) < 1e-6,
   "pair: both halves share ONE scale — no size jump across the gutter")
// flush at the spine: right half's left edge and left half's right edge meet at _halfW
// vertical centring: a pair shorter than the viewport is centred, not top-pinned
ck(dbl.pairTopY > 0, "pair: short content is vertically centred (top offset > 0)")
// gutter shadow spans the drawn pages, not the viewport
ck(Math.abs(dbl.gutterShadowItem.height - dbl.pairDrawnHeight) < 1.0, "pair: gutter height = drawn page height")
```

(Expose `rightImgItem`/`leftImgItem` aliases and the two readonly helpers below; adapt names to the harness's access pattern.)

- [ ] **Step 2:** Run surfaces gate → FAIL.
- [ ] **Step 3: Implement.** Replace the geometry of the spread block:

```qml
// ================= unified pair geometry =================
// ONE scale for the whole displayed unit (min over both halves' fits) — the lineage
// law (ReaderEngine.js sc=min(...); TB2 "B2: Unified pair scale"). Pages keep their
// true relative size, meet flush at the spine, and centre vertically as a block.
readonly property real _fitH: height * zoomFactor
function _fitScale(iw, ih, boxW, boxH) {
    return (iw > 0 && ih > 0) ? Math.min(boxW / iw, boxH / ih) : 0
}
readonly property real _pairScale: {
    var sR = _fitScale(rightImg.implicitWidth, rightImg.implicitHeight,
                       isPair ? _halfW : _contentW, _fitH)
    var sL = isPair ? _fitScale(leftImg.implicitWidth, leftImg.implicitHeight, _halfW, _fitH) : 0
    if (sR > 0 && sL > 0) return Math.min(sR, sL)
    return Math.max(sR, sL)     // one half not decoded yet: fit the loaded one alone
}
readonly property real _drawnHR: rightImg.implicitHeight * _pairScale
readonly property real _drawnHL: isPair ? leftImg.implicitHeight * _pairScale : 0
readonly property real pairDrawnHeight: Math.max(_drawnHR, _drawnHL)
// centred when it fits; pan owns the offset when it doesn't
readonly property real pairTopY: Math.max(0, (height - pairDrawnHeight) / 2) - panY
readonly property real panYMax: Math.max(0, pairDrawnHeight - height)
```

Images (pair case — right half flush LEFT edge at the spine, left half flush RIGHT edge):

```qml
Image {   // rightIndex — physical RIGHT in RTL, LEFT in LTR
    id: rightImg
    width: implicitWidth * root._pairScale
    height: implicitHeight * root._pairScale
    x: root.isPair
       ? (root.rtl ? root._halfW : root._halfW - width)                    // flush to the spine
       : (root._contentW - width) / 2                                     // single/spread: centred
    y: root.pairTopY + (root.pairDrawnHeight - height) / 2                // centre within the pair band
    ...   // source/asynchronous/cache/retainWhileLoading/sourceSize/mipmap unchanged
}
Image {   // leftIndex — mirrors it
    id: leftImg
    width: implicitWidth * root._pairScale
    height: implicitHeight * root._pairScale
    x: root.rtl ? root._halfW - width : root._halfW
    y: root.pairTopY + (root.pairDrawnHeight - height) / 2
    ...
}
```

**Careful:** `fillMode: Image.PreserveAspectFit` becomes redundant (explicit w/h now exact-ratio) — keep it, it is harmless and guards a rounding sliver. Delete the old `width: root.isPair ? root._halfW : root._contentW` / derived-height lines and the old `x`/`y` expressions. Preserve the exposed observability (`rightIndexX`, `leftIndexX`, `singleImageWidth`, sources) — recompute `rightIndexX = content.x + rightImg.x` etc. so the direction assertions stay true. Update `_maxImgH` uses: replace with `pairDrawnHeight` (delete `_maxImgH`).

Gutter shadow:

```qml
Rectangle {
    id: gutterShadow
    visible: root.isPair && root.gutterStrength > 0
    width: 18
    height: root.pairDrawnHeight            // the pages, not the viewport
    x: root._halfW - width / 2
    y: root.pairTopY                        // rides the centred pair (and the pan)
    ...gradient unchanged
}
```

- [ ] **Step 4:** Run surfaces + migration + chrome gates → OK (the direction-assert tests exercise `rightIndexX`/`leftIndexX`; if one fails, print both values and the image x's before editing anything).
- [ ] **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderDoubleSurface.qml tests/comicreader_surfaces_harness.qml`

### Task D2: A zoom step keeps the pan (clamped)

**Files:** Modify: `qml/comicreader/ComicReaderDoubleSurface.qml` (`setZoom`); Test: `tests/comicreader_surfaces_harness.qml`

References: TB2 `applyPan` preserves and clamps (`:2121-2156`); Reader 1 clamps only. Ours zeroes both axes — pan to a panel, press Ctrl+= once more, and you are staring at the far corner.

- [ ] **Step 1: Failing test:**

```qml
dblSurface.setZoom(200)
dblSurface.panBy(120, 80)
var keepX = dblSurface.panX, keepY = dblSurface.panY
dblSurface.setZoom(220)
ck(dblSurface.panX === Math.min(keepX, dblSurface.panXMax) && dblSurface.panX > 0,
   "zoom: a step must KEEP the pan (clamped), got panX " + dblSurface.panX)
dblSurface.setZoom(100)
ck(dblSurface.panX === 0 && dblSurface.panY === 0, "zoom: returning to 100% clamps pan to 0 naturally")
```

- [ ] **Step 2:** Run → FAIL. **Step 3: Fix:**

```qml
function setZoom(pct) {
    zoomPercent = Math.max(100, Math.min(260, Math.round(pct)))
    // keep the reader's place: clamp the existing pan into the new bounds (TB2 applyPan),
    // never zero it — a zoom step used to teleport you to the top-left corner.
    panX = Math.max(0, Math.min(panXMax, panX))
    panY = Math.max(0, Math.min(panYMax, panY))
}
```

- [ ] **Step 4:** Run surfaces gate → OK. **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderDoubleSurface.qml tests/comicreader_surfaces_harness.qml`

---

## Phase E — Input & chrome polish (Tier 3)

### Task E1: Up/Down scroll the strip; side-zone strip clicks scroll

**Files:** Modify: `qml/comicreader/ComicReaderInput.qml` (keyAction Up/Down + releaseAt); Test: `tests/comicreader_chrome_harness.qml`

Up/Down: swallowed in strip today (`:117-120` — the double-page "never flips" ruling correctly applies only to double). References: Reader 1 `:929-930` (12% step, Shift 25%), TB2 `:3434-3445`. Side-zone click in strip: falls through to the 220ms chrome toggle; Reader 1 scrolls ±0.82 screens (`:1268-1270`).

- [ ] **Step 1: Failing tests** (chrome harness drives `keyAction`/`releaseAt` as pure functions — follow its existing call pattern):

```qml
input.mode = "long_strip"
var sawScroll = 0
input.scrollBy.connect(function (s) { sawScroll = s })
ck(input.keyAction(Qt.Key_Down, 0) === "scrollBy" && Math.abs(sawScroll - 0.12) < 1e-9,
   "strip keys: Down scrolls 0.12 screens")
ck(input.keyAction(Qt.Key_Up, Qt.ShiftModifier) === "scrollBy" && Math.abs(sawScroll + 0.25) < 1e-9,
   "strip keys: Shift+Up scrolls -0.25 screens")
// double mode ruling unchanged: Up/Down never flip, pan-or-swallow only
input.mode = "double_page"
ck(input.keyAction(Qt.Key_Down, 0) !== "scrollBy", "double keys: Down still never scrolls/flips")
// strip side-zone click scrolls instead of toggling chrome
input.mode = "long_strip"
input.pressAt(10, 600); var tok = input.releaseAt(10, 600)
ck(tok === "scrollBy" && Math.abs(sawScroll + 0.82) < 1e-9, "strip zones: left-third click scrolls back 0.82")
input.pressAt(590, 600); tok = input.releaseAt(590, 600)
ck(tok === "scrollBy" && Math.abs(sawScroll - 0.82) < 1e-9, "strip zones: right-third click scrolls forward 0.82")
```

- [ ] **Step 2:** Run chrome gate → FAIL.
- [ ] **Step 3: Implement.** keyAction Up/Down block becomes:

```qml
if (key === Qt.Key_Up || key === Qt.Key_Down) {
    if (dbl) {
        // double-page: vertical = PAN, never a flip (Tankoban Max strict model, ruling 2026-07-17)
        if (zoomed || vScrollMax > 0) { panBy(0, key === Qt.Key_Up ? -panKeyStep : panKeyStep); return "panBy" }
        return ""
    }
    // strip: the most instinctive fine-scroll key (Reader 1: 12% / Shift 25% of a screen)
    var shift = (mods & Qt.ShiftModifier) !== 0
    scrollBy((key === Qt.Key_Down ? 1 : -1) * (shift ? 0.25 : 0.12))
    return "scrollBy"
}
```

`releaseAt`, before the single-click timer arm:

```qml
if (mode !== "double_page") {
    var zStrip = zoneForX(x, w)
    if (zStrip === "left")  { scrollBy(-0.82); return "scrollBy" }   // Reader 1's step
    if (zStrip === "right") { scrollBy(0.82);  return "scrollBy" }
}
```

- [ ] **Step 4:** Run chrome + migration gates → OK. **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderInput.qml tests/comicreader_chrome_harness.qml`

### Task E2: Keyboard/scrub repositions ride the drain (glide) or the halt door

**Files:** Modify: `qml/comicreader/ComicReaderStripSurface.qml` (new `smoothScrollBy`, refactor `_intakeWheel`), `qml/comicreader/ComicReaderShell.qml` (`_stripScroll`, `scrubToFraction`, `firstPageNav`, `lastPageNav`); Test: `tests/comicreader_surfaces_harness.qml`

Raw `contentY` writes bypass both the glide and the backlog: hit Space mid-glide and the view jumps *then keeps sliding* on leftover wheel input. Reader 1 routes keys through `smoothScrollBy` and pins instant moves through `haltScrollAt` (`:688-704`).

- [ ] **Step 1: Failing test:**

```qml
// keyboard scroll GLIDES through the drain and inherits its backlog discipline
stripSurface.haltScrollAt(0)
stripSurface.smoothScrollBy(300)
ck(stripSurface._pendingWheelPx === 300, "glide: smoothScrollBy feeds the drain backlog")
// an instant reposition mid-glide drops the leftover backlog (no jump-then-slide)
stripSurface.haltScrollAt(500)
ck(stripSurface._pendingWheelPx === 0 && stripSurface.contentY === 500,
   "glide: haltScrollAt pins AND drops in-flight backlog")
```

- [ ] **Step 2:** Run → FAIL (`smoothScrollBy` missing). **Step 3: Implement.** Surface:

```qml
// keyboard/API glide — same accumulator as the wheel, so Space feels like a big notch
function smoothScrollBy(px) {
    if (px === 0) return
    if (!scrollDrain.running) { _smoothY = list.contentY; _drainFresh = true }
    _pendingWheelPx = Math.max(-_maxBacklogPx, Math.min(_maxBacklogPx, _pendingWheelPx + px))
    if (!scrollDrain.running) scrollDrain.running = true
}
```

Refactor `_intakeWheel` to end with `smoothScrollBy(-dy)`-equivalent (keep its provenance lines: `_userInteracted = true; manualNavigation()` fire before the shared call). Shell:

```qml
function _stripScroll(screens) { stripSurface.smoothScrollBy(screens * stripSurface.height) }
function scrubToFraction(frac) {
    var f = Math.max(0, Math.min(1, frac))
    stripFraction = f
    var span = stripSurface.contentHeight - stripSurface.height
    if (span > 0) stripSurface.haltScrollAt(f * span)      // a seek is instant and final
}
function firstPageNav() { currentPage = 1; if (mode === "long_strip") stripSurface.haltScrollAt(0) }
function lastPageNav() {
    ...existing page math unchanged...
    if (mode === "long_strip") stripSurface.haltScrollAt(Math.max(0, stripSurface.contentHeight - stripSurface.height))
}
```

- [ ] **Step 4:** Run surfaces + shell gates → OK. **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderStripSurface.qml qml/comicreader/ComicReaderShell.qml tests/comicreader_surfaces_harness.qml`

### Task E3: Modal gates the key map; auto-hide pauses on modal + chrome hover

**Files:** Modify: `qml/comicreader/ComicReaderInput.qml` (one line), `qml/comicreader/ComicReaderHud.qml` (`_autoHide` guard + HoverHandler); Test: `tests/comicreader_chrome_harness.qml`

Keys leak through the open settings sheet (M re-lays out the book behind it — Reader 1 gates at `:875`); the chrome fades under an open sheet (our own Task-12 carry comment says to do this) and fades out from under a resting cursor (Reader 1 `frozen` at `:826-830`).

- [ ] **Step 1: Failing tests:**

```qml
// keys are gated while a modal is open — only Escape acts
input.modalOpen = true
ck(input.keyAction(Qt.Key_M, 0) === "", "modal: M must not re-layout the book behind the sheet")
ck(input.keyAction(Qt.Key_Escape, 0) === "closeTop", "modal: Escape still closes the sheet")
input.modalOpen = false
// auto-hide pauses while a modal is open
fakeReader.modalOpen = true            // (or however the harness drives hud.modalOpen)
hud._autoHide()
ck(fakeReader.chromeVisible === true, "autohide: an open modal must hold the chrome")
fakeReader.modalOpen = false
```

- [ ] **Step 2:** Run chrome gate → FAIL. **Step 3: Implement.** Input, directly after the Escape branch:

```qml
// an open overlay owns the keyboard — everything except Escape is gated (Reader 1 :875)
if (modalOpen) return ""
```

HUD:

```qml
// chrome is HELD while a modal is up or the pointer rests on the chrome itself —
// reaching for a pill and pausing to aim must not fade the pill out from under you.
readonly property bool _holdChrome: modalOpen || chromeHover.hovered
function _autoHide() {
    if (_holdChrome) { autoHideTimer.restart(); return }
    if (reader) reader.chromeVisible = false
}
```

`modalOpen` in the HUD: `readonly property bool modalOpen: reader ? reader.modalOpen === true : false`. Add inside the footer/chrome container (the Item whose `opacity: hud.chromeVisible` — line ~301): `HoverHandler { id: chromeHover }`.

- [ ] **Step 4:** Run chrome + overlays gates → OK. **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderInput.qml qml/comicreader/ComicReaderHud.qml tests/comicreader_chrome_harness.qml`

### Task E4: Cursor auto-hide

**Files:** Modify: `qml/comicreader/ComicReaderShell.qml`; Test: `tests/comicreader_shell_harness.qml`

Neither reference leaves an arrow parked on the page: 3s idle with chrome hidden → blank cursor; any movement restores (TB2 `:424-434`; Reader 1 `:832-836, 1354-1361`).

- [ ] **Step 1: Failing test** (test the FLAG headless; the cursor itself is eyes-on):

```qml
var cShell = ...existing mounted shell...
cShell.recordDebounceMs = 20               // keep the harness fast if needed
ck(cShell._cursorIdle === false, "cursor: starts live")
cShell.cursorIdleMs = 50                   // test-tunable, like recordDebounceMs
// wait past it in the deferred phase:
ck(cShell._cursorIdle === true, "cursor: goes idle after the timeout")
cShell._pokeCursor()
ck(cShell._cursorIdle === false, "cursor: any activity restores it")
```

- [ ] **Step 2:** Run shell gate → FAIL. **Step 3: Implement.** Shell:

```qml
// ---- cursor auto-hide (3s, both references) ----
property int cursorIdleMs: 3000            // test-tunable
property bool _cursorIdle: false
function _pokeCursor() { _cursorIdle = false; cursorIdleTimer.restart() }
Timer { id: cursorIdleTimer; interval: reader.cursorIdleMs; onTriggered: reader._cursorIdle = true }
Component.onCompleted: cursorIdleTimer.restart()   // merge into the existing onCompleted body

// topmost, click-transparent: only sets the cursor shape (Reader 1's overlay, ported)
MouseArea {
    anchors.fill: parent
    z: 998                                  // above surfaces + HUD, below nothing that needs clicks
    acceptedButtons: Qt.NoButton
    cursorShape: (reader._cursorIdle && !reader.chromeVisible && !reader.modalOpen)
                 ? Qt.BlankCursor : Qt.ArrowCursor
}
```

Wire pokes: in the input wiring add `_pokeCursor()` inside the existing `onActivity:` handler (alongside `hud.notifyActivity()`), and in `onRevealRequested`.

- [ ] **Step 4:** Run shell + chrome gates → OK. **Step 5:** Commit: `git commit -- qml/comicreader/ComicReaderShell.qml tests/comicreader_shell_harness.qml`

### Task E5: Scrub bubble under the cursor + honest strip label; knob grows; one-page bar hides

**Files:** Modify: `qml/comicreader/ComicReaderHud.qml` (scrub block), `qml/comicreader/ComicReaderShell.qml` (pageAtFraction seam), `native/comicreader/ComicReaderCore.h/.cpp` (`stripPageAtCenter`); Test: `tests/comicreader_core_harness.cpp`, `tests/comicreader_chrome_harness.qml`

The bubble reports where you ARE, not where you point (both references follow the pointer: TB2 `:324-338`, Reader 1 `:1543-1552`); in strip the label is a linear estimate that ignores real page heights; the knob has no hover state; the bar renders for a one-page entry.

- [ ] **Step 1: C++ first (failing test T22):**

```cpp
// ── Test 22: stripPageAtCenter answers "what page is at this scroll position" ──
{
    ComicReaderCore core;
    core.openEntry(QStringLiteral("t22"), plainPages, QStringLiteral("ltr"), manualNormal());
    core.setStripViewportWidth(1000);
    QAbstractListModel* m = core.stripModel();
    const double top2 = m->data(m->index(2, 0), ComicReaderStripModel::TopRole).toDouble();
    const double h2 = m->data(m->index(2, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
    CHECK(core.stripPageAtCenter(top2 + h2 / 2 - 400, 800) == 2,
          "T22 the page whose band holds the viewport centre is returned");
    CHECK(core.stripPageAtCenter(0, 0) <= 0, "T22 degenerate viewport never crashes");
}
```

Header: `Q_INVOKABLE int stripPageAtCenter(double top, double viewportHeight) const;` Impl: `return m_pages.isEmpty() ? -1 : m_strip->pageAtCenter(top, viewportHeight);`
- [ ] **Step 2:** Build+run → RED then implement → GREEN.
- [ ] **Step 3: QML.** Shell exposes the resolver the HUD calls:

```qml
// what page a scrub fraction lands on — geometry-honest in strip, unit math in double
function pageAtFraction(frac) {
    var f = Math.max(0, Math.min(1, frac))
    if (mode === "long_strip" && core && core.stripPageAtCenter) {
        var span = Math.max(0, stripSurface.contentHeight - stripSurface.height)
        var p = core.stripPageAtCenter(f * span, stripSurface.height)
        if (p >= 0) return p + 1
    }
    return Math.max(1, Math.round(f * (Math.max(1, max) - 1)) + 1)
}
```

HUD scrub changes: (a) the scrub MouseArea gains `hoverEnabled: true`; (b) the bubble binds to the POINTER while hovering/dragging: `readonly property real pointerRatio: _ratioAt(<ma>.mouseX)` — bubble `x` centres on `<ma>.mouseX`, bubble text = `reader.pageAtFraction(pointerRatio)` (falls back to the resolver's double-mode math); (c) knob: `width: (<ma>.containsMouse || hud._scrubbing) ? 15 : 12` with a 120ms Behavior; (d) the whole scrub Item gains `visible: hud.max > 1`. Adapt `<ma>` to the scrub MouseArea's id; add `objectName`s where the test needs handles.
- [ ] **Step 4: Chrome test:**

```qml
ck(byName(hud, "scrubBubble") !== null, "bubble exists")
// with the fake reader in strip mode, pageAtFraction is consulted (spy on the fake)
fakeReader.pageAtFractionCalls = 0
...hover simulation or direct property drive per the harness pattern...
```

(Keep this test modest: assert the bubble's text binding calls `reader.pageAtFraction` — give FakeReader `function pageAtFraction(f) { pageAtFractionCalls += 1; return 42 }` and assert the bubble text contains "42".)
- [ ] **Step 5:** All gates → OK. Commit: `git commit -- native/comicreader/ComicReaderCore.h native/comicreader/ComicReaderCore.cpp qml/comicreader/ComicReaderHud.qml qml/comicreader/ComicReaderShell.qml tests/comicreader_core_harness.cpp tests/comicreader_chrome_harness.qml`

### Task E6: Anti-jump clamp; side-thumb cursor + drag binding; persist learned spreads; hide flushes the entry blob

**Files:** Modify: `qml/comicreader/ComicReaderStripSurface.qml` (`_applyCompensation`), `qml/comicreader/ComicReaderHud.qml` (side thumb), `native/comicreader/ComicReaderCore.cpp` (`persistedState`/`applyPersisted`/`openEntry` fold); Test: `tests/comicreader_surfaces_harness.qml`, `tests/comicreader_core_harness.cpp`

Three small, unrelated closers batched: (1) the compensation write is the only unclamped `contentY` write — near the top of a book a negative delta leaves a black band above page 1; (2) the side thumb reintroduces the `SizeVerCursor` TB2 explicitly reverted, and its `y` binding is destroyed by the first drag (binding vs `drag.target`); (3) detected spreads aren't persisted, so pairing can reshuffle between opens (Reader 1 `spreadKnown` / TB2 `knownSpreadIndices`).

- [ ] **Step 1 (clamp):** surfaces-harness: extend the compensation test — set `contentY` near 0, fire `stripCompensation(-200)`, assert `contentY === 0`. Fix:

```qml
function _applyCompensation(delta) {
    var maxY = Math.max(0, list.contentHeight - list.height)
    list.contentY = Math.max(0, Math.min(maxY, list.contentY + delta))
    _smoothY = list.contentY
}
```

(Check the existing 2000→2060 compensation test still passes — its fake content must be tall enough that the clamp is inert there; if not, raise the fixture's contentHeight, never weaken the assertion.)
- [ ] **Step 2 (thumb):** delete `cursorShape: Qt.SizeVerCursor`; replace the thumb's `y:` binding with:

```qml
Binding on y {
    when: !<thumbMa>.drag.active
    value: hud.fillRatio() * <span expression as-is>
    restoreMode: Binding.RestoreBindingOrValue   // the drag writes y imperatively; this re-arms tracking after
}
```

(adapt ids; the drag handler's existing seek wiring is untouched).
- [ ] **Step 3 (spreads), failing test T23:**

```cpp
// ── Test 23: learned spreads persist — pairing cannot reshuffle between opens ──
{
    ComicReaderCore core;
    QVariantMap p = manualNormal();
    QVariantList ds; ds.append(3);
    p.insert(QStringLiteral("detectedSpreads"), ds);
    core.openEntry(QStringLiteral("t23"), plainPages, QStringLiteral("ltr"), p);
    CHECK(core.unitForPage(3).value(QStringLiteral("spread")).toBool() == true,
          "T23 a persisted detected spread shapes the pairing BEFORE any decode");
    const QVariantList out = core.persistedState().value(QStringLiteral("detectedSpreads")).toList();
    CHECK(out.size() == 1 && out[0].toInt() == 3, "T23 detectedSpreads round-trips");
}
```

Implement: `applyPersisted` reads `detectedSpreads` into a member `QVector<int> m_persistedDetectedSpreads` (clear in `resetEntryState`); `openEntry` folds it onto `m_pages[idx].detectedSpread = true` alongside the spread-override fold; `persistedState()` emits the CURRENT set only when non-empty:

```cpp
QVariantList ds;
for (const PageMeta& pm : m_pages)
    if (pm.detectedSpread) ds.append(pm.index);
if (!ds.isEmpty())
    m.insert(QStringLiteral("detectedSpreads"), ds);
```

(T12's byte-identical round-trip stays green because absence round-trips as absence.)
- [ ] **Step 4 (hide flush):** the hide path flushes progress but leaves the 800ms `entrySave` debounce pending — an app kill in that window loses a just-made spread override (Reader 1 flushes both: `:160-163`). In the shell's `onVisibleChanged` hide branch, alongside `recordProgress()`:

```qml
entrySave.stop()
_saveEntryBlob()     // hide = leaving; a pending debounce must not be the only copy
```

Shell-harness test: give the fake core a blob, hide the shell, assert `entryRecords.all` contains the record immediately (no deferred phase needed).
- [ ] **Step 5:** Build + run core harness; surfaces + chrome + shell gates → OK.
- [ ] **Step 6:** Commit: `git commit -- qml/comicreader/ComicReaderStripSurface.qml qml/comicreader/ComicReaderHud.qml qml/comicreader/ComicReaderShell.qml native/comicreader/ComicReaderCore.h native/comicreader/ComicReaderCore.cpp tests/comicreader_surfaces_harness.qml tests/comicreader_shell_harness.qml tests/comicreader_core_harness.cpp`

---

## Phase F — Conditional tasks (execute ONLY per Phase A verdicts)

Each carries code for the RECOMMENDED option. A different verdict = a fresh planning pass for that item, not an improvisation.

### Task F1 (if R1=168): Wheel travel to Reader 1's tuning

**Files:** `qml/comicreader/ComicReaderStripSurface.qml` (`_intakeWheel`); update the surfaces-harness intake assertion (300 → 420 for 3 notches).

```qml
if (dy === 0) dy = angleY * 1.4    // Reader 1's house tuning: 168px/notch — the wheel he last read on
```

### Task F2 (if R2=per-series): Strip width/gap per series, global seed

**Files:** `qml/comicreader/ComicReaderShell.qml` (`_applySeriesPrefs`, `_saveSeriesPrefs`, `setStripLayout`); shell-harness test.
Series record grows to `{ rm, sw, sg }`. `_applySeriesPrefs` pushes `core.setStripLayout(rec.sw || globalPrefs.stripWidthPct, rec.sg !== undefined ? rec.sg : globalPrefs.stripGap)` after resolving `rm`; `setStripLayout` writes BOTH the global (seed for untouched series) and `_saveSeriesPrefs()` (this series). Test: two shells over one seriesRecords object — a width set in series A must not re-dress series B unless B has no record and the global moved.

### Task F3 (if R3=seed): Coupling phase seeds from the series record

**Files:** `qml/comicreader/ComicReaderShell.qml` (`_applyEntryPrefs`, save path), shell-harness test.
When the ENTRY has no blob and the series record carries `cp`, seed: `blob.couplingMode = "manual"; blob.couplingPhase = rec.cp` before `openEntry`. Write `cp` into the series record whenever the user nudges (`nudgeCoupling()` → after the core call, read `core.couplingState` phase, `_saveSeriesPrefs()` including it); `resetCoupling()` deletes `cp` from the record.

### Task F5 (if R5=toast): End-of-volume feedback

**Files:** `qml/comicreader/ComicReaderShell.qml` (`pageNext`), chrome/shell test.

```qml
function pageNext() {
    var b = _unitBoundsForIndex(currentPage - 1)
    var t = b[1] + 1
    if (t < max) { currentPage = t + 1 }
    else hud.showToast(hasNext ? "End — Next: use the pill or Alt+→" : "End of volume")
}
```

(mirror in `pagePrev` at the front edge only if he asks).

### Task F10 (if R10=extend): BackAction raised-label variant + HUD swap

**Files:** `qml/BackAction.qml` (new opt-in prop), `qml/comicreader/ComicReaderHud.qml` (back pill swap), `tests/test_back_action_p0.ps1` (goes green as-is — it greps the HUD for `BackAction {`).
BackAction gains:

```qml
property bool raisedLabel: false   // reader HUD: black drop shadow under label+chevron for over-page legibility
```

Label Text gains `style: root.raisedLabel ? Text.Raised : Text.Normal; styleColor: Qt.rgba(0,0,0,0.5)`; the chevron Shape gains a `visible: root.raisedLabel` duplicate offset (0,1) painted `Qt.rgba(0,0,0,0.5)` BEHIND it (Shapes have no styleColor — the shadow is a second ShapePath 1px down). HUD `icBack` Row is replaced by:

```qml
BackAction {
    id: icBack
    x: 18; y: 16
    variant: "plain"
    label: "Library"
    raisedLabel: true
    labelSize: 14
    idleColor: theme.ink
    hoverColor: theme.gold
    onTriggered: hud.backRequested()
}
```

**Careful:** the chrome harness asserts `icBack.glyphKind` via `iconKinds` — update that list to drop icBack (BackAction owns its chevron; the semantic-icon audit exempts the shared component) and update the audit test accordingly, saying why in the test comment. Verify `test_back_action_p0.ps1` passes.

---

## Phase G — Acceptance, review, merge

### Task G1: Task-14 resilience + performance gate

**Files:** per the original plan `docs/superpowers/plans/2026-07-23-comicreader.md` §Task 14 (its authoritative spec): create `tests/comicreader_perf_harness.cpp`, `tests/test_comicreader_acceptance.ps1`; modify `native/CMakeLists.txt` (grep-verify the shared file after editing — multi-agent collision rule).

The perf harness is design work at execution time; its CONTRACT is fixed here:
- Resilience: missing + corrupt pages among valid neighbours (build on T20/T21 fixtures); 300 rapid next/prev; entry A→B mid-decode ×20 asserting zero stale publishes; all-visible pinned under budget pressure; 512→256 transition; 500-page strip rebuild + resize ×10 with stable anchors.
- Numbers: decode workers high-water ≤ 2; stale publishes == 0; cache ≤ budget unless all-pinned; visible request→ready median < 350ms on local pages; warm strip scroll p95 ≤ 20ms (the p95 is measured on Hemanth's machine — the harness prints it, HE judges it).
- `test_comicreader_acceptance.ps1` runs every native + QML gate and prints `COMICREADER_ACCEPTANCE_OK` only when all pass.
- [ ] Steps: write harness → RED where it should be → fix or record → `COMICREADER_ACCEPTANCE_OK` → commit (CMakeLists by pathspec, with a grep-verify step: `git diff --cached native/CMakeLists.txt` shows ONLY the new target block).

### Task G2: Retire `qml/_readercheck.qml`

- [ ] `grep -rn "_readercheck" qml/ tests/ native/ docs/` → expect matches only in the file itself; then `git rm qml/_readercheck.qml`, run migration + shell gates, commit.

### Task G3: Full suite + committed-tree rebuild + boot smoke

- [ ] Run all 9 gates + 6 native harnesses (list in the header). Rebuild `colosseum` from the committed tree. Boot smoke ONLY with Hemanth's word (machine may be Agent 4's): launch detached, 12s, zero QML errors in the log, kill.

### Task G4: Codex cross-review (producer ≠ reviewer)

- [ ] Write a self-contained review prompt to `docs/superpowers/handoffs/2026-07-25-comicreader-final-stretch-review.md`: branch `agent1/comicreader`, range `916035f..HEAD`, the five audit reports' claims as the review checklist, P0/P1 blocks the merge. Hand Hemanth the prompt to fire (per doctrine: hand Codex a prompt, not a fired MCP). Fix anything it confirms; re-run gates.

### Task G5: Eyes-on + merge gate (EXPLICIT go only)

- [ ] Eyes-on script for Hemanth (the four lanes: manga chapter RTL, western LTR, tankoban volume, strip webtoon; resume, both modes, settings, danger row, bookmarks, the smooth scroll he flagged).
- [ ] On his explicit "merge": verify master isn't checked out anywhere (`git worktree list`); `git worktree add ..\..\..\_merge-master master` → `git merge --no-ff agent1/comicreader` → run migration gate + boot build there → `git push origin master` → remove the temp worktree. Announce in the haven's `agents/chat.md` (cross-lane wire): reader cutover + final stretch landed on master.
- [ ] Session recap + memory updates per house protocol.

---

## Deferred with reason (recorded so it reads as a decision, not a miss)

- **`openEntry` stats every page synchronously on the GUI thread** (`ComicReaderCore.cpp:100-111`, decode audit #9). ~200 blocking `QFileInfo::exists` calls at open on a cold path. Deferred: it is open-time cost (not scroll stutter), the decode worker re-checks existence anyway, and the honest fix (async stat pass) is disproportionate to this stretch. Revisit if cold-open ever reads slow on a network path.
- **Per-entry `finished` state** (persistence audit #9): TB2-only, keyed per book — a feature we never built, explicitly out of a parity plan.

## Self-review notes (already applied)

- Task ordering respects dependencies: B4 (toast) precedes B5/B6 which call `hud.showToast`; B2 depends on nothing new (seekToPage/stripPageTop shipped earlier today); C2 is ordered after B3 so the throttled emit drives pinning at 80ms, not per-frame; F10's harness edits are called out (iconKinds).
- Type consistency: `stripDecodePriority` (C3) and `stripPageAtCenter` (E5) are declared exactly once each; `_pendingStripFrac`/`_stripRestoreTries` names match Task B2 across shell and tests; FakeCore gains `lastVisible`/`fakePageInfo`/`lastSpreadOverride` where first used.
- Known deliberate non-fixes: R4–R9 skips are recorded as verdicts, not silently dropped; the audits' "we are ahead" categories (generation handling, eviction policy, anti-jump, drag-pan) are left untouched by design.
- The `wait()` helper in B3 and the stall/release hooks in C4 are adapt-to-harness points — the assertions are the contract, the scaffolding follows the file's existing pattern. If a harness lacks the scaffolding entirely, build it the way the sibling test in the same file does, never a new pattern.
