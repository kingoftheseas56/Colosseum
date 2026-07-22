// Offscreen logic harness for Guided as the FIFTH MangaReader style (Task 10).
//
// It loads the REAL MangaReader with an injected fake page store + fake GuidedAnalysis
// service, and the native camera controller is supplied by a QML mock on the -I import
// path (tests/qmlmock) so this runs under plain qml.exe with no build. The real controller
// is proven by tests/guided_camera_controller_harness.cpp (Task 8); here we prove the READER
// wires Guided correctly:
//   * entering Guided builds one canvas per read-unit (cover single; consecutive pages pair
//     into ONE wide spread canvas), covers every page exactly once, and opens analysis;
//   * a spread canvas keeps BOTH original pages and reverses physical order for RTL;
//   * entering lands on the canvas holding the current page; exiting restores the prior
//     style and the reading page;
//   * the controller receives a path (Panel Step / Auto Read have something to drive).
//
// A thrown error HANGS qml.exe offscreen, so every check is wrapped in try/catch -> Qt.exit(1).

import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

    component FakeGuidedAnalysis: QtObject {
        property var lastOpen: null
        property int lastVisibleCanvas: -1
        property bool closed: false
        property var paths: ({})                     // canvasIndex -> path map (empty -> reader falls back)
        signal jobChanged(string entryId)
        signal canvasChanged(string entryId, int canvasIndex)
        function openEntry(id, canvases, rtl) { lastOpen = { id: id, canvases: canvases, rtl: rtl } }
        function closeEntry(id) { closed = true }
        function setVisibleCanvas(id, index) { lastVisibleCanvas = index }
        function pathForCanvas(id, index) { return paths[index] !== undefined ? paths[index] : null }
        function jobSummary(id) { return { stage: "planning", ready: 1, total: 3, paused: false } }
        function activeJobs() { return [] }
        function canvasDetails(id) { return [] }
        function pauseJob(id) {}
        function resumeJob(id) {}
        function retryCanvas(id, index) {}
        function useWholePage(id, index) {}
        function useDetectedPanels(id, index) {}
        function reverseOrder(id, index) {}
    }

    component FakePageStore: QtObject {
        property var pages: []
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
        function localPages(cid) { return pages }
        function statusOf(cid) { return { state: pages.length ? "ready" : "none", done: 0, total: 0 } }
        function downloadChapter(cid, sid, title, label) {}
        function downloadIssue(cid, url, sid, title, label, bytes) {}
    }

    FakeGuidedAnalysis { id: svc }
    FakePageStore {
        id: pageStore
        pages: [
            { index: 0, url: "file:///f/p0.png", group: 0 },
            { index: 1, url: "file:///f/p1.png", group: 0 },
            { index: 2, url: "file:///f/p2.png", group: 0 },
            { index: 3, url: "file:///f/p3.png", group: 0 },
            { index: 4, url: "file:///f/p4.png", group: 0 }
        ]
    }

    property var rdr: null

    function ck(cond, msg) { if (!cond) throw new Error(msg) }
    function eqArr(a, b) {
        if (!a || !b || a.length !== b.length) return false
        for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false
        return true
    }

    function setup() {
        var rc = Qt.createComponent("../qml/MangaReader.qml")
        if (rc.status === Component.Error) throw new Error("reader component: " + rc.errorString())
        harness.rdr = rc.createObject(harness, {
            "width": 640, "height": 480, "seriesId": "guidedtest", "seriesTitle": "Guided Test",
            "pageStore": pageStore, "entryKind": "manga",
            "guidedService": svc,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        })
        if (!harness.rdr) throw new Error("reader createObject returned null")
    }

    function runChecks() {
        try {
            var r = harness.rdr
            // clean, deterministic start regardless of any app-wide persisted style
            r.styleOv = "long_strip"
            r.setDirection("right_left")   // RTL manga default
            ck(r.max === 5, "reader must load 5 pages, got " + r.max)
            ck(!r.guided, "must start outside Guided")

            // --- enter Guided from page 4 (0-based index 3 -> canvas [3,4]) ---
            r.page = 4
            r.setStyle("guided")
            ck(r.guided, "setStyle('guided') must switch to the Guided style")
            ck(r.style === "guided", "style must read 'guided'")
            ck(r.preGuidedStyle === "long_strip", "entering Guided must remember the prior style")

            // --- canvas model: cover single, then two pairs; every page once ---
            var cv = r.guidedCanvases
            ck(cv.length === 3, "5 pages -> 3 canvases (cover + two pairs), got " + cv.length)
            ck(cv[0].files.length === 1, "the cover is one single-page canvas")
            ck(cv[1].files.length === 2 && cv[1].kind === "spread", "pages 2-3 form ONE wide spread canvas")
            ck(cv[2].files.length === 2 && cv[2].kind === "spread", "pages 4-5 form ONE wide spread canvas")
            // coverage: exactly pages 0..4 across all canvases, no dup
            var seen = {}
            var count = 0
            for (var i = 0; i < cv.length; i++)
                for (var j = 0; j < cv[i].readingPageIndices.length; j++) {
                    var idx = cv[i].readingPageIndices[j]
                    ck(seen[idx] === undefined, "page " + idx + " must appear in exactly one canvas")
                    seen[idx] = true; count++
                }
            ck(count === 5, "every source page must be covered exactly once, got " + count)

            // --- RTL reverses PHYSICAL order but keeps READING order ---
            ck(eqArr(cv[1].readingPageIndices, [1, 2]), "reading order stays low->high")
            ck(eqArr(cv[1].pageIndices, [2, 1]), "RTL lays the spread right page first (physical reversed)")

            // --- entering landed on the canvas holding the page we were on (index 3 -> canvas 2) ---
            ck(r.guidedCanvasIndex === 2, "entering Guided lands on the canvas holding the current page, got " + r.guidedCanvasIndex)

            // --- analysis opened with the built canvases + direction ---
            ck(svc.lastOpen !== null, "entering Guided must open analysis")
            ck(String(svc.lastOpen.id) === "ch1", "analysis opens for the current entry")
            ck(svc.lastOpen.rtl === true, "analysis is told the reading direction")
            ck(svc.lastOpen.canvases.length === 3, "analysis receives the built canvas model")
            ck(svc.lastVisibleCanvas === 2, "the visible canvas is published to analysis")

            // --- exit restores the prior style AND the reading page ---
            r.exitGuided()
            ck(!r.guided && r.style === "long_strip", "exiting Guided restores the prior style")
            ck(svc.closed === true, "exiting Guided closes analysis")
            ck(r.page === 4, "exiting Guided restores the reading page, got " + r.page)

            // controller wiring needs the Loader to have resolved the mock — check next tick
            Qt.callLater(runControllerChecks)
        } catch (e) {
            console.log("GUIDED_MANGA_READER_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    function runControllerChecks() {
        try {
            var r = harness.rdr
            r.setStyle("guided")
            var cam = r.guidedCamera
            ck(cam !== null && cam !== undefined, "the native camera controller (mock) must load via the Loader")
            // activateGuidedCanvas handed the controller a path -> it opens paused on step 0
            ck(cam.stepIndex === 0, "the controller opens on the first step")
            ck(cam.autoRead === false, "Guided opens PAUSED, never auto-starting")
            var cr = cam.cameraRect
            ck(cr.width > 0 && cr.height > 0, "the controller has a real camera rect (whole-page fallback)")

            console.log("GUIDED_MANGA_READER_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("GUIDED_MANGA_READER_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Component.onCompleted: {
        try { setup(); Qt.callLater(runChecks) }
        catch (e) { console.log("GUIDED_MANGA_READER_FAIL setup: " + e.message); Qt.exit(1) }
    }

    Timer { interval: 8000; running: true
        onTriggered: { console.log("GUIDED_MANGA_READER_FAIL timeout"); Qt.exit(1) } }
}
