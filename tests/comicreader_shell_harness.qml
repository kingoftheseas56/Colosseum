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
        property var lastVisible: null
        function setVisible(pages) { lastVisible = pages }
        property var fakeUnit: null   // when set, unitForPage returns THIS regardless of page (B5 RTL test)
        // NOTE on the default: it answers `rightIndex: page - 1`, which is NOT what the real core
        // answers for an unpaired page (ComicReaderCore returns rightIndex == page). The existing
        // checks that use this fake only care that SOME unit comes back, so the shift is harmless to
        // them and is left alone rather than changed underneath them. It is not harmless to anything
        // doing forward unit arithmetic: with the shift, "the unit after this one" walks backwards,
        // so pageNext could never advance and an end-of-book test read as a code bug for an hour.
        // unitIdentity opts into the real core's contract for tests that need honest unit math.
        property bool unitIdentity: false
        function unitForPage(page) {
            if (fakeUnit !== null) return fakeUnit
            if (unitIdentity) return { rightIndex: page, leftIndex: -1, spread: false }
            return { rightIndex: page - 1, leftIndex: -1, spread: false }
        }
        // spread override spy (B5): pageInfo reports the override as absent/true/false (matches the
        // real core's PageMeta::toVariantMap — absence IS the auto state, never a third "auto" value).
        property var fakePageInfo: ({})
        property var lastSpreadOverride: null
        function pageInfo(page) { return fakePageInfo }
        function setSpreadOverride(page, state) { lastSpreadOverride = { page: page, state: String(state) } }
        // The real core FLIPS the phase and republishes couplingState; F3 reads that string back to
        // learn which phase the nudge landed on, so a no-op fake would make the test vacuous.
        property int nudgeCalls: 0
        function nudgeCoupling() {
            nudgeCalls += 1
            couplingState = (couplingState.split(":")[1] === "shifted")
                ? "manual:normal:1.0" : "manual:shifted:1.0"
        }
        // settings-seam surface (Task 12): the shell pushes taste in and reads it back out
        property int stripWidthPct: 78
        property int stripGap: 0
        property bool memorySaver: false
        property var lastStripLayout: null
        property var blob: ({})        // what persistedState() hands back to the shell's saver
        function setMemorySaver(on) { memorySaver = (on === true) }
        function setStripLayout(w, g) { stripWidthPct = w; stripGap = g; lastStripLayout = { w: w, g: g } }
        property int resetCalls: 0
        function resetCoupling() { resetCalls += 1; couplingState = "auto:normal:1.0" }
        function persistedState() { return JSON.parse(JSON.stringify(blob)) }
        // bookmarks (B6): a REAL toggle over a JS array — not a stub — so the shell's live-list
        // wiring is actually exercised, not merely assumed. Mirrors the real backend's contract:
        // sorted, de-duped, and folded into persistedState()'s "bookmarks" entry.
        property var bookmarksArr: []
        property var lastToggleBookmark: null
        signal bookmarksChanged()
        function toggleBookmark(page) {
            lastToggleBookmark = page
            var i = bookmarksArr.indexOf(page)
            if (i >= 0) bookmarksArr.splice(i, 1)
            else { bookmarksArr.push(page); bookmarksArr.sort(function (a, b) { return a - b }) }
            blob = Object.assign({}, blob, { bookmarks: bookmarksArr.slice() })
            bookmarksChanged()
        }
        function bookmarks() { return bookmarksArr.slice() }
    }

    // ---- fake persistence stores: the shape of the shell's three Settings seams ----
    component FakePrefs: QtObject {
        property string nightVeil: "off"
        property real   gutterStrength: 0.35
        property int    stripWidthPct: 78
        property int    stripGap: 0
        property bool   memorySaver: false
        // the last LAYOUT + ORDER picked anywhere (Task 3 — two independent keys), plus the LEGACY
        // combined identity the shipped reader wrote, which the shell still reads on first launch.
        // These three must mirror the real Settings element: a fake that is missing a property the
        // shell writes turns a real bug into a silent no-op.
        property string layout: ""
        property string order: ""
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
    property var _mB6Records: null
    property var _csShell: null
    property var _csArea: null

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
            // Task 3: layout + order are the state; mode + rtl are derived aliases of them.
            ck(mShell.layout === "paired_pages",
               "ready manga: layout must default to 'paired_pages', got " + mShell.layout)
            ck(mShell.order === "rtl", "ready manga: order must default to 'rtl', got " + mShell.order)

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
                // Task 3: forced through the REAL persistence path (a stored series record), not an
                // override seam — the seams are gone and the record is the memory.
                "seriesRecords": freshRecords('{"s3":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(rShell.readingMode === "strip", "resume: forced Strip layout must read as readingMode 'strip', got " + rShell.readingMode)
            ck(rShell.currentPage === 3, "resume: currentPage must be restored to 3 before first paint, got " + rShell.currentPage)
            ck(rShell.stripFraction === 0.5, "resume: stripFraction must be restored to 0.5, got " + rShell.stripFraction)
            ck(rShell.maxSeen === 4, "resume: maxSeen must be restored to 4, got " + rShell.maxSeen)

            // resume routes through the ONE restore door: a saved fraction arms it, and a
            // page-only record (scrollFrac 0, page > 1) must arm it too — that second shape is
            // what a paged-mode record looks like, and it used to leave the column at the top.
            ck(Math.abs(rShell._pendingStripFrac - 0.5) < 1e-9,
               "resume: the saved scrollFrac must arm _pendingStripFrac, got " + rShell._pendingStripFrac)

            var r2Prog = fakeProgR2
            r2Prog.saved = { "resume": { "chapterId": "ch1", "page": 3, "scrollFrac": 0, "maxSeen": 4, "finished": false } }
            var r2Store = fakeStoreR2; r2Store.pages = fivePages()
            var r2 = makeShell({
                "width": 640, "height": 480, "seriesId": "s3b", "seriesTitle": "Resume2",
                "seriesCover": "file:///f/c.png", "core": fakeCoreR2, "progress": r2Prog,
                "pageStore": r2Store, "seriesRecords": freshRecords('{"s3b":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(r2.currentPage === 3, "resume2: page restored to 3")
            ck(r2._pendingStripFrac === 0, "resume2: no fraction to arm")
            ck(r2._stripRestorePending === true, "resume2: the restore door must be ARMED for a page-only record")

            // ===== 3b. LEAVING the strip must DISARM the pending fraction =====
            // The door is settle-gated, so there is a real window between arming it and it firing.
            // If the reader leaves Strip inside that window the door closes — but a fraction left
            // armed behind it is a landmine: the NEXT opening of the door (a mode switch BACK into
            // Strip, which is contractually "keep your page") would take the fraction arm and jump to
            // the entry's original resume spot instead, discarding the page it promised to keep.
            // That is this task's own bug class re-entering through the mode-switch path.
            var r3Prog = fakeProgR3
            r3Prog.saved = { "resume": { "chapterId": "ch1", "page": 2, "scrollFrac": 0.6, "maxSeen": 5 } }
            var r3Store = fakeStoreR3; r3Store.pages = fivePages()
            var r3 = makeShell({
                "width": 640, "height": 480, "seriesId": "s3c", "seriesTitle": "Resume3",
                "seriesCover": "file:///f/c.png", "core": fakeCoreR3, "progress": r3Prog,
                "pageStore": r3Store, "seriesRecords": freshRecords('{"s3c":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(Math.abs(r3._pendingStripFrac - 0.6) < 1e-9,
               "disarm: the saved 0.6 must arm the door, got " + r3._pendingStripFrac)

            // 2. the user leaves Strip BEFORE the settle-gated door fires
            r3.setReadingMode("comic")
            ck(r3.mode === "double_page", "disarm: setReadingMode('comic') must leave the strip, got " + r3.mode)

            // 3. the door fires while we are OFF the strip — it must CLOSE AND DISARM
            r3._runStripRestore()
            ck(r3._pendingStripFrac === 0,
               "disarm: leaving the strip must CLEAR the pending fraction, got " + r3._pendingStripFrac
               + " (armed for a later, unrelated opening)")

            // 4+5. back to Strip: the door re-arms, and the ONLY arm it can take is the PAGE arm —
            // the mode switch's kept page, never the stale entry-resume fraction.
            // The page is captured rather than asserted as a literal: in double mode goToPageIndex
            // snaps to the canonical UNIT anchor, and this harness's fake unitForPage answers
            // `rightIndex: page - 1`, so the landing page is the fake's business, not the shell's.
            // What the shell owes us is that whatever page we are on SURVIVES the switch.
            r3.goToPageIndex(4)
            var keptPage = r3.currentPage
            ck(keptPage > 1, "disarm: the fixture must sit past page 1 for the page arm to mean anything, got " + keptPage)
            r3.setReadingMode("strip")
            ck(r3.currentPage === keptPage,
               "disarm: the mode switch must KEEP the page (" + keptPage + "), got " + r3.currentPage)
            ck(r3._pendingStripFrac === 0,
               "disarm: a mode switch back into Strip must NOT resurrect the entry's resume fraction, got "
               + r3._pendingStripFrac)
            ck(r3._stripRestorePending === true, "disarm: the door must be armed again by the mode switch")

            // ===== 3c. ARMING the door always grants a FULL retry budget =====
            // The budget is spent by the retry loop on a column that has not laid out yet. If a new
            // target (a crossing, a mode switch) armed the door without resetting it, that target
            // would inherit whatever the PREVIOUS restore had left — so a slow-to-lay-out column
            // would give up early, and the retry guarantee would depend on unrelated history.
            // The shell harness's fake core has no strip model, so the surface never lays out —
            // span <= 0 is exactly the retry branch, no scaffolding needed.
            ck(r3.max > 0 && r3.mode === "long_strip", "budget: fixture must be on the strip with pages")
            r3._runStripRestore()
            r3._runStripRestore()
            ck(r3._stripRestoreTries === 2,
               "budget: an unlaid column must burn retries (expected 2), got " + r3._stripRestoreTries)
            r3.setReadingMode("comic")      // leave + come back = a NEW target for the door
            r3.setReadingMode("strip")
            ck(r3._stripRestoreTries === 0,
               "budget: arming the door for a new target must RESET the retry budget, got "
               + r3._stripRestoreTries + " already spent")

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
            ck(zPrefs.layout === "long_strip",
               "persist: picking a layout must update the global default, got '" + zPrefs.layout + "'")
            ck(JSON.parse(qSeries.all)["s-z"].layout === "long_strip",
               "persist: picking a layout must record it for THIS series, got "
               + JSON.stringify(JSON.parse(qSeries.all)["s-z"]))
            // ...and the combined identity is RETIRED from the record on that first real write, so a
            // stale "manga" can never contradict the layout/order the reader is actually using.
            ck(JSON.parse(qSeries.all)["s-z"].rm === undefined,
               "persist: a real user change must retire the legacy `rm` key, got "
               + JSON.stringify(JSON.parse(qSeries.all)["s-z"]))

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

            // -- 9. spread override (B5): right-click fixes ONE page's pairing without re-phasing
            // the book (that is what P/nudgeCoupling does). pageInfo reports the override as
            // absent/true/false; cycleSpreadOverride walks auto -> spread -> single -> auto(clear).
            // Right-click routes to it ONLY in double-page over a real unit; everywhere else (and
            // long_strip) it still opens Settings — the approved mock's explicit rule. --
            var b5Store = fakeStoreB5
            b5Store.pages = fivePages()
            var b5Shell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-b5", "seriesTitle": "B5", "seriesCover": "file:///f/b5.png",
                "core": fakeCoreB5, "progress": fakeProgB5, "pageStore": b5Store,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })

            // 9a. the full cycle, driven purely off what pageInfo reports (absence IS auto)
            fakeCoreB5.fakePageInfo = {}
            b5Shell.cycleSpreadOverride(3)
            ck(fakeCoreB5.lastSpreadOverride && fakeCoreB5.lastSpreadOverride.page === 3
               && fakeCoreB5.lastSpreadOverride.state === "spread",
               "spread override: absent (auto) must cycle to 'spread', got " + JSON.stringify(fakeCoreB5.lastSpreadOverride))

            fakeCoreB5.fakePageInfo = { spreadOverride: true }
            b5Shell.cycleSpreadOverride(3)
            ck(fakeCoreB5.lastSpreadOverride && fakeCoreB5.lastSpreadOverride.state === "single",
               "spread override: 'spread' must cycle to 'single', got " + JSON.stringify(fakeCoreB5.lastSpreadOverride))

            fakeCoreB5.fakePageInfo = { spreadOverride: false }
            b5Shell.cycleSpreadOverride(3)
            ck(fakeCoreB5.lastSpreadOverride && fakeCoreB5.lastSpreadOverride.state === "clear",
               "spread override: 'single' must cycle to 'clear' (auto), got " + JSON.stringify(fakeCoreB5.lastSpreadOverride))

            // 9b. routing: long_strip mode -> right-click still opens Settings, override untouched
            fakeCoreB5.lastSpreadOverride = null
            var gotSettingsB5 = false
            b5Shell.settingsRequested.connect(function () { gotSettingsB5 = true })
            b5Shell.layout = "long_strip"          // Task 3: layout is the writable truth; mode is derived
            b5Shell._onContextMenu(100, 100)
            ck(gotSettingsB5, "spread override: long_strip right-click must still open Settings")
            ck(fakeCoreB5.lastSpreadOverride === null, "spread override: long_strip right-click must not touch the override")

            // 9c. routing: double_page over a real pair -> resolves to a page, never opens Settings
            b5Shell.layout = "paired_pages"
            b5Shell.currentPage = 4
            fakeCoreB5.fakeUnit = { rightIndex: 7, leftIndex: 3, spread: false }
            gotSettingsB5 = false
            fakeCoreB5.lastSpreadOverride = null
            b5Shell._onContextMenu(50, 100)   // left half of a 640-wide shell
            ck(!gotSettingsB5, "spread override: double-page right-click on a pair must not open Settings")
            ck(fakeCoreB5.lastSpreadOverride !== null, "spread override: double-page right-click on a pair must cycle a page")

            // 9d. RTL mapping (ground-truthed against ComicReaderDoubleSurface's rightIndexX/
            // leftIndexX): rightIndex sits physical-RIGHT in RTL manga, physical-LEFT in LTR. So the
            // SAME left-half click must resolve to a DIFFERENT page depending on direction — the half
            // most likely to be wrong, and the one a reader notices immediately (wrong page "fixed").
            b5Shell.order = "ltr"                 // Task 3: order is the writable truth; rtl is derived
            var ltrLeftTarget = b5Shell._spreadOverrideTargetPage(50)
            b5Shell.order = "rtl"
            var rtlLeftTarget = b5Shell._spreadOverrideTargetPage(50)
            ck(ltrLeftTarget !== rtlLeftTarget,
               "spread override: a left-half click must target a different page in RTL (" + rtlLeftTarget + ") vs LTR (" + ltrLeftTarget + ")")
            ck(ltrLeftTarget === 7, "spread override: LTR left-half click must target rightIndex (physical LEFT), got " + ltrLeftTarget)
            ck(rtlLeftTarget === 3, "spread override: RTL left-half click must target leftIndex (physical LEFT), got " + rtlLeftTarget)

            // -- 10. bookmarks (B6): live end-to-end. B toggles the CURRENT page; the shell's live
            // list (what the HUD's scrub-bar ticks bind to) updates off core.bookmarksChanged — NOT
            // the load-time persistedState snapshot — and the toggle rides the same debounced
            // entrySave door a spread override does (checked in the deferred phase below). --
            var b6Store = fakeStoreB6
            b6Store.pages = fivePages()
            var b6Records = freshRecords()
            var b6Shell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-b6", "seriesTitle": "B6", "seriesCover": "file:///f/b6.png",
                "core": fakeCoreB6, "progress": fakeProgB6, "pageStore": b6Store,
                "entryRecords": b6Records,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })

            // 10a. pressing the toggle reaches core.toggleBookmark with the 0-based CURRENT page
            b6Shell.currentPage = 3
            b6Shell.bookmarkToggleRequested()
            ck(fakeCoreB6.lastToggleBookmark === 2,
               "bookmarks: B must reach core.toggleBookmark with the 0-based current page (2), got " + fakeCoreB6.lastToggleBookmark)

            // 10b. shell.liveBookmarks updates when the core emits bookmarksChanged
            ck(deepEqual(b6Shell.liveBookmarks, [2]),
               "bookmarks: liveBookmarks must reflect core.bookmarks() after a toggle, got " + JSON.stringify(b6Shell.liveBookmarks))

            // 10c. a second bookmark (page 5, index 4) ADDS, keeping the list sorted
            b6Shell.currentPage = 5
            b6Shell.bookmarkToggleRequested()
            ck(deepEqual(b6Shell.liveBookmarks, [2, 4]),
               "bookmarks: a second toggle on a different page must ADD (not replace), got " + JSON.stringify(b6Shell.liveBookmarks))

            // 10d. toggling the SAME page again REMOVES it
            b6Shell.bookmarkToggleRequested()
            ck(deepEqual(b6Shell.liveBookmarks, [2]),
               "bookmarks: toggling the same page again must REMOVE it, got " + JSON.stringify(b6Shell.liveBookmarks))

            // 10e. a bare core.entryChanged (a fresh open / crossing) also refreshes the live list —
            // the new entry's bookmarks must replace the old entry's, never linger.
            fakeCoreB6.bookmarksArr = [1]
            fakeCoreB6.entryChanged()
            ck(deepEqual(b6Shell.liveBookmarks, [1]),
               "bookmarks: core.entryChanged must refresh liveBookmarks too (a fresh open), got " + JSON.stringify(b6Shell.liveBookmarks))

            // stash for the deferred phase: the persistence debounce (entrySave, 800ms) must have
            // ARMED on the bookmark change and eventually file the live bookmarks under this entry.
            // (blob.bookmarks is [2] from the last toggleBookmark call in 10d — 10e's direct
            // bookmarksArr poke never touched blob, matching a real core where only toggleBookmark
            // itself mutates persisted state.)
            harness._mB6Records = b6Records

            // -- 11. STRIP PINNING: the mounted strip surface's visiblePages signal must reach
            // core.setVisible (pins on-screen pages in the LRU + promotes them to top decode
            // priority). Force long_strip via a stored series record so the surface mounts active.
            var vpStore = fakeStoreVP
            vpStore.pages = fivePages()
            var vpShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-vp", "seriesTitle": "VisiblePages", "seriesCover": "file:///f/vp.png",
                "core": fakeCoreVP, "progress": fakeProgVP, "pageStore": vpStore,
                "seriesRecords": freshRecords('{"s-vp":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(vpShell.mode === "long_strip", "strip pinning: fixture must be mounted in long_strip, got " + vpShell.mode)
            var vpSurface = byName(vpShell, "stripSurface")
            ck(vpSurface !== null, "strip pinning: the strip surface must be mounted (objectName 'stripSurface')")
            if (vpSurface) {
                vpSurface.visiblePages([2, 3, 4])
                ck(deepEqual(fakeCoreVP.lastVisible, [2, 3, 4]),
                   "strip pinning: the strip surface's visiblePages must reach core.setVisible, got " + JSON.stringify(fakeCoreVP.lastVisible))
            }

            // -- 12. CURSOR AUTO-HIDE: neither reference leaves an arrow parked on the page.
            // Blanks after cursorIdleMs of stillness while the chrome is away; a poke (real
            // activity) clears it; the chrome being up holds the arrow regardless of idle time. --
            var csStore = fakeStoreCS
            csStore.pages = fivePages()
            var csShell = makeShell({
                "width": 640, "height": 480, "cursorIdleMs": 25,
                "seriesId": "s-cursor", "seriesTitle": "Cursor", "seriesCover": "file:///f/cs.png",
                "core": fakeCoreCS, "progress": fakeProgCS, "pageStore": csStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(csShell._cursorIdle === false, "cursor: _cursorIdle must start false, got " + csShell._cursorIdle)
            var csArea = byName(csShell, "cursorHideArea")
            ck(csArea !== null, "cursor: a click-transparent cursor overlay (objectName 'cursorHideArea') must be mounted")
            harness._csShell = csShell
            harness._csArea = csArea

            // -- 13. F5 END OF VOLUME: pressing forward on the last page ANNOUNCES the end instead
            // of going silent. Silence there is indistinguishable from a dropped input — you press
            // again, harder, and wonder whether the reader is stuck. --
            var f5Store = fakeStoreF5
            f5Store.pages = fivePages()
            fakeCoreF5.unitIdentity = true      // honest unit math — see the FakeCore note
            var f5Shell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-f5", "seriesTitle": "EndOfVolume", "seriesCover": "file:///f/f5.png",
                "core": fakeCoreF5, "progress": fakeProgF5, "pageStore": f5Store,
                "seriesRecords": freshRecords('{"s-f5":{"layout":"paired_pages"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],     // ONLY chapter -> no next
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            var f5Toast = byName(f5Shell, "hudToastText")
            ck(f5Toast !== null, "F5: the HUD toast text must be reachable (objectName 'hudToastText')")
            ck(f5Shell.hasNext === false, "F5: a single-chapter fixture must report hasNext=false, got " + f5Shell.hasNext)

            ck(f5Shell.mode === "double_page",
               "F5: fixture must mount in double_page, got '" + f5Shell.mode + "' (max=" + f5Shell.max + ")")

            // mid-book: a normal page turn must stay SILENT
            f5Shell.currentPage = 1
            var f5Before = f5Toast ? f5Toast.text : ""
            f5Shell.pageNext()
            ck(f5Shell.currentPage > 1, "F5: a mid-book pageNext must actually turn the page, got "
               + f5Shell.currentPage + " (mode=" + f5Shell.mode + " max=" + f5Shell.max + ")")
            ck(!f5Toast || f5Toast.text === f5Before,
               "F5: a mid-book page turn must NOT toast, got '" + (f5Toast ? f5Toast.text : "") + "'")

            // last page: announce, and with no next entry say only that
            f5Shell.currentPage = f5Shell.max
            f5Shell.pageNext()
            ck(f5Shell.currentPage === f5Shell.max,
               "F5: pageNext at the end must not move past the last page, got " + f5Shell.currentPage)
            ck(f5Toast && f5Toast.text === "End of volume",
               "F5: the end of a volume with no next entry must toast 'End of volume', got '"
               + (f5Toast ? f5Toast.text : "") + "'")

            // ...and when there IS a next entry, it names the binding that actually exists.
            var f5bStore = fakeStoreF5b
            f5bStore.pages = fivePages()
            fakeCoreF5b.unitIdentity = true
            var f5bShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-f5b", "seriesTitle": "EndOfVolumeNext", "seriesCover": "file:///f/f5b.png",
                "core": fakeCoreF5b, "progress": fakeProgF5b, "pageStore": f5bStore,
                "seriesRecords": freshRecords('{"s-f5b":{"layout":"paired_pages"}}'),
                "entryKind": "manga", "western": false,
                // chapters are NEWEST-FIRST (see the crossing checks above): "next" means index-1,
                // toward the newest. The open entry must therefore sit LAST for a next to exist.
                "chapters": [{ "id": "ch2", "number": "2", "name": "" },
                             { "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            var f5bToast = byName(f5bShell, "hudToastText")
            ck(f5bShell.hasNext === true, "F5: a two-chapter fixture on the first must report hasNext=true, got " + f5bShell.hasNext)
            f5bShell.currentPage = f5bShell.max
            f5bShell.pageNext()
            ck(f5bToast && f5bToast.text === "End of volume — Alt+Right for the next",
               "F5: with a next entry the toast must name the REAL binding (Alt+Right, per "
               + "ComicReaderInput's nextEntry), got '" + (f5bToast ? f5bToast.text : "") + "'")

            // -- 14. F2 PER-SERIES STRIP MEASURE: a width set on one series must not re-dress the
            // others. Two shells share ONE seriesRecords store and ONE globalPrefs, which is what
            // makes this a real test of the leak rather than of two isolated objects. --
            var f2Records = freshRecords()
            var f2Prefs = freshPrefs({ stripWidthPct: 78, stripGap: 0 })
            var f2StoreA = fakeStoreF2a, f2StoreB = fakeStoreF2b
            f2StoreA.pages = fivePages(); f2StoreB.pages = fivePages()

            var aShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "series-A", "seriesTitle": "A", "seriesCover": "file:///f/a.png",
                "core": fakeCoreF2a, "progress": fakeProgF2a, "pageStore": f2StoreA,
                "globalPrefs": f2Prefs, "seriesRecords": f2Records,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            // A is dressed narrow by the user
            aShell.setStripLayout(55, 12)
            ck(fakeCoreF2a.lastStripLayout.w === 55 && fakeCoreF2a.lastStripLayout.g === 12,
               "F2: setStripLayout must reach the backend, got " + JSON.stringify(fakeCoreF2a.lastStripLayout))
            var recA = JSON.parse(f2Records.all)["series-A"]
            ck(recA && recA.sw === 55 && recA.sg === 12,
               "F2: the series record must remember this series' own measure, got " + JSON.stringify(recA))
            ck(f2Prefs.stripWidthPct === 55 && f2Prefs.stripGap === 12,
               "F2: a user-set measure must ALSO seed the global for undressed series, got "
               + f2Prefs.stripWidthPct + "/" + f2Prefs.stripGap)

            // B has no record of its own -> follows the global seed (which A just moved). That is
            // the intended half of the coupling: a width you set still reaches books you have no
            // opinion about.
            var bShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "series-B", "seriesTitle": "B", "seriesCover": "file:///f/b.png",
                "core": fakeCoreF2b, "progress": fakeProgF2b, "pageStore": f2StoreB,
                "globalPrefs": f2Prefs, "seriesRecords": f2Records,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(fakeCoreF2b.lastStripLayout.w === 55 && fakeCoreF2b.lastStripLayout.g === 12,
               "F2: a series with NO record must follow the global seed, got "
               + JSON.stringify(fakeCoreF2b.lastStripLayout))
            var recBopen = JSON.parse(f2Records.all)["series-B"]
            ck(recBopen === undefined || recBopen === null || recBopen.sw === undefined,
               "F2: merely OPENING a series must not invent a width opinion for it, got "
               + JSON.stringify(recBopen))

            // B is dressed wide by the user...
            bShell.setStripLayout(92, 4)
            ck(JSON.parse(f2Records.all)["series-B"].sw === 92,
               "F2: B must record its own measure, got "
               + JSON.stringify(JSON.parse(f2Records.all)["series-B"]))
            // ...and A must be UNTOUCHED. This is the whole point of the task.
            var recA2 = JSON.parse(f2Records.all)["series-A"]
            ck(recA2.sw === 55 && recA2.sg === 12,
               "F2: dressing series B must NOT re-dress series A, got " + JSON.stringify(recA2))

            // ...and re-opening A restores A's own measure, not B's or the global's.
            fakeCoreF2a.lastStripLayout = null
            aShell.seriesId = "series-A"
            aShell._applySeriesPrefs()
            ck(fakeCoreF2a.lastStripLayout && fakeCoreF2a.lastStripLayout.w === 55,
               "F2: re-opening A must restore A's own 55, not B's 92 or the global, got "
               + JSON.stringify(fakeCoreF2a.lastStripLayout))

            // A reading-mode change must not silently invent/drop a width record.
            var beforeMode = JSON.parse(f2Records.all)["series-A"].sw
            aShell._saveSeriesPrefs()
            ck(JSON.parse(f2Records.all)["series-A"].sw === beforeMode,
               "F2: saving series prefs without an explicit measure must PRESERVE the stored width, got "
               + JSON.stringify(JSON.parse(f2Records.all)["series-A"]))

            // -- 15. E6 HIDE FLUSHES THE BOOK RECORD: hiding is LEAVING. The 800ms entrySave
            // debounce may still be pending, so a spread override or coupling nudge just made was
            // only ever in memory - close the app in that window and it is gone. Asserted with NO
            // deferred phase on purpose: the whole point is that it lands immediately. --
            var hfRecords = freshRecords()
            var hfStore = fakeStoreHF
            hfStore.pages = fivePages()
            // The harness root is `visible: false`, so every shell it parents is ALREADY invisible
            // and assigning visible=false fires no change at all - the sibling hide test above only
            // asserts negatives, so it never noticed. The root is made visible for the length of
            // this check so the hide is a REAL true->false transition. (Offscreen platform: nothing
            // is drawn either way.)
            harness.visible = true
            var hfShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-hide", "seriesTitle": "Hide", "seriesCover": "file:///f/h.png",
                "core": fakeCoreHF, "progress": fakeProgHF, "pageStore": hfStore,
                "entryRecords": hfRecords,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            // a change the user just made, sitting in the backend and NOT yet debounced to disk
            fakeCoreHF.blob = { couplingMode: "manual", couplingPhase: "shifted" }
            ck(JSON.parse(hfRecords.all)["ch1"] === undefined,
               "E6 hide: precondition - nothing filed for this entry yet, got "
               + JSON.stringify(JSON.parse(hfRecords.all)["ch1"]))
            ck(hfShell.visible === true,
               "E6 hide: precondition - the shell must actually BE visible, otherwise hiding it "
               + "fires no change and this check would pass vacuously")

            // -- 16. F3 COUPLING PHASE SEEDS FROM THE SERIES: a chapter you have never opened
            // inherits the phase you set by hand on this series. Without it every new chapter
            // re-ran the auto probe and could land on the opposite phase, so reading a series in
            // order meant re-nudging at nearly every chapter. --
            var f3Records = freshRecords('{"s-f3":{"rm":"manga","cp":"shifted"}}')
            var f3Entries = freshRecords()
            var f3Store = fakeStoreF3
            f3Store.pages = fivePages()
            var f3Shell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-f3", "seriesTitle": "Coupling", "seriesCover": "file:///f/c.png",
                "core": fakeCoreF3, "progress": fakeProgF3, "pageStore": f3Store,
                "seriesRecords": f3Records, "entryRecords": f3Entries,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch9", "number": "9", "name": "" }],
                "chapterId": "ch9", "chapterLabel": "Chapter 9"
            })
            var f3Open = fakeCoreF3.lastOpenEntry
            ck(f3Open && f3Open.persisted && f3Open.persisted.couplingMode === "manual"
               && f3Open.persisted.couplingPhase === "shifted",
               "F3: a never-opened chapter must inherit the series' hand-set phase, got "
               + JSON.stringify(f3Open ? f3Open.persisted : null))

            // ...but an entry that already has its OWN coupling record keeps it - the series value
            // is a seed, not an override.
            var f3bEntries = freshRecords('{"ch8":{"couplingMode":"manual","couplingPhase":"normal"}}')
            var f3bStore = fakeStoreF3b
            f3bStore.pages = fivePages()
            var f3bShell = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-f3", "seriesTitle": "Coupling", "seriesCover": "file:///f/c.png",
                "core": fakeCoreF3b, "progress": fakeProgF3b, "pageStore": f3bStore,
                "seriesRecords": f3Records, "entryRecords": f3bEntries,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch8", "number": "8", "name": "" }],
                "chapterId": "ch8", "chapterLabel": "Chapter 8"
            })
            ck(fakeCoreF3b.lastOpenEntry.persisted.couplingPhase === "normal",
               "F3: an entry with its own coupling record must NOT be overridden by the series seed, got "
               + JSON.stringify(fakeCoreF3b.lastOpenEntry.persisted))

            // a hand nudge records the landed phase onto the SERIES
            fakeCoreF3.couplingState = "auto:normal:1.0"
            f3Shell.nudgeCoupling()
            ck(fakeCoreF3.nudgeCalls === 1, "F3: nudgeCoupling must reach the backend")
            ck(JSON.parse(f3Records.all)["s-f3"].cp === "shifted",
               "F3: a nudge must record the landed phase on the series, got "
               + JSON.stringify(JSON.parse(f3Records.all)["s-f3"]))

            // ...and reset FORGETS it, so the next chapter cannot re-apply what you just reset
            f3Shell.resetCoupling()
            ck(fakeCoreF3.resetCalls === 1, "F3: resetCoupling must reach the backend")
            ck(JSON.parse(f3Records.all)["s-f3"].cp === undefined,
               "F3: reset must DELETE the series phase seed, not store a falsy one, got "
               + JSON.stringify(JSON.parse(f3Records.all)["s-f3"]))

            hfShell.visible = false
            var hfSaved = JSON.parse(hfRecords.all)["ch1"]
            ck(hfSaved !== undefined && hfSaved.couplingPhase === "shifted",
               "E6 hide: hiding the reader must flush the book record IMMEDIATELY (not leave it on "
               + "the 800ms debounce), got " + JSON.stringify(hfSaved))
            harness.visible = false

            // -- 17. LAYOUT vs ORDER are INDEPENDENT, and every saved book survives the split
            // (Task 3, plan 2026-07-28). Hemanth's ruling: layout (Single Page / Paired Pages /
            // Long Strip) is presentation; order (comic LTR / manga RTL) is the physical page
            // ordering; neither moves the other. These checks are written against what a REAL
            // SAVED BOOK must do, not against what the shell happens to do today. --

            // 17a. A series saved by the SHIPPED reader ({"rm":"manga"} plus its strip measure)
            // reopens with the SAME reading experience: same layout, same direction, same measure.
            var t3aRecords = freshRecords('{"s-mig":{"rm":"manga","sw":55,"sg":12}}')
            var t3aStore = fakeStoreT3a
            t3aStore.pages = fivePages()
            var t3a = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-mig", "seriesTitle": "Migrated", "seriesCover": "file:///f/mig.png",
                "core": fakeCoreT3a, "progress": fakeProgT3a, "pageStore": t3aStore,
                "seriesRecords": t3aRecords,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch2", "number": "2", "name": "" }, { "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch2", "chapterLabel": "Chapter 2"
            })
            ck(t3a.layout === "paired_pages" && t3a.order === "rtl",
               "migrate: a shipped {rm:'manga'} record must reopen as paired pages + RTL, got "
               + t3a.layout + "/" + t3a.order)
            ck(t3a.mode === "double_page" && t3a.rtl === true,
               "migrate: the mode/rtl aliases must agree with layout/order, got " + t3a.mode + "/" + t3a.rtl)
            ck(t3a.readingMode === "manga",
               "migrate: the old identity the HUD chips still read must stay 'manga', got " + t3a.readingMode)
            ck(fakeCoreT3a.lastOpenEntry && fakeCoreT3a.lastOpenEntry.direction === "rtl",
               "migrate: the backend must be opened in the migrated ORDER, got "
               + (fakeCoreT3a.lastOpenEntry ? fakeCoreT3a.lastOpenEntry.direction : "<none>"))
            ck(fakeCoreT3a.lastStripLayout && fakeCoreT3a.lastStripLayout.w === 55
               && fakeCoreT3a.lastStripLayout.g === 12,
               "migrate: the series' stored strip measure must survive the migration, got "
               + JSON.stringify(fakeCoreT3a.lastStripLayout))

            // 17b. READING IS NOT WRITING. Opening the book must leave the stored JSON exactly as it
            // was — migrate in memory, re-write only on the next real user change. Otherwise every
            // book anyone merely opens gets silently rewritten by an update.
            var t3aRaw = JSON.parse(t3aRecords.all)["s-mig"]
            ck(t3aRaw && t3aRaw.rm === "manga" && t3aRaw.layout === undefined && t3aRaw.order === undefined,
               "migrate: merely OPENING a legacy book must NOT rewrite its record, got " + JSON.stringify(t3aRaw))

            // 17c. ...and the first real user change writes the NEW shape, keeping everything else.
            t3a.setOrder("ltr")
            var t3aNew = JSON.parse(t3aRecords.all)["s-mig"]
            ck(t3aNew && t3aNew.layout === "paired_pages" && t3aNew.order === "ltr",
               "migrate: a real change must write the new layout/order shape, got " + JSON.stringify(t3aNew))
            ck(t3aNew && t3aNew.rm === undefined,
               "migrate: that write must retire the legacy identity key, got " + JSON.stringify(t3aNew))
            ck(t3aNew && t3aNew.sw === 55 && t3aNew.sg === 12,
               "migrate: the write must PRESERVE the series' strip measure, got " + JSON.stringify(t3aNew))

            // 17d. A legacy STRIP record on a manga series: the layout is kept, and the direction
            // falls to the MANGA lane rather than to the LTR the old identity implied by
            // construction. That implication is the conflation being removed — a manga read in Long
            // Strip must turn the right way the moment its reader switches to Paired Pages.
            var t3bStore = fakeStoreT3b
            t3bStore.pages = fivePages()
            var t3b = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-ls", "seriesTitle": "LegacyStrip", "seriesCover": "file:///f/ls.png",
                "core": fakeCoreT3b, "progress": fakeProgT3b, "pageStore": t3bStore,
                "seriesRecords": freshRecords('{"s-ls":{"rm":"strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t3b.layout === "long_strip", "migrate: a legacy {rm:'strip'} record must stay Long Strip, got " + t3b.layout)
            ck(t3b.order === "rtl",
               "migrate: a legacy strip record on a MANGA series must take the lane's RTL, not the "
               + "LTR the old combined identity implied, got " + t3b.order)
            ck(t3b.readingMode === "strip", "migrate: the old identity must still read 'strip', got " + t3b.readingMode)

            // 17e. ORTHOGONALITY — the whole task. Changing the layout must never move the order,
            // and choosing an order must never move the layout.
            var t3cRecords = freshRecords()
            var t3cStore = fakeStoreT3c
            t3cStore.pages = fivePages()
            var t3c = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-ortho", "seriesTitle": "Ortho", "seriesCover": "file:///f/o2.png",
                "core": fakeCoreT3c, "progress": fakeProgT3c, "pageStore": t3cStore,
                "seriesRecords": t3cRecords, "globalPrefs": freshPrefs(),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch2", "number": "2", "name": "" }, { "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch2", "chapterLabel": "Chapter 2"
            })
            ck(t3c.layout === "paired_pages" && t3c.order === "rtl",
               "ortho: a fresh manga series must start paired + RTL, got " + t3c.layout + "/" + t3c.order)
            t3c.setLayout("long_strip")
            ck(t3c.layout === "long_strip" && t3c.order === "rtl",
               "ortho: switching to Long Strip must NOT flip a manga to left-to-right, got "
               + t3c.layout + "/" + t3c.order)
            t3c.setLayout("single_page")
            ck(t3c.layout === "single_page" && t3c.order === "rtl",
               "ortho: switching to Single Page must NOT flip the order either, got "
               + t3c.layout + "/" + t3c.order)
            t3c.setOrder("ltr")
            ck(t3c.layout === "single_page" && t3c.order === "ltr",
               "ortho: choosing an order must NOT change the layout, got " + t3c.layout + "/" + t3c.order)
            // ...and through the COMPATIBILITY door the HUD/settings sheet still use: picking Strip
            // is a layout choice only, even though the old identity used to bake LTR into it.
            t3c.setOrder("rtl")
            t3c.setReadingMode("strip")
            ck(t3c.layout === "long_strip" && t3c.order === "rtl",
               "ortho: setReadingMode('strip') must change the layout ALONE, got " + t3c.layout + "/" + t3c.order)

            // 17f. Single Page is a first-class PERSISTABLE layout (its surface arrives in Task 4).
            t3c.setLayout("single_page")
            ck(JSON.parse(t3cRecords.all)["s-ortho"].layout === "single_page",
               "single page: the layout must persist per series, got " + JSON.stringify(JSON.parse(t3cRecords.all)["s-ortho"]))
            ck(t3c.mode === "single_page",
               "single page: the compat alias must report itself, never pretend to be a pair, got " + t3c.mode)

            // 17f-2 (Task 4). The layout now has a SURFACE. All three mounts are keyed on `layout` —
            // the persisted truth — not on the derived `mode` alias, so exactly one surface paints for
            // any layout and the rule is the same for all three.
            var t3cPair = byName(t3c, "doubleSurface")
            var t3cSingle = byName(t3c, "singleSurface")
            var t3cStrip = byName(t3c, "stripSurface")
            ck(t3cSingle !== null, "single page: the Single Page surface must be mounted in the shell")
            ck(t3cPair !== null && t3cStrip !== null,
               "single page: all three surfaces must be mounted (pair=" + (t3cPair !== null)
               + " strip=" + (t3cStrip !== null) + ")")
            // The mount contract is read off activeSurface, the ONE property that owns the layout ->
            // surface mapping. NOT off the surfaces' visible: this harness roots its tree invisible,
            // so every child reports visible=false whichever surface is actually mounted.
            ck(t3c.activeSurface === "singleSurface",
               "single page: layout single_page must mount the SINGLE surface, got " + t3c.activeSurface)
            ck(t3cSingle && t3cSingle.currentPage === t3c.currentPage,
               "single page: the surface must be handed the shell page, got " + (t3cSingle ? t3cSingle.currentPage : "<null>"))

            // ...and a page turn WALKS ONE PAGE, snapping to no unit. Before Task 4 this fell through
            // to the strip branch and scrolled an unmounted ListView, so the page never turned at all.
            var t3cBefore = t3c.currentPage
            t3c.pageNext()
            ck(t3c.currentPage === t3cBefore + 1,
               "single page: forward must advance exactly ONE page, got " + t3c.currentPage
               + " from " + t3cBefore)
            t3c.pagePrev()
            ck(t3c.currentPage === t3cBefore,
               "single page: back must step exactly ONE page, got " + t3c.currentPage)

            // the other two layouts still resolve to exactly one surface each through the same rule
            t3c.setLayout("paired_pages")
            ck(t3c.activeSurface === "doubleSurface",
               "mounts: layout paired_pages must mount the PAIR, got " + t3c.activeSurface)
            t3c.setLayout("long_strip")
            ck(t3c.activeSurface === "stripSurface",
               "mounts: layout long_strip must mount the STRIP, got " + t3c.activeSurface)
            t3c.setLayout("single_page")
            ck(t3c.activeSurface === "singleSurface",
               "mounts: layout single_page must mount SINGLE, got " + t3c.activeSurface)

            // 17g. An unknown layout/order is REFUSED, never stored, never wedges the reader.
            t3c.setLayout("guided")
            ck(t3c.layout === "single_page", "refuse: an unknown layout must be ignored, got " + t3c.layout)
            t3c.setLayout("")
            ck(t3c.layout === "single_page", "refuse: an empty layout must be ignored, got " + t3c.layout)
            t3c.setOrder("right_left")
            ck(t3c.order === "rtl", "refuse: an unknown order must be ignored, got " + t3c.order)

            // 17h. A CROSSING keeps the choice. This is what the retired persistedMode/Direction
            // seams were for: load() runs again on every chapter/volume jump, so the record has to
            // carry the choice or the next chapter would open in the lane default.
            t3c.setLayout("long_strip")
            t3c.openEntryById("ch1", false)
            ck(String(t3c.curChapterId) === "ch1", "crossing: fixture must actually cross, got " + t3c.curChapterId)
            ck(t3c.layout === "long_strip" && t3c.order === "rtl",
               "crossing: a chapter jump must keep the chosen layout + order, got " + t3c.layout + "/" + t3c.order)

            // 17i. A corrupt/hand-edited record degrades to the LANE default, never a throw and
            // never a silent direction flip.
            var t3dStore = fakeStoreT3d
            t3dStore.pages = fivePages()
            var t3d = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-bad", "seriesTitle": "Corrupt", "seriesCover": "file:///f/bad.png",
                "core": fakeCoreT3d, "progress": fakeProgT3d, "pageStore": t3dStore,
                "seriesRecords": freshRecords('{"s-bad":{"layout":"bogus","order":"sideways"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t3d.layout === "paired_pages" && t3d.order === "rtl",
               "corrupt: an unreadable record must open at the manga lane default, got "
               + t3d.layout + "/" + t3d.order)

            // 17j. The GLOBAL last-choice is split in two as well — and a LEGACY global written by
            // the shipped reader (the combined identity) is still understood on the first launch
            // after the update, so nobody's taste is forgotten by the upgrade itself.
            var t3eStore = fakeStoreT3e
            t3eStore.pages = fivePages()
            var t3e = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-glob", "seriesTitle": "GlobalSeed", "seriesCover": "file:///f/g.png",
                "core": fakeCoreT3e, "progress": fakeProgT3e, "pageStore": t3eStore,
                "globalPrefs": freshPrefs({ readingMode: "strip" }),    // legacy global only
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t3e.layout === "long_strip",
               "global: a LEGACY global identity must still seed an untouched series, got " + t3e.layout)
            var t3fStore = fakeStoreT3f
            t3fStore.pages = fivePages()
            var t3f = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-glob2", "seriesTitle": "GlobalSeed2", "seriesCover": "file:///f/g2.png",
                "core": fakeCoreT3f, "progress": fakeProgT3f, "pageStore": t3fStore,
                "globalPrefs": freshPrefs({ layout: "single_page", order: "rtl" }),
                "entryKind": "manga", "western": true,        // western lane default would be LTR
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t3f.layout === "single_page" && t3f.order === "rtl",
               "global: the split global last-choice must beat the lane default, got "
               + t3f.layout + "/" + t3f.order)

            // -- 18. THE ONE OVERLAY COORDINATOR (Task 5, plan 2026-07-28). Hemanth's approved
            // interaction contract: only ONE temporary surface may be open at a time, the comic
            // never shifts to make room for it, and Escape resolves ONE layer at a time and never
            // leaves the book. The chrome only raises intents; THIS is the single place that
            // decides, so two surfaces can never both believe they are open. --
            var ovShell = t3c
            ovShell.activeOverlay = ""
            ovShell.chromeVisible = true

            ovShell.openOverlay("pages")
            ck(ovShell.activeOverlay === "pages", "overlay: Pages must take ownership, got '" + ovShell.activeOverlay + "'")
            // asking for a DIFFERENT surface replaces the open one — never stacks two
            ovShell.openOverlay("image")
            ck(ovShell.activeOverlay === "image", "overlay: a second surface must REPLACE the first, got '" + ovShell.activeOverlay + "'")
            // re-asking for the surface that is already open closes it: the command that raised a
            // surface is also the way back out
            ovShell.openOverlay("image")
            ck(ovShell.activeOverlay === "", "overlay: re-asking for the open surface must close it, got '" + ovShell.activeOverlay + "'")
            // opening a surface always brings the chrome with it (the surface hangs off the chrome)
            ovShell.chromeVisible = false
            ovShell.openOverlay("layout")
            ck(ovShell.chromeVisible === true, "overlay: opening a surface must bring the chrome back")

            // Escape, layer by layer. Layer 1: the open surface.
            ovShell.closeTop()
            ck(ovShell.activeOverlay === "" && ovShell.chromeVisible === true,
               "escape: layer 1 closes the surface and KEEPS the chrome, got '" + ovShell.activeOverlay
               + "' chrome=" + ovShell.chromeVisible)
            // Layer 2: with nothing open, Escape toggles the chrome — both ways.
            ovShell.closeTop()
            ck(ovShell.chromeVisible === false, "escape: layer 2 hides the chrome, got " + ovShell.chromeVisible)
            ovShell.closeTop()
            ck(ovShell.chromeVisible === true, "escape: layer 2 brings the chrome back, got " + ovShell.chromeVisible)
            // Layer 3 DOES NOT EXIST. "never unexpectedly leave the book" — Back is the only
            // reader-to-library action, and no number of Escapes may become a second one.
            var leftTheBook = false
            ovShell.backRequested.connect(function () { leftTheBook = true })
            ovShell.chromeVisible = false
            ovShell.closeTop(); ovShell.closeTop(); ovShell.closeTop()
            ck(leftTheBook === false, "escape: Escape must NEVER exit the book, however many times it is pressed")

            // The settings sheet is the TOP layer while it is up (it still exists until Task 12).
            ovShell.settingsRequested()
            ck(ovShell.modalOpen === true, "escape: precondition - the settings sheet must be open")
            ovShell.activeOverlay = "pages"
            ovShell.closeTop()
            ck(ovShell.activeOverlay === "pages",
               "escape: the sheet closes FIRST and leaves the surface beneath it alone, got overlay='"
               + ovShell.activeOverlay + "'")
            // ...and the reader is STILL modal, because the filmstrip beneath the sheet is a real
            // surface now (Task 6) rather than an ownership token with no pixels. One more Escape
            // takes that layer too and hands the keyboard back — which is the proof the first
            // Escape consumed the SHEET layer and not this one.
            ck(ovShell.modalOpen === true,
               "escape: with the filmstrip still up the reader stays modal, got " + ovShell.modalOpen)
            ovShell.closeTop()
            ck(ovShell.activeOverlay === "" && ovShell.modalOpen === false,
               "escape: the next Escape closes the filmstrip and hands the keyboard back, got overlay='"
               + ovShell.activeOverlay + "' modal=" + ovShell.modalOpen)

            // The commands that have no surface yet are honest, not faked: they really do take
            // ownership, so the command goes gold and Escape gives it back. Nothing pretends.
            ovShell.openOverlay("loupe")
            ck(ovShell.activeOverlay === "loupe", "overlay: Loupe takes ownership even before its surface lands")
            ovShell.closeTop()
            ck(ovShell.activeOverlay === "", "overlay: ...and Escape gives it straight back")

            // -- 19. THE PAGES FILMSTRIP (Task 6) — the first of the four temporary surfaces to get
            // real pixels. The overlay itself is pinned by tests/comicreader_overlays_harness.qml;
            // THIS pins the shell half: that it is mounted, that it is fed the shell's own facts,
            // and — the rule that matters most — that the SHELL is the only thing able to navigate,
            // so every way out that is not a thumbnail leaves the reading position exactly alone. --
            var film = byName(ovShell, "pagesOverlay")
            ck(film !== null, "filmstrip: the Pages overlay (objectName 'pagesOverlay') must be mounted in the shell")

            ovShell.activeOverlay = ""
            ck(film.open === false, "filmstrip: closed while no surface is open, got " + film.open)
            ck(ovShell.modalOpen === false, "filmstrip: a closed filmstrip must not hold the keyboard")

            ovShell.openOverlay("pages")
            ck(film.open === true, "filmstrip: the Pages command must open it, got " + film.open)
            // An open filmstrip owns the keyboard (ComicReaderInput gates everything but Escape on
            // modalOpen) and holds the chrome awake (ComicReaderHud._holdChrome) — the 2.5s sleep
            // must never pull the strip out from under the reader's hand.
            ck(ovShell.modalOpen === true, "filmstrip: an OPEN filmstrip must make the reader modal")

            // the shell's facts, plumbed in — not re-derived inside the overlay
            ck(film.pageCount === ovShell.max, "filmstrip: pageCount must be the shell's max, got " + film.pageCount)
            ck(film.currentPage === ovShell.currentPage, "filmstrip: currentPage must track the shell, got " + film.currentPage)
            ck(film.order === ovShell.order, "filmstrip: order must track the shell, got '" + film.order + "'")

            // ONE bookmark list feeds BOTH marks. The HUD's rail ticks bind to reader.liveBookmarks
            // (see the ComicReaderHud mount) and so does this; the rail's tick geometry is pinned by
            // tests/comicreader_chrome_harness.qml. A toggle therefore moves both together.
            ovShell.currentPage = 2
            ovShell.bookmarkToggleRequested()
            ck(ovShell.liveBookmarks.length === 1,
               "filmstrip: fixture - a bookmark toggle must reach the core, got " + JSON.stringify(ovShell.liveBookmarks))
            ck(deepEqual(film.bookmarks, ovShell.liveBookmarks),
               "filmstrip: the filmstrip's marks must read the SAME live list as the rail's ticks, got "
               + JSON.stringify(film.bookmarks) + " vs " + JSON.stringify(ovShell.liveBookmarks))

            // T and the Pages command are the same door (the shell's thumbnailsRequested wiring)
            ovShell.activeOverlay = ""
            ovShell.thumbnailsRequested()
            ck(ovShell.activeOverlay === "pages" && film.open === true,
               "filmstrip: T must open the same surface the Pages command does, got '" + ovShell.activeOverlay + "'")

            // JUMP: a thumbnail goes there and gives the screen back, in one move.
            ovShell.currentPage = 1
            film.activateIndex(2)
            ck(ovShell.currentPage === 3,
               "filmstrip: activating index 2 must move the reader to page 3, got " + ovShell.currentPage)
            ck(ovShell.activeOverlay === "" && film.open === false,
               "filmstrip: a jump must also dismiss, got '" + ovShell.activeOverlay + "'")

            // DISMISS WITHOUT MOVING — the whole point of the surface being temporary. Three doors,
            // all of them must leave the page exactly where it was.
            //
            // The page is parked at 4 ON PURPOSE, not left wherever the jump above put it. A
            // dismissal that secretly navigates almost always navigates to page 1 or to the centred
            // page, and starting from either of those would let the mutation pass unnoticed —
            // measured: with the shell's onDismissRequested mutated to goToPageIndex(1), these three
            // checks passed vacuously until this line existed.
            ovShell.currentPage = 4
            ovShell.openOverlay("pages")
            var pageBeforeDismiss = ovShell.currentPage
            ck(pageBeforeDismiss === 4, "filmstrip: fixture - park the reader off page 1 before the dismiss checks, got " + pageBeforeDismiss)
            film.dismiss()
            ck(ovShell.activeOverlay === "" && ovShell.currentPage === pageBeforeDismiss,
               "filmstrip: dismiss() must close and NOT move the page, " + pageBeforeDismiss
               + " -> " + ovShell.currentPage)

            ovShell.openOverlay("pages")
            byName(film, "pagesDismissCatcher").tap()
            ck(ovShell.activeOverlay === "" && ovShell.currentPage === pageBeforeDismiss,
               "filmstrip: clicking the comic must close and NOT move the page, " + pageBeforeDismiss
               + " -> " + ovShell.currentPage)

            ovShell.openOverlay("pages")
            ovShell.chromeVisible = true
            ovShell.closeTop()
            ck(ovShell.activeOverlay === "" && ovShell.currentPage === pageBeforeDismiss
               && ovShell.chromeVisible === true,
               "filmstrip: Escape must close and NOT move the page, " + pageBeforeDismiss
               + " -> " + ovShell.currentPage + " chrome=" + ovShell.chromeVisible)

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
        bookmarkDebounceTimer.start()
    }

    // BOOKMARK-DEFERRED phase — entrySave's debounce (800ms, fixed) is longer than the pinned
    // recordDebounceMs used above, so it gets its own trailing timer rather than sharing
    // deferredTimer's 150ms window. Confirms the bookmark toggle in section 10 actually ARMED the
    // persistence door (not just updated the live in-memory list).
    function runBookmarkDeferred() {
        try {
            ck(_mB6Records !== null, "bookmarks: the B6 entryRecords store must have been stashed")
            var rec = _mB6Records ? JSON.parse(_mB6Records.all) : {}
            ck(rec["ch1"] !== undefined && deepEqual(rec["ch1"].bookmarks, [2]),
               "bookmarks: a bookmark change must ARM the entrySave debounce and file it under the entry, got " + JSON.stringify(rec))
        } catch (e) {
            failures.push("exception during bookmark-deferred checks: " + e.message)
        }
        cursorDeferredTimer.start()
    }

    // CURSOR-DEFERRED phase — after cursorIdleMs (pinned to 25ms) has elapsed with the chrome
    // away: the arrow must have blanked. Then a poke (real activity) must clear it immediately,
    // and — separately — the chrome being up must hold the arrow even while idle.
    function runCursorDeferred() {
        try {
            var s = harness._csShell, area = harness._csArea
            ck(s !== null, "cursor: the pinned-interval shell must have been stashed")
            if (s) {
                s.chromeVisible = false
                ck(s._cursorIdle === true,
                   "cursor: _cursorIdle must go true after cursorIdleMs of stillness (chrome away), got " + s._cursorIdle)
                if (area) {
                    ck(area.enabled === true, "cursor: the overlay must be enabled while the chrome is away, got " + area.enabled)
                    ck(area.cursorShape === Qt.BlankCursor,
                       "cursor: the overlay must show BlankCursor once idle with the chrome away, got " + area.cursorShape)
                }
                s._pokeCursor()
                ck(s._cursorIdle === false, "cursor: _pokeCursor() must clear _cursorIdle immediately, got " + s._cursorIdle)
                if (area) ck(area.cursorShape === Qt.ArrowCursor,
                   "cursor: a poke must restore the ArrowCursor immediately, got " + area.cursorShape)

                // chrome visible: even once idle, the arrow must NOT blank — the overlay must step
                // OUT of the cursor contest entirely (never override the HUD's own PointingHandCursors)
                s.chromeVisible = true
                s._cursorIdle = true
                ck(area === null || area.enabled === true,
                   "cursor: with chromeVisible=true the overlay must remain enabled so it can restore the cursor, got enabled=" + (area ? area.enabled : "<no area>"))
                ck(area === null || area.cursorShape === Qt.ArrowCursor,
                   "cursor: with chromeVisible=true the overlay must explicitly show ArrowCursor, got " + (area ? area.cursorShape : "<no area>"))

                // an open modal (settings sheet) also takes the overlay out of the contest
                s.chromeVisible = false
                s.settingsRequested()
                ck(s.modalOpen === true, "cursor: settingsRequested must open the settings sheet (modalOpen), got " + s.modalOpen)
                ck(area === null || area.enabled === false,
                   "cursor: with a modal open the overlay must also go DISABLED, got enabled=" + (area ? area.enabled : "<no area>"))
                s.closeTopRequested()
                ck(s.modalOpen === false, "cursor: closeTopRequested must close the sheet again")

                // click-transparency: the overlay must never accept a mouse button
                if (area) ck(area.acceptedButtons === Qt.NoButton,
                   "cursor: the overlay must be acceptedButtons:Qt.NoButton (click/wheel transparent), got " + area.acceptedButtons)

                // -- Task 5: ONE wake door. "Any plain mouse movement restores HUD and cursor
                // together." There is real history here — the HUD used to come back while the
                // cursor stayed hidden, so you could see the controls and not your pointer. --
                s.chromeVisible = false
                s._cursorIdle = true
                if (area) ck(area.cursorShape === Qt.BlankCursor,
                   "wake: precondition - the cursor must actually be blanked first, got " + area.cursorShape)
                s.restoreCursorAndChrome()
                ck(s._cursorIdle === false && s.chromeVisible === true,
                   "wake: restoreCursorAndChrome must bring BOTH back, got idle=" + s._cursorIdle
                   + " chrome=" + s.chromeVisible)
                if (area) ck(area.cursorShape === Qt.ArrowCursor,
                   "wake: ...and the arrow must be back immediately, got " + area.cursorShape)

                // ...and the cursor must still be ABLE to sleep afterwards. This is the assertion
                // that fences off the tempting one-line version of the fix: assigning
                // `cursorHideArea.cursorShape = Qt.ArrowCursor` imperatively destroys the binding
                // for good, so the pointer would show an arrow forever after the first mouse move.
                // (The same trap already cost this file's side-scroller thumb its `y:` binding.)
                s.chromeVisible = false
                s._cursorIdle = true
                if (area) ck(area.cursorShape === Qt.BlankCursor,
                   "wake: the cursor must still be able to sleep AFTER a wake (binding intact, not "
                   + "overwritten by an imperative assignment), got " + area.cursorShape)

                // Leaving and returning: the callers HIDE this same instance on back and SHOW it
                // again to reopen, so the cursor clock has to survive the round trip. A stop with no
                // matching re-arm means the pointer never sleeps again for the rest of the session.
                // The harness root is `visible: false`, so every shell it parents is ALREADY
                // invisible and assigning visible=false fires no change at all — the assertions
                // below would pass vacuously. Make the root visible for the length of this probe so
                // the hide is a REAL true->false transition (same trick as the E6 hide check).
                harness.visible = true
                ck(s.visible === true,
                   "wake: precondition - the reader must actually BE visible, else hiding it fires "
                   + "no change and this check would prove nothing")
                s._cursorIdle = true                      // the pointer had gone to sleep mid-read
                s.visible = false
                ck(s._cursorIdle === false, "wake: hiding the reader must hand the arrow back, got " + s._cursorIdle)
                s._cursorIdle = true
                s.visible = true
                ck(s._cursorIdle === false, "wake: showing it again must leave the arrow up, got " + s._cursorIdle)
                ck(cursorRearmTimer.running === false, "wake: precondition - the re-arm probe timer is idle")
                harness._csRearm = s
                cursorRearmTimer.start()
                return                     // report() moves to the re-arm probe below
            }
        } catch (e) {
            failures.push("exception during cursor-deferred checks: " + e.message)
        }
        report()
    }

    // CURSOR RE-ARM probe — the second half of the hide/show round trip. After the reader comes
    // back on screen the cursor clock must be TICKING again, not merely reset: a stopped clock
    // looks identical to a running one at the instant you check it, and only shows itself a few
    // seconds later when the pointer refuses to sleep. Pinned to the same 25ms cursorIdleMs.
    property var _csRearm: null
    function runCursorRearm() {
        try {
            var s = harness._csRearm
            ck(s !== null, "wake: the re-arm shell must have been stashed")
            if (s) {
                s.chromeVisible = false
                ck(s._cursorIdle === true,
                   "wake: after a hide/show round trip the cursor clock must still be RUNNING, so "
                   + "the pointer sleeps again after cursorIdleMs, got _cursorIdle=" + s._cursorIdle)
            }
        } catch (e) {
            failures.push("exception during cursor re-arm checks: " + e.message)
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
    FakeCore { id: fakeCoreR2 }  FakeProgress { id: fakeProgR2 }  FakePageStore { id: fakeStoreR2 }
    FakeCore { id: fakeCoreR3 }  FakeProgress { id: fakeProgR3 }  FakePageStore { id: fakeStoreR3 }
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
    FakeCore { id: fakeCoreB5 }  FakeProgress { id: fakeProgB5 }  FakePageStore { id: fakeStoreB5 }
    FakeCore { id: fakeCoreB6 }  FakeProgress { id: fakeProgB6 }  FakePageStore { id: fakeStoreB6 }
    FakeCore { id: fakeCoreVP }  FakeProgress { id: fakeProgVP }  FakePageStore { id: fakeStoreVP }
    FakeCore { id: fakeCoreCS }  FakeProgress { id: fakeProgCS }  FakePageStore { id: fakeStoreCS }
    FakeCore { id: fakeCoreF5 }  FakeProgress { id: fakeProgF5 }  FakePageStore { id: fakeStoreF5 }
    FakeCore { id: fakeCoreF5b } FakeProgress { id: fakeProgF5b } FakePageStore { id: fakeStoreF5b }
    FakeCore { id: fakeCoreF2a } FakeProgress { id: fakeProgF2a } FakePageStore { id: fakeStoreF2a }
    FakeCore { id: fakeCoreF2b } FakeProgress { id: fakeProgF2b } FakePageStore { id: fakeStoreF2b }
    FakeCore { id: fakeCoreHF }  FakeProgress { id: fakeProgHF }  FakePageStore { id: fakeStoreHF }
    FakeCore { id: fakeCoreF3 }  FakeProgress { id: fakeProgF3 }  FakePageStore { id: fakeStoreF3 }
    FakeCore { id: fakeCoreF3b } FakeProgress { id: fakeProgF3b } FakePageStore { id: fakeStoreF3b }
    // --- layout/order split + legacy migration (section 17, Task 3) ---
    FakeCore { id: fakeCoreT3a } FakeProgress { id: fakeProgT3a } FakePageStore { id: fakeStoreT3a }
    FakeCore { id: fakeCoreT3b } FakeProgress { id: fakeProgT3b } FakePageStore { id: fakeStoreT3b }
    FakeCore { id: fakeCoreT3c } FakeProgress { id: fakeProgT3c } FakePageStore { id: fakeStoreT3c }
    FakeCore { id: fakeCoreT3d } FakeProgress { id: fakeProgT3d } FakePageStore { id: fakeStoreT3d }
    FakeCore { id: fakeCoreT3e } FakeProgress { id: fakeProgT3e } FakePageStore { id: fakeStoreT3e }
    FakeCore { id: fakeCoreT3f } FakeProgress { id: fakeProgT3f } FakePageStore { id: fakeStoreT3f }

    // fires the deferred phase after the pinned 20ms record debounce has elapsed
    Timer { id: deferredTimer; interval: 150; running: false; onTriggered: harness.runDeferred() }
    // fires after entrySave's fixed 800ms debounce interval elapses
    Timer { id: bookmarkDebounceTimer; interval: 900; running: false; onTriggered: harness.runBookmarkDeferred() }
    // fires after the pinned 25ms cursorIdleMs has elapsed
    Timer { id: cursorDeferredTimer; interval: 60; running: false; onTriggered: harness.runCursorDeferred() }
    // ...and again, after the hide/show round trip, to prove the clock is still running
    Timer { id: cursorRearmTimer; interval: 60; running: false; onTriggered: harness.runCursorRearm() }

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
