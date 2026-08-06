// Comic Reader resume-race regressions as real Qt Quick Test cases (Qt Test
// arc, slice 5). Converted from tests/comicreader_resume_race_harness.qml —
// same four pinned fixes (T1–T4, see that file's header for the confirmed root
// cause), with the timer-chained orchestration replaced by independent test_*
// functions, tryVerify on the debounced write instead of fixed 60 ms timers,
// SignalSpy instead of a hand-rolled counter, and createTemporaryObject so a
// failing case cannot leak state into the next. The legacy harness (and its
// gate test_comicreader_resume_race_p0.ps1) stays until parity review.
//
// The fakes are byte-equivalent in behavior to the legacy harness's: the KEYED
// SharedProgress exists because the shared FakeProgress in the shell suite
// returns preset values from get() regardless of record(), which cannot see
// the write→destroy→recreate→read loop this bug lives in.
import QtQuick
import QtTest 1.3

TestCase {
    id: testCase
    name: "ComicReaderResumeRace"

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

    Component { id: progComp; SharedProgress {} }
    Component { id: storeComp; FakePageStore {} }
    // A writable stand-in for the shell's seriesRecords Settings ("needs a
    // writable `all` JSON string" — the shell's own property comment).
    component SeriesRecords: QtObject { property string all: "{}" }
    Component { id: recordsComp; SeriesRecords {} }
    property var shellComp: null

    function fivePages() {
        var out = []
        for (var i = 0; i < 5; i++) out.push({ index: i, url: "file:///f/p" + i + ".png", group: -1 })
        return out
    }

    function makeShell(cfg) {
        var full = Object.assign({
            "width": 640, "height": 480, "recordDebounceMs": 20,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        }, cfg)
        var inst = createTemporaryObject(shellComp, testCase, full)
        verify(inst !== null, "shell createTemporaryObject must succeed")
        return inst
    }

    function makeStore() {
        var s = createTemporaryObject(storeComp, testCase)
        s.pages = fivePages()
        return s
    }

    function initTestCase() {
        shellComp = Qt.createComponent("../../qml/comicreader/ComicReaderShell.qml")
        verify(shellComp.status !== Component.Error,
               "shell component must load: " + shellComp.errorString())
    }

    // T1 — the resume race: a mount-time page-1 report during a pending restore
    // must not overwrite a real resume (one bad write poisons every later open).
    function test_mount_time_page_one_cannot_overwrite_restore() {
        var prog = createTemporaryObject(progComp, testCase)

        var shellA = makeShell({ "seriesId": "t1", "seriesTitle": "Race",
                                 "seriesCover": "file:///c.png",
                                 "progress": prog, "pageStore": makeStore() })
        shellA.setLayout("long_strip")
        compare(shellA.currentPage, 1, "shellA opens fresh at page 1")
        shellA._onPresented(4, 0)   // the user actually reached page 4
        // The record rides a 20 ms debounce — wait on the WRITE, not on a timer.
        tryVerify(function () {
            var r = prog.get("manga", "t1")
            return r !== null && r.resume && r.resume.page === 4
        }, 2000, "shellA's real read of page 4 must be recorded")
        shellA.destroy()

        var shellB = makeShell({ "seriesId": "t1", "seriesTitle": "Race",
                                 "seriesCover": "file:///c.png",
                                 "progress": prog, "pageStore": makeStore() })
        shellB.setLayout("long_strip")
        compare(shellB.currentPage, 4, "shellB must resume page 4 from Progress")
        verify(shellB._stripRestorePending === true,
               "restore door must be armed (test invalid otherwise)")

        // THE RACE: the real strip surface fires a mount-time presented(1).
        shellB._onPresented(1, 0)
        // The assertion is a NON-event: the wrong write must never land. A
        // non-event has no completion signal to wait on, so the only honest
        // check is a bounded observation window comfortably past the 20 ms
        // debounce (10x), with the event loop pumping. This is an observation
        // window for absence, not a sleep standing in for a signal.
        wait(200)
        var rec = prog.get("manga", "t1")
        verify(rec !== null && rec.resume && rec.resume.page === 4,
               "mount-time page-1 report during pending restore must NOT overwrite: "
               + JSON.stringify(rec))
    }

    // T2 — manualActivity() disarms an in-flight restore.
    function test_manual_activity_disarms_pending_restore() {
        var prog = createTemporaryObject(progComp, testCase)
        prog.entries["manga|t2"] = { resume: { chapterId: "ch1", page: 4, scrollFrac: 0, maxSeen: 4 } }
        var shell = makeShell({ "seriesId": "t2", "seriesTitle": "Manual",
                                "seriesCover": "file:///c.png",
                                "progress": prog, "pageStore": makeStore() })
        shell.setLayout("long_strip")
        verify(shell._stripRestorePending === true, "restore door must be armed")
        shell.manualActivity()
        verify(shell._stripRestorePending === false,
               "manualActivity() must disarm a pending restore door")
        compare(shell._pendingStripFrac, 0, "strip fraction arm cleared")
        compare(shell._pendingPageFraction, -1, "page fraction arm cleared")
    }

    // T3 — the give-up branch clears BOTH pending arms, not just the try
    // counter (offscreen span is always <= 0, driving give-up deterministically).
    function test_give_up_clears_pending_arms() {
        var prog = createTemporaryObject(progComp, testCase)
        prog.entries["manga|t3"] = {
            resume: { chapterId: "ch1", page: 4, scrollFrac: 0, pageFraction: 0.6, maxSeen: 4 }
        }
        // The within-page fraction arms only when the shell OPENS in long_strip
        // (_applyResume gates on the open-time mode; a later setLayout re-arms
        // the door but the fraction is already discarded — that is the product
        // contract, not a bug). The legacy harness got long_strip-at-open from
        // its runner's ambient prefs; here the SERIES RECORD — the top layer of
        // the pref resolution — says it deterministically: this reader chose
        // Long Strip for this series. `seriesRecords` is the shell's own
        // injectable seam (`.all` is a JSON string keyed by seriesId).
        var records = createTemporaryObject(recordsComp, testCase,
            { "all": JSON.stringify({ "t3": { "layout": "long_strip" } }) })

        var shell = makeShell({ "seriesId": "t3", "seriesTitle": "GiveUp",
                                "seriesCover": "file:///c.png",
                                "progress": prog, "pageStore": makeStore(),
                                "seriesRecords": records })
        verify(shell._pendingPageFraction >= 0, "within-page fraction must be armed")
        shell._runStripRestore()
        shell._runStripRestore()
        shell._runStripRestore()
        shell._runStripRestore()
        compare(shell._stripRestoreTries, 0, "the door must have given up")
        compare(shell._pendingStripFrac, 0, "give-up clears the strip arm")
        compare(shell._pendingPageFraction, -1, "give-up clears the page arm")
    }

    // T4 — goMinimize() flushes Progress synchronously BEFORE emitting
    // minimizeRequested(), and emits it exactly once.
    function test_minimize_flushes_before_emitting() {
        var prog = createTemporaryObject(progComp, testCase)
        var shell = makeShell({ "seriesId": "t4", "seriesTitle": "Flush",
                                "seriesCover": "file:///c.png",
                                "progress": prog, "pageStore": makeStore() })
        shell.setLayout("single_page")   // isolate from the strip-restore path
        shell._onPresented(3, 0)

        var spy = createTemporaryObject(spyComp, testCase, { "target": shell,
                                                             "signalName": "minimizeRequested" })
        verify(spy.valid, "minimizeRequested spy must attach")
        shell.goMinimize()
        // Synchronous contract: by the time goMinimize() returns, the record is
        // flushed and the signal fired — no debounce wait allowed.
        var rec = prog.get("manga", "t4")
        verify(rec !== null && rec.resume && rec.resume.page === 3,
               "goMinimize() must flush the presented page synchronously: " + JSON.stringify(rec))
        compare(spy.count, 1, "minimizeRequested emitted exactly once")
    }

    Component { id: spyComp; SignalSpy {} }
}
