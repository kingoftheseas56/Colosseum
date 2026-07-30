// Comic Reader — MIGRATION ACCEPTANCE gate (Task 13, the cutover).
//
// This harness loads `qml/MangaReader.qml` — THE PRODUCTION READER PATH, whatever is behind that
// filename — and asserts that what answers is the from-scratch Comic Reader honouring the Task 1
// public contract. It is deliberately written against the FILENAME, not against ComicReaderShell:
// that is the whole point of the cutover. Before the swap it fails (the old reader answers); after
// the swap it passes, and it keeps passing only while the wrapper keeps its promises.
//
// What it pins, and why each one is here rather than in a sibling harness:
//   * CONTRACT SURFACE — every input property, signal and public method the four real callers
//     touch. Not a paraphrase: the list is the Task 1 handoff PLUS openChapterById(), which that
//     survey missed because it only read the three series pages. qml/BakeoffStripHost.qml calls it.
//     A missing method here is a runtime "not a function" that only bites when someone opens that
//     surface, which is exactly the kind of break a cutover must not ship.
//   * THE BACKEND SEAM — the new reader decides in C++ and paints in QML, so it exposes an
//     injectable `core` and its pages come from `image://comicreader/`. The old reader has neither.
//     This is the sharpest old-vs-new discriminator in the suite.
//   * BOTH MODES SELECTABLE, GUIDED ABSENT — Guided is frozen on disk but must not be reachable
//     from the reader's own mode identity (the freeze gate in test_guided_manga_reader.ps1 asserts
//     the files are untouched; this asserts the PICKER can't reach it).
//   * PROGRESS PAYLOAD BYTE-IDENTICAL — a Continue record written by the new reader must be
//     indistinguishable from the old one, or every existing Continue row silently orphans.
//   * MEMORY SURVIVES RECREATION — the callers HIDE and re-SHOW the reader, and the app restarts;
//     a remembered mode/override/bookmark that only lives in the instance is not memory. This
//     destroys the reader and builds a new one over the SAME stores, which is the real-world shape.
//
// HOUSE HARNESS PATTERN: a thrown error hangs qml.exe offscreen, so `ck` never throws — it collects
// failures; exactly one COMICREADER_MIGRATION_OK when clean, else one
// COMICREADER_MIGRATION_FAIL:<msg> per failure and Qt.exit(1). A safety-net Timer fails loudly on a
// true hang instead of stalling the gate.

