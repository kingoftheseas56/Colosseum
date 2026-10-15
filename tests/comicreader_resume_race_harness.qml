// Comic Reader — the Minimize/Continue-reading "always resumes at page 1" regression (2026-08).
//
// Root cause (confirmed by direct reproduction, not just source reading): ComicReaderStripSurface
// mounts and, via onActiveChanged, fires a Qt.callLater(_emitPresented) THE INSTANT it becomes
// active -- reporting whatever page the not-yet-restored contentY=0 column shows. That report used
// to schedule an unconditional debounced Progress write. The restore door (_runStripRestore) moves
// the column to the resumed page on its OWN timer, racing the same debounce. When the debounced
// write landed first, it banked page 1 over a correct resume -- and because the record IS the input
// to the NEXT resume, one bad write poisoned every subsequent open. This reproduced identically
// whether the reader was rebuilt via Minimize->taskbar OR a plain Continue-reading open: no
// Sessions/Main.qml code is involved in this harness, matching that confirmed fact.
//
// Fixes pinned here:
//   T1 — _onPresented() must not schedule a debounced write while a strip restore is pending.
//   T2 — manualActivity() disarms an in-flight restore (independent bug: a late restore firing
//        after the reader has already taken input would yank the column out from under a
//        reading user).
//   T3 — _runStripRestore()'s give-up branch (the column never laid out in time) must clear the
//        pending fraction arms, not just the try counter -- otherwise a LATER, unrelated mode
//        switch back into Long Strip inherits a stale arm and jumps to an abandoned spot.
//   T4 — goMinimize() flushes Progress synchronously before emitting minimizeRequested(), closing
//        the narrow window where the process could die while parked in the taskbar before
//        Component.onDestruction's own flush runs.
//
// HOUSE HARNESS PATTERN: a thrown error hangs qml.exe offscreen, so `ck` never throws — it collects
// failures; prints exactly one COMICREADER_RESUME_RACE_OK when clean, else one
// COMICREADER_RESUME_RACE_FAIL:<msg> per failure and Qt.exit(1).

import QtQuick

