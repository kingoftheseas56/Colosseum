// Comic Reader — ORCHESTRATION SHELL oracle (Task 9).
//
// Instantiates qml/comicreader/ComicReaderShell.qml offscreen with INJECTED FAKE seams — a fake
// backend core (the ComicReaderCore API), a fake Progress sink, and a fake page store — for all
// three lanes (manga chapter / western comic / Tankoban volume) and asserts the orchestration the
// shell owns:
//   * ready manga entry -> core.openEntry(entryId, localPages, direction="rtl", persisted);
//     ready western -> direction="ltr" (ComicReaderState smart default per lane).
//   * an unavailable manga chapter routes startDownload() -> store.downloadChapter(...);
//     an unavailable western issue -> store.downloadIssue(...);
//     an unavailable Tankoban volume -> sourceRequested(entryId) (never a chapter-download API).
//   * RESUME: given Progress.get returns a saved {resume:{page,scrollFrac,maxSeen}}, the shell
//     applies currentPage + stripFraction + maxSeen BEFORE first paint (checked at Qt.callLater,
//     i.e. after Component.onCompleted's load() ran and before any real frame).
//   * PROGRESS: a per-page-change record is DEBOUNCED (NOT immediate — Task 10's strip scroll would
//     storm QSettings otherwise); after the debounce interval it fires with the byte-identical
//     Task 1 §4.1 payload. Crossing + close/shutdown record IMMEDIATELY. Nothing records when
//     max <= 0 (the MangaReader.qml:211 guard).
//   * CROSSING: next/previous over a newest-first `chapters` array opens the right adjacent entry
//     and RECORDS progress for the outgoing entry BEFORE jumping (asserted via a shared event log);
//     hasNext/hasPrev reflect the adjacency (false at the ends).
//   * HIDE vs CLOSE: hiding the reader (visible:false — how the three callers "go back") FLUSHES
//     progress but must NOT close the backend entry (reopen-same-entry would blank); the entry is
//     torn down ONLY on destruction (shutdown()).
//   * progressKind flips manga -> comic -> tankoban as western/entryKind change.
//   * GRACEFUL DEGRADATION: null core+progress seams never error; a null STORE (western caller with
//     no Comics context property) never errors.
//   * NO Guided: the shell imports nothing under guided/ and references no guided service — the
//     grep-style assertion lives in tests/test_comicreader_shell.ps1 (PowerShell can read the file;
//     qml.exe cannot read arbitrary local files reliably). This harness pins BEHAVIOR.
//
// The three seams are injectable BECAUSE the shell declares them as plain `property var` with a
// `typeof <ContextProperty> !== "undefined" ? ... : null` default — createObject assignment
// overrides that default with our fake, exactly the mechanism the real app uses (context
// properties) and the reason the shell degrades gracefully when a seam is absent (every call is
// guarded `if (core) ...` / `if (progress) ...`).
//
// HOUSE HARNESS PATTERN (mirrors tests/comicreader_contract_harness.qml): a thrown error HANGS
// qml.exe offscreen, so `ck` never throws — it collects failures; the run prints exactly ONE
// `COMICREADER_SHELL_OK` when clean, else one `COMICREADER_SHELL_FAIL: <msg>` per failure and
// Qt.exit(1). Because per-page recording is debounced, the run has a SYNC phase (all immediate
// assertions) then a DEFERRED phase (fires after the debounce, asserts the debounced record +
// close), then reports. A safety-net Timer fails loudly on a true hang instead of stalling CI.