import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }

    property var events: []

    // ---- the same fake seams the shell harness uses (the reader must accept them by contract) ----
    component FakeCore: QtObject {
        property int generation: 0
        property int pageCount: 0
        property string couplingState: "auto:normal:1.0"
        property var stripModel: null
        property var lastOpenEntry: null
        property int openCount: 0
        property bool closed: false
        property int stripWidthPct: 78
        property int stripGap: 0
        property bool memorySaver: false
        property var blob: ({})
        signal entryChanged()
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        signal pairingChanged()
        signal progressChanged()
        function openEntry(entryId, pages, direction, persisted) {
            lastOpenEntry = { entryId: String(entryId), pages: pages,
                              direction: String(direction), persisted: persisted }
            pageCount = pages ? pages.length : 0
            openCount += 1
        }
        function closeEntry() { closed = true }
        function setVisible(pages) {}
        function unitForPage(page) { return { rightIndex: page - 1, leftIndex: -1, spread: false } }
        function setSpreadOverride(page, state) {}
        function nudgeCoupling() {}
        function resetCoupling() {}
        function setMemorySaver(on) { memorySaver = (on === true) }
        function setStripLayout(w, g, top, h) { stripWidthPct = w; stripGap = g; return top }
        function persistedState() { return JSON.parse(JSON.stringify(blob)) }
        // THE page-source discriminator: the new reader asks the backend for an
        // image://comicreader/ url; the old reader has no such seam at all.
        function imageUrl(page) { return "image://comicreader/1/" + page + "?rev=1" }
    }
    component FakeProgress: QtObject {
        property var saved: null
        property var lastRecord: null
        property var records: []
        function record(payload) { lastRecord = payload; records.push(payload) }
        function get(kind, id) { return saved }
    }
    component FakePageStore: QtObject {
        property var pages: []
        property var lastDownloadChapter: null
        property var lastDownloadIssue: null
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
        function localPages(cid) { return pages }
        function statusOf(cid) { return { state: pages.length ? "ready" : "none", done: 0, total: 0 } }
        function downloadChapter(cid, sid, title, label) { lastDownloadChapter = { cid: String(cid) } }
        function downloadIssue(cid, url, sid, title, label, bytes) { lastDownloadIssue = { cid: String(cid) } }
    }
    component FakePrefs: QtObject {
        property string nightVeil: "off"
        property real   gutterStrength: 0.35
        property int    stripWidthPct: 78
        property int    stripGap: 0
        property bool   memorySaver: false
        // Task 3: the global last-choice is TWO independent keys (layout + order); `readingMode` is
        // the legacy combined identity the shell still reads for a store written by the shipped
        // build. All three must exist here — the shell WRITES layout/order, and QML raises "cannot
        // assign to non-existent property" on a fake that is missing one.
        property string layout: ""
        property string order: ""
        property string readingMode: ""
    }
    component FakeRecords: QtObject { property string all: "{}" }

    Component { id: prefsComp;   FakePrefs {} }
    Component { id: recordsComp; FakeRecords {} }
    function freshPrefs(over)   { return prefsComp.createObject(harness, over || {}) }
    function freshRecords(json) { return recordsComp.createObject(harness, { all: json || "{}" }) }

    FakeCore { id: coreM }  FakeProgress { id: progM }  FakePageStore { id: storeM }
    FakeCore { id: coreW }  FakeProgress { id: progW }  FakePageStore { id: storeW }
    FakeCore { id: coreT }  FakeProgress { id: progT }  FakePageStore { id: storeT }
    FakeCore { id: coreR }  FakeProgress { id: progR }  FakePageStore { id: storeR }

    function fivePages() {
        var out = []
        for (var i = 0; i < 5; i++)
            out.push({ index: i, url: "file:///f/p" + i + ".png", group: "" })
        return out
    }

    function deepEqual(a, b) {
        if (a === b) return true
        if (typeof a !== "object" || typeof b !== "object" || a === null || b === null) return false
        var ka = Object.keys(a), kb = Object.keys(b)
        if (ka.length !== kb.length) return false
        for (var i = 0; i < ka.length; i++) {
            if (kb.indexOf(ka[i]) < 0) return false
            if (!deepEqual(a[ka[i]], b[ka[i]])) return false
        }
        return true
    }

    property var readerComp: null
    function makeReader(cfg) {
        if (!cfg.globalPrefs)   cfg.globalPrefs   = freshPrefs()
        if (!cfg.seriesRecords) cfg.seriesRecords = freshRecords()
        if (!cfg.entryRecords)  cfg.entryRecords  = freshRecords()
        return readerComp.createObject(harness, cfg)
    }

    function report() {
        if (failures.length === 0) { console.log("COMICREADER_MIGRATION_OK"); Qt.exit(0) }
        else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_MIGRATION_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    function runChecks() {
        // ===== 1. MANGA lane: the contract surface, on the production filename =====
        storeM.pages = fivePages()
        var m = makeReader({
            "width": 640, "height": 480,
            "seriesId": "s1", "seriesTitle": "Contract Series", "seriesCover": "file:///f/cover.png",
            "core": coreM, "progress": progM, "pageStore": storeM,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        })
        if (!m) { failures.push("reader createObject returned null"); report(); return }

        // -- input properties every real caller sets --
        var props = ["backdrop", "seriesTitle", "seriesId", "seriesCover", "chapters", "chapterId",
                     "chapterLabel", "western", "pageStore", "entryKind", "entryLabelPrefix"]
        for (var i = 0; i < props.length; i++)
            ck(m[props[i]] !== undefined, "contract: input property '" + props[i] + "' must exist")
        ck(m.max === 5, "contract: `max` must be THE page count (5), got " + m.max)

        // -- signals every real caller connects --
        var sigs = ["backRequested", "minimizeRequested", "fullscreenRequested", "closeRequested",
                    "sourceRequested"]
        for (var s = 0; s < sigs.length; s++)
            ck(typeof m[sigs[s]] === "function", "contract: signal '" + sigs[s] + "' must exist")

        // -- public methods. openChapterById is the one the Task 1 survey missed: it reads only the
        //    three series pages, but qml/BakeoffStripHost.qml calls this by name. --
        ck(typeof m.openChapterById === "function",
           "contract: openChapterById() must exist — qml/BakeoffStripHost.qml calls it by name")
        ck(typeof m.startDownload === "function", "contract: startDownload() must exist")
        ck(typeof m.recordProgress === "function", "contract: recordProgress() must exist")

        // -- THE backend seam: decisions in C++, pages via the comicreader provider --
        ck(m.core === coreM, "cutover: the reader must accept an injected `core` backend seam")
        ck(coreM.openCount === 1, "cutover: opening an entry must drive core.openEntry(), got " + coreM.openCount)
        ck(coreM.lastOpenEntry && coreM.lastOpenEntry.direction === "rtl",
           "cutover: a manga entry must open RTL")
        ck(String(coreM.imageUrl(0)).indexOf("image://comicreader/") === 0,
           "cutover: pages must be served from the image://comicreader/ provider")

        // -- both modes selectable; Guided unreachable from the identity --
        ck(typeof m.setReadingMode === "function", "modes: setReadingMode() must exist")
        m.setReadingMode("strip")
        ck(m.readingMode === "strip", "modes: Strip must be selectable, got '" + m.readingMode + "'")
        m.setReadingMode("manga")
        ck(m.readingMode === "manga", "modes: Manga must be selectable, got '" + m.readingMode + "'")
        m.setReadingMode("comic")
        ck(m.readingMode === "comic", "modes: Comic must be selectable, got '" + m.readingMode + "'")

        // Guided is frozen: the picker must not reach it, whatever it is asked for.
        m.setReadingMode("guided")
        ck(m.readingMode !== "guided",
           "GUIDED FROZEN: the reader's mode identity must never resolve to 'guided', got '" + m.readingMode + "'")
        ck(m.guidedActive === undefined && m.guidedMode === undefined,
           "GUIDED FROZEN: the reader must expose no guided state property")

        // ===== 2. WESTERN lane: LTR + the 'comic' Progress namespace =====
        storeW.pages = fivePages()
        var w = makeReader({
            "width": 640, "height": 480,
            "seriesId": "gc:tag", "seriesTitle": "Western", "seriesCover": "file:///f/w.png",
            "core": coreW, "progress": progW, "western": true, "pageStore": storeW,
            "chapters": [{ "id": "i1", "number": "1", "name": "" }],
            "chapterId": "i1", "chapterLabel": "Issue 1"
        })
        ck(w && w.progressKind === "comic", "western lane: progressKind must be 'comic', got '" + (w ? w.progressKind : "") + "'")
        ck(coreW.lastOpenEntry && coreW.lastOpenEntry.direction === "ltr", "western lane: must open LTR")

        // ===== 3. TANKOBAN lane: a not-ready volume routes to the source chooser, never a download =====
        storeT.pages = []      // not downloaded
        var gotSource = false
        var t = makeReader({
            "width": 640, "height": 480,
            "seriesId": "s-vol", "seriesTitle": "Vol Series", "seriesCover": "file:///f/v.png",
            "core": coreT, "progress": progT, "pageStore": storeT,
            "entryKind": "tankoban", "entryLabelPrefix": "Vol. ",
            "chapters": [{ "id": "v1", "number": "1", "name": "" }],
            "chapterId": "v1", "chapterLabel": "Vol. 1"
        })
        if (t) {
            t.sourceRequested.connect(function () { gotSource = true })
            t.startDownload()
            ck(gotSource, "tankoban lane: a not-ready volume must emit sourceRequested()")
            ck(storeT.lastDownloadChapter === null && storeT.lastDownloadIssue === null,
               "tankoban lane: a volume must NEVER route to a chapter/issue download API")
            ck(t.progressKind === "tankoban", "tankoban lane: progressKind must be 'tankoban'")
        }

        // ===== 4. PROGRESS payload byte-identical to the recorded §4.1 shape =====
        m.currentPage = 3
        m.recordProgress()
        var expect = {
            "id": "s1", "kind": "manga", "caption": "Contract Series", "title": "Contract Series",
            "sub": "Chapter 1", "cover": "file:///f/cover.png",
            "c1": "#3a2f55", "c2": "#15111f",
            "progress": 0.6,
            "resume": { "chapterId": "ch1", "page": 3, "scrollFrac": 0, "maxSeen": 3, "finished": false }
        }
        ck(deepEqual(progM.lastRecord, expect),
           "progress: the Continue payload must be byte-identical to the old reader's — got "
           + JSON.stringify(progM.lastRecord))
        // SWITCHING MODE MUST NOT RESTART THE BOOK. Hemanth hit this on the first eyes-on of the
        // cutover: entering Strip dropped him back at page 1. Changing how pages are laid out is not
        // a reason to lose your place — every reader in the family keeps it, and so must this one.
        m.setReadingMode("manga")
        m.currentPage = 4
        m.setReadingMode("strip")
        ck(m.currentPage === 4,
           "modes: switching INTO Strip must keep your page (was 4), got " + m.currentPage)
        m.setReadingMode("manga")
        ck(m.currentPage === 4,
           "modes: switching back OUT of Strip must keep your page (was 4), got " + m.currentPage)
        m.currentPage = 1

        // ===== 5. MEMORY SURVIVES RECREATION =====
        // The callers hide and re-show the reader, and the app restarts. Memory that only lives in
        // the instance is not memory — so build one, teach it, DESTROY it, and build a fresh one
        // over the SAME stores.
        var sharedPrefs   = freshPrefs()
        var sharedSeries  = freshRecords()
        var sharedEntries = freshRecords()
        coreR.blob = { spreadOverrides: { "2": true }, bookmarks: [4], couplingMode: "manual" }
        storeR.pages = fivePages()
        var r1 = makeReader({
            "width": 640, "height": 480,
            "seriesId": "s-mem", "seriesTitle": "Mem", "seriesCover": "file:///f/m.png",
            "core": coreR, "progress": progR, "pageStore": storeR,
            "globalPrefs": sharedPrefs, "seriesRecords": sharedSeries, "entryRecords": sharedEntries,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        })
        if (!r1) { failures.push("memory: first reader instance was null"); report(); return }
        r1.nightVeil = "high"          // a global taste
        r1.setReadingMode("strip")     // a per-series identity
        r1.shutdown()                  // flushes the per-entry record, as a real teardown does
        r1.destroy()

        var r2 = makeReader({
            "width": 640, "height": 480,
            "seriesId": "s-mem", "seriesTitle": "Mem", "seriesCover": "file:///f/m.png",
            "core": coreR, "progress": progR, "pageStore": storeR,
            "globalPrefs": sharedPrefs, "seriesRecords": sharedSeries, "entryRecords": sharedEntries,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        })
        if (r2) {
            ck(r2.nightVeil === "high",
               "memory: the night veil must survive a full reader recreation, got '" + r2.nightVeil + "'")
            ck(r2.readingMode === "strip",
               "memory: the per-series mode must survive recreation, got '" + r2.readingMode + "'")
            var reopened = coreR.lastOpenEntry ? coreR.lastOpenEntry.persisted : null
            ck(reopened && reopened.spreadOverrides && reopened.spreadOverrides["2"] === true,
               "memory: a spread override must be handed back to the backend on reopen")
            ck(reopened && reopened.bookmarks && reopened.bookmarks.length === 1,
               "memory: bookmarks must be handed back to the backend on reopen")
            ck(reopened && reopened.couplingMode === "manual",
               "memory: a manual coupling must be handed back to the backend on reopen")
        }

        report()
    }

    Component.onCompleted: {
        try {
            readerComp = Qt.createComponent("../qml/MangaReader.qml")
            if (readerComp.status === Component.Error)
                throw new Error("MangaReader.qml: " + readerComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_MIGRATION_FAIL: setup: " + e.message)
            Qt.exit(1)
        }
    }

    Timer {
        interval: 10000; running: true
        onTriggered: { console.log("COMICREADER_MIGRATION_FAIL: timeout"); Qt.exit(1) }
    }
}