Item {
    id: harness
    width: 10; height: 10

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }

    // KEYED shared progress fake — get() returns what record() actually wrote, keyed by
    // (kind, id). The shared FakeProgress in comicreader_shell_harness.qml deliberately returns a
    // hand-preset value from get() regardless of what record() wrote, which is right for THAT
    // suite's isolated write/read assertions but cannot see a write→destroy→recreate→read loop —
    // exactly the loop this bug lives in. A dedicated fake here, rather than changing the shared
    // one, keeps this reproduction from touching a fixture ~30 other fixtures depend on.
    component SharedProgress: QtObject {
        property var entries: ({})
        function key(kind, id) { return String(kind) + "|" + String(id) }
        function record(payload) {
            if (!payload || !payload.kind || !payload.id) return
            entries[key(payload.kind, payload.id)] = JSON.parse(JSON.stringify(payload))
        }
        function get(kind, id) { return entries[key(kind, id)] || null }
        function forget(kind, id) { delete entries[key(kind, id)] }
    }

    component FakePageStore: QtObject {
        property var pages: []
        function localPages(cid) { return pages }
        function downloadChapter() {}
        function downloadIssue() {}
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
    }

    function fivePages() {
        var out = []
        for (var i = 0; i < 5; i++) out.push({ index: i, url: "file:///f/p" + i + ".png", group: -1 })
        return out
    }

    Component { id: progComp; SharedProgress {} }
    Component { id: storeComp; FakePageStore {} }
    property var shellComp: null

    function makeShell(cfg) {
        var full = Object.assign({
            "width": 640, "height": 480, "recordDebounceMs": 20,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        }, cfg)
        var inst = shellComp.createObject(harness, full)
        if (!inst) throw new Error("shell createObject returned null")
        return inst
    }

    property var sharedProg: null
    property var _t1ShellA: null
    property var _t1ShellB: null
    property var _t3Shell: null

    function step1_loadComponent() {
        shellComp = Qt.createComponent("../qml/comicreader/ComicReaderShell.qml")
        if (shellComp.status === Component.Error) {
            failures.push("shell component error: " + shellComp.errorString())
            report()
            return
        }
        step2_t1Setup()
    }

    // ===== T1: the resume race — a mount-time page-1 report must not overwrite a real resume =====
    function step2_t1Setup() {
        sharedProg = progComp.createObject(harness)
        var storeA = storeComp.createObject(harness); storeA.pages = fivePages()
        _t1ShellA = makeShell({
            "seriesId": "t1", "seriesTitle": "Race", "seriesCover": "file:///c.png",
            "progress": sharedProg, "pageStore": storeA
        })
        _t1ShellA.setLayout("long_strip")
        ck(_t1ShellA.currentPage === 1, "T1 setup: shellA opens fresh at page 1, got " + _t1ShellA.currentPage)
        _t1ShellA._onPresented(4, 0)   // the user actually reached page 4
        t1Timer1.restart()
    }
    Timer { id: t1Timer1; interval: 60; repeat: false; onTriggered: harness.step3_t1CheckSetupThenRace() }

    function step3_t1CheckSetupThenRace() {
        var recA = sharedProg.get("manga", "t1")
        ck(recA !== null && recA.resume && recA.resume.page === 4,
           "T1 setup: shellA's real read of page 4 must be recorded, got " + JSON.stringify(recA))
        _t1ShellA.destroy()

        var storeB = storeComp.createObject(harness); storeB.pages = fivePages()
        _t1ShellB = makeShell({
            "seriesId": "t1", "seriesTitle": "Race", "seriesCover": "file:///c.png",
            "progress": sharedProg, "pageStore": storeB
        })
        _t1ShellB.setLayout("long_strip")
        ck(_t1ShellB.currentPage === 4,
           "T1: shellB must resume the number correctly from Progress, got " + _t1ShellB.currentPage)
        ck(_t1ShellB._stripRestorePending === true,
           "T1 setup: the restore door must be armed at this point (test invalid otherwise)")

        // THE RACE, reproduced as directly as the shell's own seam allows: the real strip
        // surface's onActiveChanged fires Qt.callLater(_emitPresented) the instant it mounts.
        _t1ShellB._onPresented(1, 0)

        t1Timer2.restart()
    }
    Timer { id: t1Timer2; interval: 60; repeat: false; onTriggered: harness.step4_t1CheckNotOverwritten() }

    function step4_t1CheckNotOverwritten() {
        var recB = sharedProg.get("manga", "t1")
        ck(recB !== null && recB.resume && recB.resume.page === 4,
           "T1: a mount-time page-1 report during a pending restore must NOT overwrite the record, "
           + "got " + JSON.stringify(recB))
        _t1ShellB.destroy()
        step5_t2ManualActivityDisarms()
    }

    // ===== T2: manualActivity() disarms an in-flight restore =====
    function step5_t2ManualActivityDisarms() {
        var prog = progComp.createObject(harness)
        prog.entries["manga|t2"] = { resume: { chapterId: "ch1", page: 4, scrollFrac: 0, maxSeen: 4 } }
        var store = storeComp.createObject(harness); store.pages = fivePages()
        var shell = makeShell({
            "seriesId": "t2", "seriesTitle": "Manual", "seriesCover": "file:///c.png",
            "progress": prog, "pageStore": store
        })
        shell.setLayout("long_strip")
        ck(shell._stripRestorePending === true, "T2 setup: the restore door must be armed")
        shell.manualActivity()
        ck(shell._stripRestorePending === false,
           "T2: manualActivity() must disarm a pending restore door")
        ck(shell._pendingStripFrac === 0 && shell._pendingPageFraction === -1,
           "T2: manualActivity() must clear both pending fraction arms, got frac="
           + shell._pendingStripFrac + " pageFraction=" + shell._pendingPageFraction)
        shell.destroy()
        step6_t3GiveUpClearsArms()
    }

    // ===== T3: _runStripRestore's give-up branch must clear BOTH pending arms, not just the =====
    // ===== try counter (offscreen span is always <= 0, so this drives the give-up path      =====
    // ===== directly and deterministically — no real ListView layout needed).                =====
    function step6_t3GiveUpClearsArms() {
        var prog = progComp.createObject(harness)
        prog.entries["manga|t3"] = {
            resume: { chapterId: "ch1", page: 4, scrollFrac: 0, pageFraction: 0.6, maxSeen: 4 }
        }
        var store = storeComp.createObject(harness); store.pages = fivePages()
        _t3Shell = makeShell({
            "seriesId": "t3", "seriesTitle": "GiveUp", "seriesCover": "file:///c.png",
            "progress": prog, "pageStore": store
        })
        _t3Shell.setLayout("long_strip")
        ck(_t3Shell._pendingPageFraction >= 0,
           "T3 setup: the within-page fraction must be armed, got " + _t3Shell._pendingPageFraction)
        // Drive the door's own retries directly (named exactly so a harness can do this — see the
        // function's own header comment). Offscreen, stripSurface.contentHeight never lays out, so
        // every call takes the span<=0 branch: 3 retries, then give-up on the 4th.
        _t3Shell._runStripRestore()
        _t3Shell._runStripRestore()
        _t3Shell._runStripRestore()
        _t3Shell._runStripRestore()
        ck(_t3Shell._stripRestoreTries === 0, "T3: the door must have given up (tries reset to 0)")
        ck(_t3Shell._pendingStripFrac === 0 && _t3Shell._pendingPageFraction === -1,
           "T3: giving up must clear BOTH pending arms so a later mode switch cannot inherit a "
           + "stale one, got frac=" + _t3Shell._pendingStripFrac
           + " pageFraction=" + _t3Shell._pendingPageFraction)
        _t3Shell.destroy()
        step7_t4MinimizeFlushes()
    }

    // ===== T4: goMinimize() flushes Progress synchronously before emitting minimizeRequested() =====
    property int _t4MinimizeSignalCount: 0
    function step7_t4MinimizeFlushes() {
        var prog = progComp.createObject(harness)
        var store = storeComp.createObject(harness); store.pages = fivePages()
        var shell = makeShell({
            "seriesId": "t4", "seriesTitle": "Flush", "seriesCover": "file:///c.png",
            "progress": prog, "pageStore": store
        })
        shell.setLayout("single_page")   // isolate from the strip-restore path entirely
        shell._onPresented(3, 0)
        // No debounce wait — goMinimize() must flush IMMEDIATELY, not ride the timer.
        shell.minimizeRequested.connect(function () { harness._t4MinimizeSignalCount += 1 })
        shell.goMinimize()
        var rec = prog.get("manga", "t4")
        ck(rec !== null && rec.resume && rec.resume.page === 3,
           "T4: goMinimize() must flush the presented page synchronously, got " + JSON.stringify(rec))
        ck(harness._t4MinimizeSignalCount === 1,
           "T4: goMinimize() must still emit minimizeRequested() exactly once, got "
           + harness._t4MinimizeSignalCount)
        shell.destroy()
        report()
    }

    function report() {
        if (failures.length === 0) { console.log("COMICREADER_RESUME_RACE_OK"); Qt.exit(0) }
        else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_RESUME_RACE_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    Component.onCompleted: Qt.callLater(harness.step1_loadComponent)
}