import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }

    // shared ordered event log — both the fake core and the fake progress push here so the
    // crossing test can prove record-before-jump ordering across two separate spies.
    property var events: []

    // ---- fake backend core: the ComicReaderCore QML-facing API (Task 7) ----
    component FakeCore: QtObject {
        // Q_PROPERTY surface the shell/surfaces read (present for shape; shell only needs a few)
        property int generation: 0
        property int pageCount: 0
        property string couplingState: "auto:normal:1.0"
        property var stripModel: null
        // spies
        property var lastOpenEntry: null
        property int openCount: 0
        property bool closed: false
        property int closeCount: 0
        // signals (shape parity with the real core)
        signal entryChanged()
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        signal pairingChanged()
        signal progressChanged()
        function openEntry(entryId, pages, direction, persisted) {
            lastOpenEntry = { entryId: String(entryId), pages: pages,
                              direction: String(direction), persisted: persisted,
                              pageCount: (pages ? pages.length : 0) }
            pageCount = pages ? pages.length : 0
            openCount += 1
            harness.events.push({ t: "open", id: String(entryId) })
        }
        function closeEntry() { closed = true; closeCount += 1; harness.events.push({ t: "close" }) }
        function setVisible(pages) {}
        function unitForPage(page) { return { rightIndex: page - 1, leftIndex: -1, spread: false } }
        function setSpreadOverride(page, state) {}
        function nudgeCoupling() {}
        // settings-seam surface (Task 12): the shell pushes taste in and reads it back out
        property int stripWidthPct: 78
        property int stripGap: 0
        property bool memorySaver: false
        property var lastStripLayout: null
        property var blob: ({})        // what persistedState() hands back to the shell's saver
        function setMemorySaver(on) { memorySaver = (on === true) }
        function setStripLayout(w, g) { stripWidthPct = w; stripGap = g; lastStripLayout = { w: w, g: g } }
        function resetCoupling() {}
        function persistedState() { return JSON.parse(JSON.stringify(blob)) }
    }

    // ---- fake persistence stores: the shape of the shell's three Settings seams ----
    component FakePrefs: QtObject {
        property string nightVeil: "off"
        property real   gutterStrength: 0.35
        property int    stripWidthPct: 78
        property int    stripGap: 0
        property bool   memorySaver: false
        property string readingMode: ""
    }
    component FakeRecords: QtObject { property string all: "{}" }

    // ---- fake Progress sink: record(payload) spy + get(kind, id) ----
    component FakeProgress: QtObject {
        property var saved: null          // what get(kind, id) hands back (resume restore)
        property var lastRecord: null
        property var records: []
        function record(payload) {
            lastRecord = payload
            records.push(payload)
            harness.events.push({ t: "record",
                                  chapter: (payload && payload.resume) ? String(payload.resume.chapterId) : "" })
        }
        function get(kind, id) { return saved }
    }

    // ---- fake page store: same localPages/statusOf/download shape as Downloads/Comics ----
    component FakePageStore: QtObject {
        property var pages: []
        property string lastLocalPagesArg: "<none>"
        property var lastDownloadChapter: null
        property var lastDownloadIssue: null
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
        function localPages(cid) { lastLocalPagesArg = String(cid); return pages }
        function statusOf(cid) { return { state: pages.length ? "ready" : "none", done: 0, total: 0 } }
        function downloadChapter(cid, sid, title, label) {
            lastDownloadChapter = { cid: String(cid), sid: String(sid), title: String(title), label: String(label) }
        }
        function downloadIssue(cid, url, sid, title, label, bytes) {
            lastDownloadIssue = { cid: String(cid), url: String(url), sid: String(sid),
                                  title: String(title), label: String(label), bytes: bytes }
        }
    }

    // find a descendant item by its marker objectName (the shell tags its veil overlay)
    function byName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var f = byName(kids[i], name)
            if (f) return f
        }
        return null
    }

    // small structural deep-equal (plain object/array payload shapes)
    function deepEqual(a, b) {
        if (a === b) return true
        if (typeof a !== typeof b) return false
        if (a === null || b === null) return false
        if (typeof a !== "object") return false
        var ak = Object.keys(a), bk = Object.keys(b)
        if (ak.length !== bk.length) return false
        for (var i = 0; i < ak.length; i++) {
            var k = ak[i]
            if (!b.hasOwnProperty(k)) return false
            if (!deepEqual(a[k], b[k])) return false
        }
        return true
    }

    function fivePages() {
        return [ { index: 0, url: "file:///f/p0.png", group: 0 },
                 { index: 1, url: "file:///f/p1.png", group: 0 },
                 { index: 2, url: "file:///f/p2.png", group: 0 },
                 { index: 3, url: "file:///f/p3.png", group: 0 },
                 { index: 4, url: "file:///f/p4.png", group: 0 } ]
    }

    // ---- signal probes ----
    property string gotSource: ""
    property bool gotBack: false
    property bool gotMin: false
    property bool gotFull: false
    property bool gotClose: false

    property var shellComp: null

    // makeShell(cfg) — create the shell with injected fakes; returns the instance.
    // Fresh in-memory persistence stores. The shell reads/writes its settings through injectable
    // seams for exactly this reason: without them a harness run reads whatever a PREVIOUS run left
    // in the real reader settings (so the suite passes or fails depending on history) and writes its
    // own scratch values back into them. A test must never be able to change your night veil.
    //
    // These are real QtObjects, NOT JS object literals: a literal passed through createObject is
    // converted to a QVariantMap — a COPY — so the shell's write-backs would land in the copy and
    // the assertions would silently test nothing. A QObject is passed by reference, exactly like the
    // real Settings element it stands in for.
    Component { id: prefsComp; FakePrefs {} }
    Component { id: recordsComp; FakeRecords {} }
    function freshPrefs(over) { return prefsComp.createObject(harness, over || {}) }
    function freshRecords(json) { return recordsComp.createObject(harness, { all: json || "{}" }) }

    function makeShell(cfg) {
        if (!cfg.globalPrefs)   cfg.globalPrefs   = freshPrefs()
        if (!cfg.seriesRecords) cfg.seriesRecords = freshRecords()
        if (!cfg.entryRecords)  cfg.entryRecords  = freshRecords()
        var inst = shellComp.createObject(harness, cfg)
        if (!inst) throw new Error("shell createObject returned null")
        return inst
    }

    function report() {
        if (failures.length === 0) {
            console.log("COMICREADER_SHELL_OK")
            Qt.exit(0)
        } else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_SHELL_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    // context stashed for the DEFERRED phase (asserted after the debounce interval elapses)
    property var _mShell: null
    property var _mProg: null
    property var _mCore: null
    property int _recBefore: 0
    property var _expectPageRec: null

    function runChecks() {
        try {
            // ===== 1. READY MANGA: openEntry(localPages, rtl); max; progressKind =====
            // recordDebounceMs pinned tiny so the DEFERRED phase can observe the debounced record fast.
            var mCore = fakeCoreA, mProg = fakeProgA, mStore = fakeStoreA
            mStore.pages = fivePages()
            var mShell = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20,
                "seriesId": "s1", "seriesTitle": "Contract Series", "seriesCover": "file:///f/cover.png",
                "core": mCore, "progress": mProg, "pageStore": mStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })

            ck(mShell.max === 5, "ready manga: reader.max must be 5, got " + mShell.max)
            ck(mShell.pageCount === 5, "ready manga: pageCount must be 5, got " + mShell.pageCount)
            ck(String(mStore.lastLocalPagesArg) === "ch1",
               "ready manga: store.localPages(curChapterId='ch1') must be called, got '" + mStore.lastLocalPagesArg + "'")
            ck(mCore.lastOpenEntry !== null, "ready manga: core.openEntry must be called")
            if (mCore.lastOpenEntry) {
                ck(mCore.lastOpenEntry.entryId === "ch1", "ready manga: openEntry entryId must be 'ch1', got " + mCore.lastOpenEntry.entryId)
                ck(mCore.lastOpenEntry.pageCount === 5, "ready manga: openEntry must receive the 5 local pages, got " + mCore.lastOpenEntry.pageCount)
                ck(mCore.lastOpenEntry.direction === "rtl", "ready manga: openEntry direction must be 'rtl', got " + mCore.lastOpenEntry.direction)
            }
            ck(mShell.rtl === true, "ready manga: reader.rtl must be true")
            // Hemanth ruling 2026-07-25: manga now DEFAULTS to Manga mode = RTL double-page (MangaPlus),
            // not long_strip. readingMode reflects the single identity.
            ck(mShell.mode === "double_page", "ready manga: mode must default to 'double_page' (Manga/MangaPlus), got " + mShell.mode)
            ck(mShell.readingMode === "manga", "ready manga: readingMode must be 'manga', got " + mShell.readingMode)

            // trailing eager record on entry change: the freshly-opened entry is already persisted
            // (a crash before the next page-turn/close must not lose it) — MangaReader.qml:157.
            ck(mProg.records.length >= 1, "ready manga: entry open must EAGERLY record once (not wait for a page-turn), got " + mProg.records.length)

            // progressKind flips manga -> comic -> tankoban
            ck(mShell.progressKind === "manga", "progressKind must be 'manga', got " + mShell.progressKind)
            mShell.western = true
            ck(mShell.progressKind === "comic", "progressKind must flip to 'comic' when western, got " + mShell.progressKind)
            mShell.western = false; mShell.entryKind = "tankoban"
            ck(mShell.progressKind === "tankoban", "progressKind must flip to 'tankoban', got " + mShell.progressKind)
            mShell.entryKind = "manga"   // restore for the record test below

            // ---- required signals exist + connect + emit ----
            harness.gotBack = false; harness.gotMin = false; harness.gotFull = false; harness.gotClose = false
            mShell.backRequested.connect(function () { harness.gotBack = true })
            mShell.minimizeRequested.connect(function () { harness.gotMin = true })
            mShell.fullscreenRequested.connect(function () { harness.gotFull = true })
            mShell.closeRequested.connect(function () { harness.gotClose = true })
            mShell.backRequested();       ck(harness.gotBack,  "backRequested must connect + emit")
            mShell.minimizeRequested();   ck(harness.gotMin,   "minimizeRequested must connect + emit")
            mShell.fullscreenRequested(); ck(harness.gotFull,  "fullscreenRequested must connect + emit")
            mShell.closeRequested();      ck(harness.gotClose, "closeRequested must connect + emit")

            // ===== PROGRESS record on a page change is DEBOUNCED (not immediate) =====
            var recBefore = mProg.records.length
            mShell.currentPage = 3
            ck(mProg.records.length === recBefore,
               "page change must be DEBOUNCED — no immediate record, got " + mProg.records.length + " (was " + recBefore + ")")
            var expectPageRec = {
                "id": "s1", "kind": "manga", "caption": "Contract Series", "title": "Contract Series",
                "sub": "Chapter 1", "cover": "file:///f/cover.png",
                "c1": "#3a2f55", "c2": "#15111f",
                "progress": 0.6,
                "resume": { "chapterId": "ch1", "page": 3, "scrollFrac": 0, "maxSeen": 3, "finished": false }
            }
            // stash for the deferred phase (after the debounce fires + the close flush)
            harness._mShell = mShell; harness._mProg = mProg; harness._mCore = mCore
            harness._recBefore = recBefore; harness._expectPageRec = expectPageRec

            // ===== 1b. HIDE must NOT close the backend (callers hide on back, then reopen) =====
            var hCore = fakeCoreH, hProg = fakeProgH, hStore = fakeStoreH
            hStore.pages = fivePages()
            var hShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-hide", "seriesTitle": "Hide", "seriesCover": "file:///f/h.png",
                "core": hCore, "progress": hProg, "pageStore": hStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            var hOpenCount = hCore.openCount
            ck(hCore.closed === false, "hide test: entry must be open (not closed) after construction")
            hShell.visible = false
            ck(hCore.closed === false, "HIDE must NOT call core.closeEntry() (callers reopen the same entry)")
            hShell.visible = true
            ck(hCore.closed === false, "reopen after hide must keep the entry open (still not closed)")
            ck(hCore.openCount === hOpenCount, "reopen-same-entry after hide must not need a reload/reopen, got openCount " + hCore.openCount)

            // ===== 2a. READY WESTERN: direction 'ltr', progressKind 'comic' =====
            var wCore = fakeCoreW, wProg = fakeProgW, wStore = fakeStoreW
            wStore.pages = fivePages()
            var wShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "gc:x", "seriesTitle": "West", "seriesCover": "file:///f/w.png",
                "core": wCore, "progress": wProg, "pageStore": wStore,
                "entryKind": "manga", "western": true,
                "chapters": [{ "id": "iss1", "number": "1", "name": "", "url": "http://p/1", "sizeMB": 10 }],
                "chapterId": "iss1", "chapterLabel": "Issue 1"
            })
            ck(wCore.lastOpenEntry && wCore.lastOpenEntry.direction === "ltr",
               "ready western: openEntry direction must be 'ltr', got " + (wCore.lastOpenEntry ? wCore.lastOpenEntry.direction : "<none>"))
            ck(wShell.rtl === false, "ready western: rtl must be false")
            ck(wShell.progressKind === "comic", "ready western: progressKind must be 'comic', got " + wShell.progressKind)

            // ===== 2b. UNAVAILABLE MANGA chapter -> store.downloadChapter(...) =====
            var uCore = fakeCoreU, uProg = fakeProgU, uStore = fakeStoreU
            uStore.pages = []   // not downloaded
            var uShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s2", "seriesTitle": "Unavail", "seriesCover": "file:///f/u.png",
                "core": uCore, "progress": uProg, "pageStore": uStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "chU", "number": "9", "name": "" }],
                "chapterId": "chU", "chapterLabel": "Chapter 9"
            })
            ck(uShell.max === 0, "unavailable manga: max must be 0, got " + uShell.max)
            ck(uCore.lastOpenEntry === null, "unavailable manga: core.openEntry must NOT be called for a not-ready entry")
            // NOT recorded while max <= 0 (MangaReader.qml:211 guard) — even the eager entry record is guarded
            ck(uProg.records.length === 0, "unavailable manga: progress.record must NOT fire while max<=0, got " + uProg.records.length)
            uShell.currentPage = 2   // poke a page change — still guarded
            ck(uProg.records.length === 0, "unavailable manga: a page change with max<=0 must not record, got " + uProg.records.length)
            uShell.startDownload()
            var dc = uStore.lastDownloadChapter
            ck(dc !== null && dc.cid === "chU" && dc.sid === "s2" && dc.label === "Chapter 9",
               "unavailable manga startDownload must call store.downloadChapter(chU, s2, .., 'Chapter 9'), got " + JSON.stringify(dc))

            // ===== 2c. UNAVAILABLE WESTERN issue -> store.downloadIssue(...) =====
            var uwStore = fakeStoreUW
            uwStore.pages = []
            var uwShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "gc:y", "seriesTitle": "UW", "seriesCover": "",
                "core": fakeCoreUW, "progress": fakeProgUW, "pageStore": uwStore,
                "entryKind": "manga", "western": true,
                "chapters": [{ "id": "issU", "number": "3", "name": "", "url": "http://p/3", "sizeMB": 5 }],
                "chapterId": "issU", "chapterLabel": "Issue 3"
            })
            uwShell.startDownload()
            var di = uwStore.lastDownloadIssue
            ck(di !== null && di.cid === "issU" && di.url === "http://p/3" && di.sid === "gc:y"
               && di.bytes === 5 * 1024 * 1024,
               "unavailable western startDownload must call store.downloadIssue(issU, url, gc:y, .., 5MiB), got " + JSON.stringify(di))

            // ===== 2d. UNAVAILABLE TANKOBAN volume -> sourceRequested(entryId) =====
            var tStore = fakeStoreT
            tStore.pages = []
            harness.gotSource = ""
            var tShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "tk:1", "seriesTitle": "Tank", "seriesCover": "file:///f/t.png",
                "core": fakeCoreT, "progress": fakeProgT, "pageStore": tStore,
                "entryKind": "tankoban", "entryLabelPrefix": "Vol. ",
                "chapters": [{ "id": "v1", "number": 1, "name": "" }],
                "chapterId": "v1", "chapterLabel": "Vol. 1"
            })
            tShell.sourceRequested.connect(function (id) { harness.gotSource = String(id) })
            tShell.startDownload()
            ck(harness.gotSource === "v1",
               "unavailable tankoban startDownload must emit sourceRequested('v1'), got '" + harness.gotSource + "'")
            ck(fakeStoreT.lastDownloadChapter === null && fakeStoreT.lastDownloadIssue === null,
               "tankoban must NOT touch a chapter-download API")

            // ===== 3. RESUME: apply saved page + strip fraction + maxSeen before first paint =====
            var rCore = fakeCoreR, rProg = fakeProgR, rStore = fakeStoreR
            rStore.pages = fivePages()
            rProg.saved = { "cover": "file:///f/c.png",
                            "resume": { "chapterId": "ch1", "page": 3, "scrollFrac": 0.5, "maxSeen": 4 } }
            var rShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s3", "seriesTitle": "Resume", "seriesCover": "file:///f/c.png",
                "core": rCore, "progress": rProg, "pageStore": rStore,
                // strip-fraction resume only applies in the vertical Strip layout; force it so the
                // scrollFrac restore is exercised (manga now defaults to double-page — MangaPlus).
                "persistedMode": "long_strip",
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(rShell.readingMode === "strip", "resume: forced Strip layout must read as readingMode 'strip', got " + rShell.readingMode)
            ck(rShell.currentPage === 3, "resume: currentPage must be restored to 3 before first paint, got " + rShell.currentPage)
            ck(rShell.stripFraction === 0.5, "resume: stripFraction must be restored to 0.5, got " + rShell.stripFraction)
            ck(rShell.maxSeen === 4, "resume: maxSeen must be restored to 4, got " + rShell.maxSeen)

            // ===== 4a. CROSSING next: newest-first, record BEFORE jump; hasNext/hasPrev =====
            var xCore = fakeCoreX, xProg = fakeProgX, xStore = fakeStoreX
            xStore.pages = fivePages()   // every id is 'ready' (localPages ignores id here)
            harness.events = []
            var xShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-cross", "seriesTitle": "Cross", "seriesCover": "file:///f/x.png",
                "core": xCore, "progress": xProg, "pageStore": xStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "c3", "number": "3", "name": "" },
                             { "id": "c2", "number": "2", "name": "" },
                             { "id": "c1", "number": "1", "name": "" }],
                "chapterId": "c2", "chapterLabel": "Chapter 2"
            })
            // at the MIDDLE entry: both directions available
            ck(xShell.hasNext === true, "hasNext must be true at a middle entry (c2)")
            ck(xShell.hasPrev === true, "hasPrev must be true at a middle entry (c2)")
            xShell.goNext()
            ck(String(xShell.curChapterId) === "c3", "crossing next: curChapterId must become 'c3' (newest-first index-1), got " + xShell.curChapterId)
            ck(xCore.lastOpenEntry && xCore.lastOpenEntry.entryId === "c3",
               "crossing next: core.openEntry must open 'c3', got " + (xCore.lastOpenEntry ? xCore.lastOpenEntry.entryId : "<none>"))
            // at the NEWEST entry (c3): no next, still a previous
            ck(xShell.hasNext === false, "hasNext must be false at the newest entry (c3)")
            ck(xShell.hasPrev === true, "hasPrev must be true at the newest entry (c3)")
            // record(c2) must appear BEFORE open(c3) in the shared event log
            var idxRecC2 = -1, idxOpenC3 = -1
            for (var e = 0; e < harness.events.length; e++) {
                var ev = harness.events[e]
                if (idxRecC2 < 0 && ev.t === "record" && ev.chapter === "c2") idxRecC2 = e
                if (idxOpenC3 < 0 && ev.t === "open" && ev.id === "c3") idxOpenC3 = e
            }
            ck(idxRecC2 >= 0 && idxOpenC3 >= 0 && idxRecC2 < idxOpenC3,
               "crossing next: must record outgoing 'c2' BEFORE opening 'c3' (record@" + idxRecC2 + ", open@" + idxOpenC3 + ")")

            // ===== 4b. CROSSING previous: index+1 (older); hasPrev false at the oldest =====
            var pStore = fakeStoreP
            pStore.pages = fivePages()
            var pShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-prev", "seriesTitle": "Prev", "seriesCover": "file:///f/p.png",
                "core": fakeCoreP, "progress": fakeProgP, "pageStore": pStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "c3", "number": "3", "name": "" },
                             { "id": "c2", "number": "2", "name": "" },
                             { "id": "c1", "number": "1", "name": "" }],
                "chapterId": "c2", "chapterLabel": "Chapter 2"
            })
            pShell.goPrev(false)
            ck(String(pShell.curChapterId) === "c1", "crossing previous: curChapterId must become 'c1' (index+1, older), got " + pShell.curChapterId)
            ck(fakeCoreP.lastOpenEntry && fakeCoreP.lastOpenEntry.entryId === "c1",
               "crossing previous: core.openEntry must open 'c1', got " + (fakeCoreP.lastOpenEntry ? fakeCoreP.lastOpenEntry.entryId : "<none>"))
            // at the OLDEST entry (c1): no previous, still a next
            ck(pShell.hasPrev === false, "hasPrev must be false at the oldest entry (c1)")
            ck(pShell.hasNext === true, "hasNext must be true at the oldest entry (c1)")

            // ===== 5a. graceful degradation: null core+progress seams never error =====
            var gStore = fakeStoreG
            gStore.pages = fivePages()
            var gShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-null", "seriesTitle": "NullSeams", "seriesCover": "file:///f/n.png",
                "core": null, "progress": null, "pageStore": gStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(gShell.max === 5, "null seams: shell still resolves pages (max=5), got " + gShell.max)
            gShell.currentPage = 2      // must not throw with progress null
            gShell.goPrev(false)        // must not throw with core null
            gShell.shutdown()           // must not throw with core null
            ck(true, "null seams: page change / crossing / shutdown must not error")

            // ===== 5b. graceful degradation: a NULL STORE (western caller, no Comics ctx prop) =====
            // pageStore null AND no Comics/Downloads context property offscreen -> store resolves null.
            var sCore = fakeCoreS, sProg = fakeProgS
            var sShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "gc:z", "seriesTitle": "NullStore", "seriesCover": "file:///f/z.png",
                "core": sCore, "progress": sProg, "pageStore": null,
                "entryKind": "manga", "western": true,
                "chapters": [{ "id": "issZ", "number": "1", "name": "", "url": "http://p/z", "sizeMB": 1 }],
                "chapterId": "issZ", "chapterLabel": "Issue Z"
            })
            ck(sShell.max === 0, "null store: with no store the shell resolves no pages (max=0), got " + sShell.max)
            ck(sCore.lastOpenEntry === null, "null store: core.openEntry must NOT be called (no pages)")
            sShell.currentPage = 2      // must not throw
            sShell.startDownload()      // guarded by `if (!store) return` — must not throw
            sShell.goNext()             // must not throw
            sShell.shutdown()           // must not throw
            ck(true, "null store: page change / startDownload / crossing / shutdown must not error")

            // ===== 6. NIGHT VEIL: a page-dim overlay whose opacity tracks nightVeil (0/.12/.26) =====
            // The settings sheet writes reader.nightVeil (a live setting); the shell paints a black
            // veil over the reading surfaces at ComicReaderState.nightVeilOpacity(level). Default off.
            var vStore = fakeStoreV
            vStore.pages = fivePages()
            var vShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-veil", "seriesTitle": "Veil", "seriesCover": "file:///f/v.png",
                "core": fakeCoreV, "progress": fakeProgV, "pageStore": vStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(vShell.nightVeil === "off", "night veil: shell must default nightVeil='off', got '" + vShell.nightVeil + "'")
            var veil = byName(vShell, "nightVeil")
            ck(veil !== null, "night veil: a veil overlay (objectName 'nightVeil') must be mounted in the shell")
            if (veil) {
                ck(veil.opacity === 0, "night veil: opacity must be 0 when nightVeil='off', got " + veil.opacity)
                vShell.nightVeil = "low"
                ck(Math.abs(veil.opacity - 0.12) < 1e-9, "night veil: opacity must be 0.12 when nightVeil='low', got " + veil.opacity)
                vShell.nightVeil = "high"
                ck(Math.abs(veil.opacity - 0.26) < 1e-9, "night veil: opacity must be 0.26 when nightVeil='high', got " + veil.opacity)
            }

            // ===== 7. GUTTER SHADOW: shell.gutterStrength feeds the double surface (real behavior) =====
            // The settings sheet writes reader.gutterStrength; the shell must drive the double
            // surface's gutter shadow from it, so a chip tap actually changes the spine shadow.
            ck(vShell.gutterStrength === 0.35, "gutter: shell must default gutterStrength=0.35, got " + vShell.gutterStrength)
            var dsurf = byName(vShell, "doubleSurface")
            ck(dsurf !== null, "gutter: the double surface must be mounted (objectName 'doubleSurface')")
            if (dsurf) {
                ck(Math.abs(dsurf.gutterStrength - 0.35) < 1e-9, "gutter: double surface must inherit the shell default 0.35, got " + dsurf.gutterStrength)
                vShell.gutterStrength = 0.55
                ck(Math.abs(dsurf.gutterStrength - 0.55) < 1e-9, "gutter: double surface gutterStrength must track shell.gutterStrength (0.55), got " + dsurf.gutterStrength)
                vShell.gutterStrength = 0
                ck(dsurf.gutterStrength === 0, "gutter: setting shell.gutterStrength=0 must reach the double surface (Off), got " + dsurf.gutterStrength)
            }

            // ===== 8. PERSISTENCE: the reader REMEMBERS across launches =====
            // Three stores, three different lifetimes: global taste, per-series identity, per-entry
            // reader state. All three are injected here, so these assertions are about the shell's
            // logic and never about whatever is in the real settings.

            // -- 8a. globals are applied at construction, BEFORE the first entry opens --
            var pStore = fakeStoreP
            pStore.pages = fivePages()
            var pPrefs = freshPrefs({ nightVeil: "high", gutterStrength: 0.55,
                                      stripWidthPct: 62, stripGap: 20, memorySaver: true })
            var pShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-persist", "seriesTitle": "Persist", "seriesCover": "file:///f/p.png",
                "core": fakeCoreP, "progress": fakeProgP, "pageStore": pStore,
                "globalPrefs": pPrefs,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(pShell.nightVeil === "high", "persist: a stored night veil must be applied at construction, got '" + pShell.nightVeil + "'")
            ck(Math.abs(pShell.gutterStrength - 0.55) < 1e-9, "persist: a stored gutter must be applied, got " + pShell.gutterStrength)
            ck(fakeCoreP.lastStripLayout !== null && fakeCoreP.lastStripLayout.w === 62 && fakeCoreP.lastStripLayout.g === 20,
               "persist: the stored strip measure must be pushed to the backend at construction")
            // memorySaver is a GLOBAL that rides the per-entry blob into openEntry
            ck(fakeCoreP.lastOpenEntry && fakeCoreP.lastOpenEntry.persisted
               && fakeCoreP.lastOpenEntry.persisted.memorySaver === true,
               "persist: the global memorySaver must ride the per-entry blob into openEntry")

            // -- 8b. changing a setting writes straight back to the global store --
            pShell.nightVeil = "low"
            ck(pPrefs.nightVeil === "low", "persist: changing the night veil must write it back, got '" + pPrefs.nightVeil + "'")
            pShell.gutterStrength = 0.22
            ck(Math.abs(pPrefs.gutterStrength - 0.22) < 1e-9, "persist: changing the gutter must write it back")

            // -- 8c. per-series identity override beats the lane default --
            var qStore = fakeStoreQ
            qStore.pages = fivePages()
            var qShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-op", "seriesTitle": "OP", "seriesCover": "file:///f/o.png",
                "core": fakeCoreQ, "progress": fakeProgQ, "pageStore": qStore,
                "seriesRecords": freshRecords('{"s-op":{"rm":"strip"}}'),
                "entryKind": "manga", "western": false,     // lane default would be Manga
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(qShell.readingMode === "strip",
               "persist: a per-series override must beat the lane default, got '" + qShell.readingMode + "'")

            // -- 8d. no per-series record -> the LAST mode chosen anywhere is the default --
            var rStore2 = fakeStoreY
            rStore2.pages = fivePages()
            var yShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-new", "seriesTitle": "New", "seriesCover": "file:///f/n.png",
                "core": fakeCoreY, "progress": fakeProgY, "pageStore": rStore2,
                "globalPrefs": freshPrefs({ readingMode: "comic" }),
                "entryKind": "manga", "western": false,     // lane default is Manga; global says Comic
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(yShell.readingMode === "comic",
               "persist: an untouched series must follow the last mode chosen anywhere, got '" + yShell.readingMode + "'")

            // -- 8e. picking a mode writes BOTH the series record and the global default --
            var qSeries = freshRecords()
            var zStore = fakeStoreZ
            zStore.pages = fivePages()
            var zPrefs = freshPrefs()
            var zShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-z", "seriesTitle": "Z", "seriesCover": "file:///f/z.png",
                "core": fakeCoreZ, "progress": fakeProgZ, "pageStore": zStore,
                "globalPrefs": zPrefs, "seriesRecords": qSeries,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            zShell.setReadingMode("strip")
            ck(zPrefs.readingMode === "strip", "persist: picking a mode must update the global default")
            ck(JSON.parse(qSeries.all)["s-z"].rm === "strip", "persist: picking a mode must record it for THIS series")

            // -- 8f. a stored entry blob reaches openEntry; a corrupt store degrades to no memory --
            ck(qShell.persistedState !== null, "persist: persistedState must never be null after load")
            var cShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-c", "seriesTitle": "C", "seriesCover": "file:///f/c2.png",
                "core": fakeCoreC, "progress": fakeProgC, "pageStore": fakeStoreC,
                "entryRecords": freshRecords("not json{"),   // corrupt on purpose
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(cShell.max === 0 || cShell.max >= 0, "persist: a CORRUPT entry store must not throw on open")

            // -- 8g. the book's record is written on shutdown, WITHOUT the global riding along --
            var sRecords = freshRecords()
            fakeCoreS2.blob = { spreadOverrides: { "3": true }, couplingMode: "manual",
                                couplingPhase: "shifted", bookmarks: [2], memorySaver: true }
            var sStore2 = fakeStoreS2
            sStore2.pages = fivePages()
            var sShell2 = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-save", "seriesTitle": "Save", "seriesCover": "file:///f/s.png",
                "core": fakeCoreS2, "progress": fakeProgS2, "pageStore": sStore2,
                "entryRecords": sRecords,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            sShell2.shutdown()
            var saved = JSON.parse(sRecords.all)["ch1"]
            ck(saved !== undefined, "persist: shutdown must write the book's record immediately (no waiting on the debounce)")
            if (saved) {
                ck(saved.spreadOverrides && saved.spreadOverrides["3"] === true, "persist: spread overrides must survive")
                ck(saved.couplingMode === "manual", "persist: a manual coupling must survive")
                ck(saved.bookmarks && saved.bookmarks.length === 1, "persist: bookmarks must survive")
                ck(saved.memorySaver === undefined,
                   "persist: the GLOBAL memorySaver must be stripped from the per-book record")
            }

            // -- 8h. a crossing files the OUTGOING book's record under the OUTGOING id --
            var xRecords = freshRecords()
            fakeCoreW2.blob = { bookmarks: [4] }
            var wStore = fakeStoreW2
            wStore.pages = fivePages()
            var wShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-w", "seriesTitle": "W", "seriesCover": "file:///f/w.png",
                "core": fakeCoreW2, "progress": fakeProgW2, "pageStore": wStore,
                "entryRecords": xRecords,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch2", "number": "2", "name": "" }, { "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch2", "chapterLabel": "Chapter 2"
            })
            wShell.openEntryById("ch1", false)        // cross to the older entry
            var wm = JSON.parse(xRecords.all)
            ck(wm["ch2"] !== undefined,
               "persist: crossing must file the OUTGOING book's record under ITS id (ch2), got keys " + JSON.stringify(Object.keys(wm)))

        } catch (e) {
            failures.push("exception during checks: " + e.message)
        }
        // hand off to the deferred phase to observe the DEBOUNCED page-change record + close
        deferredTimer.start()
    }

    // DEFERRED phase — runs after the debounce interval elapses: assert the debounced page-change
    // record fired with the exact §4.1 payload, then assert close flushes immediately + closes.
    function runDeferred() {
        try {
            ck(_mProg.records.length > _recBefore,
               "page change must produce a DEBOUNCED record after the interval, got " + _mProg.records.length + " (was " + _recBefore + ")")
            ck(deepEqual(_mProg.lastRecord, _expectPageRec),
               "debounced page-change record must deep-equal the §4.1 payload, got " + JSON.stringify(_mProg.lastRecord))

            // CLOSE: immediate flush + core.closeEntry() (must NOT wait for the debounce)
            var recBeforeClose = _mProg.records.length
            _mShell.shutdown()
            ck(_mProg.records.length > recBeforeClose, "close must flush a final progress.record (immediate)")
            ck(deepEqual(_mProg.lastRecord, _expectPageRec),
               "close record must deep-equal the §4.1 payload (state unchanged), got " + JSON.stringify(_mProg.lastRecord))
            ck(_mCore.closed === true, "close (shutdown) must call core.closeEntry()")
        } catch (e) {
            failures.push("exception during deferred checks: " + e.message)
        }
        report()
    }

    // declarative fake instances (one bundle per scenario, ids referenced in runChecks)
    FakeCore { id: fakeCoreA }   FakeProgress { id: fakeProgA }   FakePageStore { id: fakeStoreA }
    FakeCore { id: fakeCoreH }   FakeProgress { id: fakeProgH }   FakePageStore { id: fakeStoreH }
    FakeCore { id: fakeCoreW }   FakeProgress { id: fakeProgW }   FakePageStore { id: fakeStoreW }
    FakeCore { id: fakeCoreU }   FakeProgress { id: fakeProgU }   FakePageStore { id: fakeStoreU }
    FakeCore { id: fakeCoreUW }  FakeProgress { id: fakeProgUW }  FakePageStore { id: fakeStoreUW }
    FakeCore { id: fakeCoreT }   FakeProgress { id: fakeProgT }   FakePageStore { id: fakeStoreT }
    FakeCore { id: fakeCoreR }   FakeProgress { id: fakeProgR }   FakePageStore { id: fakeStoreR }
    FakeCore { id: fakeCoreX }   FakeProgress { id: fakeProgX }   FakePageStore { id: fakeStoreX }
    FakeCore { id: fakeCoreP }   FakeProgress { id: fakeProgP }   FakePageStore { id: fakeStoreP }
    FakeCore { id: fakeCoreS }   FakeProgress { id: fakeProgS }
    FakePageStore { id: fakeStoreG }
    FakeCore { id: fakeCoreV }   FakeProgress { id: fakeProgV }   FakePageStore { id: fakeStoreV }
    // --- persistence phase (section 8) ---
    FakeCore { id: fakeCoreQ }   FakeProgress { id: fakeProgQ }   FakePageStore { id: fakeStoreQ }
    FakeCore { id: fakeCoreY }   FakeProgress { id: fakeProgY }   FakePageStore { id: fakeStoreY }
    FakeCore { id: fakeCoreZ }   FakeProgress { id: fakeProgZ }   FakePageStore { id: fakeStoreZ }
    FakeCore { id: fakeCoreC }   FakeProgress { id: fakeProgC }   FakePageStore { id: fakeStoreC }
    FakeCore { id: fakeCoreS2 }  FakeProgress { id: fakeProgS2 }  FakePageStore { id: fakeStoreS2 }
    FakeCore { id: fakeCoreW2 }  FakeProgress { id: fakeProgW2 }  FakePageStore { id: fakeStoreW2 }

    // fires the deferred phase after the pinned 20ms record debounce has elapsed
    Timer { id: deferredTimer; interval: 150; running: false; onTriggered: harness.runDeferred() }

    Component.onCompleted: {
        try {
            shellComp = Qt.createComponent("../qml/comicreader/ComicReaderShell.qml")
            if (shellComp.status === Component.Error) throw new Error("shell component: " + shellComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_SHELL_FAIL: setup: " + e.message); Qt.exit(1)
        }
    }

    // safety net — a true hang (not a thrown error) still fails loudly instead of stalling CI
    Timer {
        interval: 8000; running: true
        onTriggered: { console.log("COMICREADER_SHELL_FAIL: timeout"); Qt.exit(1) }
    }
}
