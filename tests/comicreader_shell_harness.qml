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
        // Task 11: per-page error verdicts, 0-based page -> wire code. pageInfo answers from HERE
        // when the page has one, so a fixture can make exactly one page broken while the rest of the
        // book stays healthy — which is the whole shape of "keeps surrounding pages usable".
        property var pageErrors: ({})
        function pageInfo(page) {
            var e = pageErrors[page]
            if (e !== undefined) {
                var m = {}
                for (var k in fakePageInfo) m[k] = fakePageInfo[k]
                m.error = String(e)
                return m
            }
            return fakePageInfo
        }
        // Task 11 retry: the real core clears the page's verdict, drops the decode coordinator's
        // failure memo and re-queues THAT page. The fake mirrors the observable half — the verdict
        // comes down and the signal fires — so the shell's routing is exercised rather than assumed.
        property var retryCalls: []
        signal pageRetried(int page)
        function retryPage(page) {
            retryCalls.push(page)
            delete pageErrors[page]
            pageRetried(page)
        }
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
        // Task 8: the Auto-scroll SPEED seed. There is deliberately NO key for whether it was
        // running — that is session-only, and a fake carrying one would let a bug pass.
        property real   autoScrollSpeed: 1.0
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
    // Task 8 needs several throwaway backends/stores in one block; factories keep that from adding
    // a named singleton per scenario at the top of the file.
    Component { id: coreComp; FakeCore {} }
    Component { id: storeComp; FakePageStore {} }
    // Task 11 adds several throwaway sinks too — the progress spy is now a per-scenario object
    // rather than a named singleton, for the same reason the cores and stores already are.
    Component { id: progComp; FakeProgress {} }
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
    property int _burstBefore: 0
    property var _navShell: null
    property var _navProg: null
    property int _navBefore: 0
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

            // ===== PROGRESS: ONLY A PRESENTATION RECORDS (Task 11) =====
            // The defect this replaces, in Hemanth's terms: flicking through pages recorded
            // positions he never actually saw, so coming back landed in the wrong place. Navigating
            // is a REQUEST; the record now waits until a surface says the page is really on screen.
            //
            // The negative is asserted directly and it is asserted ACROSS THE DEBOUNCE, in the
            // deferred phase below — checking it synchronously would pass against the OLD code too,
            // because the old code debounced as well. What has to be true is that nothing ever
            // reaches the sink, not that nothing reached it yet.
            var recBefore = mProg.records.length
            mShell.currentPage = 3
            ck(mProg.records.length === recBefore,
               "page change must not record synchronously, got " + mProg.records.length + " (was " + recBefore + ")")
            var expectPageRec = {
                "id": "s1", "kind": "manga", "caption": "Contract Series", "title": "Contract Series",
                "sub": "Chapter 1", "cover": "file:///f/cover.png",
                "c1": "#3a2f55", "c2": "#15111f",
                "progress": 0.6,
                "resume": { "chapterId": "ch1", "page": 3, "scrollFrac": 0, "pageFraction": 0,
                            "maxSeen": 3, "finished": false }
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

            // EVERY COMMAND THE CHROME CAN RAISE MUST ACTUALLY MOUNT SOMETHING.
            //
            // This is the gate the reader did not have, and it is the one that would have caught
            // Hemanth's 2026-08-01 "the hud doesn't auto dissapear" before he did. Between Task 5
            // (where all six commands went live) and Task 9 (where the Loupe's surface landed),
            // `openOverlay("loupe")` set the shell's INTENT and mounted nothing — and the HUD's
            // auto-hide hold read that intent, so pressing Loupe pinned the HUD and the cursor awake
            // for the whole session with no visible surface to dismiss. The old assertion here
            // BLESSED that state ("Loupe takes ownership even before its surface lands"), which is
            // exactly why a full green chrome gate shipped the defect.
            //
            // The hold now reads `modalOpen` (a surface that really came up), so the invariant that
            // matters is this one: every name the chrome raises must reach modalOpen. A name that
            // does not is a command the reader cannot see, and the gate says so by name.
            var raisable = ["pages", "image", "layout", "loupe"]
            for (var ri = 0; ri < raisable.length; ri++) {
                var rname = raisable[ri]
                ovShell.activeOverlay = ""
                ck(ovShell.modalOpen === false,
                   "overlay: precondition - nothing modal before raising '" + rname + "'")
                ovShell.openOverlay(rname)
                ck(ovShell.activeOverlay === rname,
                   "overlay: '" + rname + "' must take ownership, got '" + ovShell.activeOverlay + "'")
                ck(ovShell.modalOpen === true,
                   "overlay: '" + rname + "' must MOUNT a surface (modalOpen), not just claim the "
                   + "intent - a command with no surface pins the HUD and the cursor awake forever")
                ovShell.closeTop()
                ck(ovShell.activeOverlay === "" && ovShell.modalOpen === false,
                   "overlay: Escape must give '" + rname + "' straight back, got '"
                   + ovShell.activeOverlay + "' modal=" + ovShell.modalOpen)
            }

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

            // -- 20. AUTO-SCROLL + THE LAYOUT MENU (Task 8). --
            // The two things this task is judged on, in Hemanth's own words: the portrait width
            // ("one of the most important features is the potrait width in autoscroll. I hope
            // you're not forgetting about that", then 78% confirmed by name) and the never-resize
            // rule ("Starting or resuming Auto-scroll must never resize the page"). Everything
            // below is one of those two, or one of the pause sources the design enumerates.
            var asPrefs = freshPrefs()
            var asRecords = freshRecords()
            var asCore = coreComp.createObject(harness)
            var asStore = storeComp.createObject(harness, { "pages": fivePages() })
            var asShell = makeShell({
                // A REAL SIZE, unlike the other scenarios here: the anchor seam below reads the
                // command row's laid-out geometry, and a zero-width chrome has none to read.
                "width": 1000, "height": 700,
                "seriesId": "s-auto", "seriesTitle": "AutoScroll", "seriesCover": "file:///f/a.png",
                "core": asCore, "progress": null, "pageStore": asStore,
                "globalPrefs": asPrefs, "seriesRecords": asRecords,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" },
                             { "id": "ch0", "number": "0", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            asShell.setLayout("long_strip")

            // -- 20a. SESSION-ONLY, and it always restores PAUSED. --
            ck(asShell.autoScrollRunning === false,
               "auto: a freshly opened book must NOT be moving — nobody opens a book to a moving page")
            ck(asShell.autoScrollSpeed === 1.0, "auto: the default speed is 1.0, got " + asShell.autoScrollSpeed)

            // -- 20b. START refuses where there is nothing to move. --
            asShell.setLayout("paired_pages")
            asShell.startAutoScroll()
            ck(asShell.autoScrollRunning === false,
               "auto: Auto-scroll must refuse to start outside Long Strip, got " + asShell.autoScrollRunning)
            asShell.setLayout("long_strip")
            asShell.startAutoScroll()
            ck(asShell.autoScrollRunning === true, "auto: Start must run in Long Strip, got " + asShell.autoScrollRunning)

            // -- 20c. THE RULE: STARTING OR RESUMING NEVER RESIZES THE PAGE. --
            // The width is a readback off the backend, and the backend's setter is counted: a
            // resize could only happen through it.
            ck(asShell.stripWidthPct === 78, "auto: the fixture starts at the approved 78%")
            var wBefore = asShell.stripWidthPct
            var gBefore = asShell.stripGap
            var layoutCallsBefore = asCore.lastStripLayout
            asShell.pauseAutoScroll()
            asShell.startAutoScroll()              // RESUME — the case the rule names twice
            asShell.setAutoScrollSpeed(2.5)
            asShell.setAutoScrollSpeed(0.5)
            asShell.toggleAutoScroll()
            asShell.toggleAutoScroll()
            ck(asShell.stripWidthPct === wBefore && asShell.stripGap === gBefore,
               "auto: start / pause / resume / speed must NEVER change the strip width or gap, got "
               + asShell.stripWidthPct + "% gap " + asShell.stripGap)
            ck(asCore.lastStripLayout === layoutCallsBefore,
               "auto: ...and must not touch the backend's strip layout at all, got "
               + JSON.stringify(asCore.lastStripLayout))
            ck(asShell.stripWidthPct === 78, "auto: 78 SURVIVES start/pause/resume and every speed change")

            // -- 20d. EVERY pause source. The approved list: manual wheel/touch/navigation, opening
            // chrome, and any temporary surface. Each is driven through the door a real reader uses
            // and each must leave the motion stopped. --
            var strip = byName(asShell, "stripSurface")
            ck(strip !== null, "auto: the strip surface must be mounted")

            function _armAuto() { asShell.startAutoScroll(); return asShell.autoScrollRunning }

            ck(_armAuto(), "auto: fixture - arm before the wheel check")
            strip.manualNavigation()               // the wheel/trackpad gesture, as the surface fires it
            ck(asShell.autoScrollRunning === false, "auto: a WHEEL gesture must pause immediately")

            ck(_armAuto(), "auto: fixture - arm before the keyboard-scroll check")
            asShell._stripScroll(0.9)              // Space / PageDown
            ck(asShell.autoScrollRunning === false, "auto: keyboard SCROLLING must pause")

            ck(_armAuto(), "auto: fixture - arm before the page-turn check")
            asShell.pageNext()
            ck(asShell.autoScrollRunning === false, "auto: a forward page turn must pause")

            ck(_armAuto(), "auto: fixture - arm before the back page-turn check")
            asShell.pagePrev()
            ck(asShell.autoScrollRunning === false, "auto: a backward page turn must pause")

            // ...and the two page-turn verbs pause ON THEIR OWN, not merely by delegating to the
            // strip scroll. In Long Strip they route through _stripScroll, which pauses too, so the
            // pair above would pass with the verbs' own hooks deleted. The flag is set by hand here
            // for exactly that reason: it isolates the verb from the path it usually takes.
            asShell.setLayout("paired_pages")
            asShell.autoScrollRunning = true
            asShell.pageNext()
            ck(asShell.autoScrollRunning === false, "auto: pageNext must pause on its OWN, not only via the strip scroll")
            asShell.autoScrollRunning = true
            asShell.pagePrev()
            ck(asShell.autoScrollRunning === false, "auto: pagePrev must pause on its OWN")
            asShell.setLayout("long_strip")

            ck(_armAuto(), "auto: fixture - arm before the go-to-page check")
            asShell.goToPageIndex(3)
            ck(asShell.autoScrollRunning === false, "auto: a go-to-page must pause")

            ck(_armAuto(), "auto: fixture - arm before the scrub check")
            asShell.scrubToFraction(0.5)
            ck(asShell.autoScrollRunning === false, "auto: a rail SCRUB must pause")

            ck(_armAuto(), "auto: fixture - arm before the Home check")
            asShell.firstPageNav()
            ck(asShell.autoScrollRunning === false, "auto: Home must pause")

            ck(_armAuto(), "auto: fixture - arm before the End check")
            asShell.lastPageNav()
            ck(asShell.autoScrollRunning === false, "auto: End must pause")

            // ...and every temporary surface, by name. "Pages, Image, Loupe, or another temporary
            // surface pauses it immediately."
            var surfaces = ["pages", "image", "loupe", "layout"]
            for (var si = 0; si < surfaces.length; si++) {
                asShell.activeOverlay = ""
                ck(_armAuto(), "auto: fixture - arm before the " + surfaces[si] + " check")
                asShell.openOverlay(surfaces[si])
                ck(asShell.autoScrollRunning === false,
                   "auto: opening the " + surfaces[si] + " surface must pause immediately")
            }
            asShell.activeOverlay = ""

            // the CHROME coming back pauses; the chrome going away does not (that is the reader
            // being left alone, which is exactly when Auto-scroll earns its keep)
            asShell.chromeVisible = true
            ck(_armAuto(), "auto: fixture - arm before the chrome check")
            asShell.chromeVisible = false
            ck(asShell.autoScrollRunning === true,
               "auto: the chrome SLEEPING must not pause — that is the reader being left alone")
            asShell.chromeVisible = true
            ck(asShell.autoScrollRunning === false, "auto: the chrome coming BACK must pause")

            // a layout switch pauses, and coming back to Long Strip lands PAUSED
            asShell.chromeVisible = true
            ck(_armAuto(), "auto: fixture - arm before the layout-switch check")
            asShell.setLayout("single_page")
            ck(asShell.autoScrollRunning === false, "auto: switching layout must pause")
            asShell.setLayout("long_strip")
            ck(asShell.autoScrollRunning === false,
               "auto: coming BACK to Long Strip must land paused — resume is explicit, never automatic")
            ck(asShell.stripWidthPct === 78, "auto: 78 SURVIVES a layout switch away and back")

            // -- 20e. RESUME IS EXPLICIT. Closing the surface that paused it does not restart it. --
            ck(_armAuto(), "auto: fixture - arm before the resume check")
            asShell.openOverlay("pages")
            ck(asShell.autoScrollRunning === false, "auto: fixture - the surface paused it")
            asShell.openOverlay("pages")           // re-tap = close
            ck(asShell.activeOverlay === "", "auto: fixture - the surface closed")
            ck(asShell.autoScrollRunning === false,
               "auto: closing the surface must NOT resume the motion — resume is explicit")
            asShell.closeTop()

            // ...but pressing Start from inside the OPEN Layout menu works, and closing that menu
            // does not undo it. This is the asymmetry openOverlay carries: opening pauses, closing
            // never does, or the one control that starts the motion could not be used at all.
            asShell.activeOverlay = ""
            asShell.openOverlay("layout")
            asShell.startAutoScroll()
            ck(asShell.autoScrollRunning === true, "auto: Start must work from inside the open Layout menu")
            asShell.openOverlay("layout")          // put the menu away
            ck(asShell.activeOverlay === "" && asShell.autoScrollRunning === true,
               "auto: closing the Layout menu must leave the motion you just started running")
            asShell.pauseAutoScroll()

            // -- 20f. A CROSSING lands paused. The next volume must not inherit the motion. --
            asShell.startAutoScroll()
            ck(asShell.autoScrollRunning === true, "auto: fixture - arm before the crossing check")
            asShell.goPrev(false)   // newest-first: ch0 is the OLDER neighbour of ch1
            ck(asShell.curChapterId === "ch0", "auto: fixture - the crossing actually happened, got " + asShell.curChapterId)
            ck(asShell.autoScrollRunning === false, "auto: a crossing must land the next entry PAUSED")

            // -- 20g. THE SPEED persists per series; the RUNNING state persists nowhere. --
            asShell.setAutoScrollSpeed(1.75)
            ck(Math.abs(asShell.autoScrollSpeed - 1.75) < 1e-9,
               "auto: the speed must be settable, got " + asShell.autoScrollSpeed)
            ck(Math.abs(asPrefs.autoScrollSpeed - 1.75) < 1e-9,
               "auto: a speed change must seed the GLOBAL last-choice, got " + asPrefs.autoScrollSpeed)
            var asRec = JSON.parse(asRecords.all)["s-auto"]
            ck(asRec && Math.abs(asRec.autoScrollSpeed - 1.75) < 1e-9,
               "auto: a speed change must be remembered for THIS SERIES, got " + JSON.stringify(asRec))
            // the running flag has no key anywhere — session-only means session-only
            ck(asRec.autoScrollRunning === undefined && asRec.autoScroll === undefined,
               "auto: the RUNNING state must NEVER reach the series record, got " + JSON.stringify(asRec))
            ck(asPrefs.autoScrollRunning === undefined,
               "auto: ...nor the global prefs")
            asShell.setAutoScrollSpeed(99)
            ck(Math.abs(asShell.autoScrollSpeed - 3.0) < 1e-9, "auto: the speed clamps to 3.0, got " + asShell.autoScrollSpeed)
            asShell.setAutoScrollSpeed(0)
            ck(Math.abs(asShell.autoScrollSpeed - 0.25) < 1e-9, "auto: the speed clamps UP to 0.25, got " + asShell.autoScrollSpeed)
            asShell.setAutoScrollSpeed(1.75)

            // -- 20h. REOPENING THE SERIES: 78 comes back, the speed comes back, the motion does not. --
            var asShell2 = makeShell({
                "width": 1000, "height": 700,
                "seriesId": "s-auto", "seriesTitle": "AutoScroll", "seriesCover": "file:///f/a.png",
                "core": coreComp.createObject(harness), "progress": null,
                "pageStore": storeComp.createObject(harness, { "pages": fivePages() }),
                "globalPrefs": asPrefs, "seriesRecords": asRecords,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(asShell2.stripWidthPct === 78,
               "auto: reopening the series restores the approved 78% width, got " + asShell2.stripWidthPct)
            ck(Math.abs(asShell2.autoScrollSpeed - 1.75) < 1e-9,
               "auto: reopening the series restores the remembered SPEED, got " + asShell2.autoScrollSpeed)
            ck(asShell2.autoScrollRunning === false,
               "auto: reopening the series must ALWAYS land paused, got " + asShell2.autoScrollRunning)

            // -- 20i. THE LAYOUT MENU is mounted on the ONE coordinator and fed the shell's facts. --
            var lp = byName(asShell, "layoutPopover")
            ck(lp !== null, "layout: the Layout popover (objectName 'layoutPopover') must be mounted in the shell")
            asShell.activeOverlay = ""
            ck(lp.open === false, "layout: closed while no surface is open, got " + lp.open)
            ck(asShell.modalOpen === false, "layout: a closed Layout menu must not hold the keyboard")
            asShell.openOverlay("layout")
            ck(lp.open === true, "layout: the Layout command must open it, got " + lp.open)
            ck(asShell.modalOpen === true,
               "layout: an OPEN Layout menu must make the reader modal (it carries live sliders — a page "
               + "turn landing under a drag is the same defect the Image panel closed)")
            ck(lp.layout === asShell.layout, "layout: the menu must read the shell's live layout, got '" + lp.layout + "'")
            ck(lp.stripWidthPct === asShell.stripWidthPct, "layout: ...and the live width, got " + lp.stripWidthPct)
            ck(lp.stripGap === asShell.stripGap, "layout: ...and the live gap, got " + lp.stripGap)
            ck(Math.abs(lp.autoScrollSpeed - asShell.autoScrollSpeed) < 1e-9,
               "layout: ...and the live speed, got " + lp.autoScrollSpeed)

            // the menu's intents reach the shell's real doors
            lp.setPortraitWidth(92)
            ck(asShell.stripWidthPct === 92, "layout: the width control must reach the backend, got " + asShell.stripWidthPct)
            lp.setPortraitWidth(78)
            ck(asShell.stripWidthPct === 78, "layout: ...and back to 78, got " + asShell.stripWidthPct)
            lp.setSpacing(20)
            ck(asShell.stripGap === 20, "layout: the spacing control must reach the backend, got " + asShell.stripGap)
            ck(asShell.stripWidthPct === 78, "layout: a spacing change must PRESERVE the 78% width, got " + asShell.stripWidthPct)
            lp.setSpacing(0)
            var wAtStart = asShell.stripWidthPct
            lp.startAutoScroll()
            ck(asShell.autoScrollRunning === true, "layout: the menu's Start must reach the shell")
            ck(asShell.stripWidthPct === wAtStart,
               "layout: starting from the MENU must not resize the page either, got " + asShell.stripWidthPct)
            lp.pauseAutoScroll()
            ck(asShell.autoScrollRunning === false, "layout: the menu's Pause must reach the shell")
            lp.setSpeed(2.0)
            ck(Math.abs(asShell.autoScrollSpeed - 2.0) < 1e-9, "layout: the menu's speed must reach the shell")
            lp.setLayout("single_page")
            ck(asShell.layout === "single_page", "layout: the menu's layout choice must reach the shell")
            asShell.setLayout("long_strip")
            lp.dismiss()
            ck(asShell.activeOverlay === "", "layout: the menu's dismiss must give the screen back")

            // -- 20j. THE ANCHOR SEAM, serving BOTH popovers (Task 7 deferred it; Task 8 owns it). --
            // Cover drops a panel under its own label, which is the shape Hemanth referenced. Two of
            // the six commands are live READOUTS whose label widths move with the layout and the
            // order, so the anchor has to be published per command and re-published on relayout.
            var cmdBar = byName(asShell, "readerCommandBar")
            ck(cmdBar !== null, "anchor: the command bar must be mounted")
            if (cmdBar) {
                cmdBar.refreshAnchors()
                var aLayout = asShell.commandAnchorX("layout")
                var aImage = asShell.commandAnchorX("image")
                ck(aLayout > 0, "anchor: the Layout command must publish a real anchor, got " + aLayout)
                ck(aImage > 0, "anchor: the Image command must publish one too — ONE seam, BOTH panels, got " + aImage)
                ck(Math.abs(aLayout - aImage) > 1,
                   "anchor: two different commands must anchor in two different places, got "
                   + aLayout + " vs " + aImage)
                ck(asShell.commandAnchorX("nonsense") === -1,
                   "anchor: an unknown command must answer -1 (not a position), got "
                   + asShell.commandAnchorX("nonsense"))
                // DYNAMIC, not a constant: the Layout command is a live readout, so relabelling it
                // moves its centre. This is exactly why Task 7 could not hardcode one.
                asShell.setLayout("paired_pages")     // "Long strip" -> "Paired pages"
                cmdBar.refreshAnchors()
                var aLayout2 = asShell.commandAnchorX("layout")
                ck(Math.abs(aLayout2 - aLayout) > 0.5,
                   "anchor: the anchor must be DYNAMIC — relabelling the Layout command must move it, "
                   + aLayout + " -> " + aLayout2)
                asShell.setLayout("long_strip")
                cmdBar.refreshAnchors()
                // ...and both panels actually consume it.
                var imgPanel = byName(asShell, "imagePopover")
                ck(imgPanel !== null && imgPanel.anchorX === asShell.commandAnchorX("image"),
                   "anchor: the Image panel must be fed its own command's anchor, got "
                   + (imgPanel ? imgPanel.anchorX : "<null>"))
                ck(lp.anchorX === asShell.commandAnchorX("layout"),
                   "anchor: the Layout menu must be fed its own command's anchor, got " + lp.anchorX)
            }

            // -- 21. THE LOUPE (Task 9) — completing the scaffold the reader has carried since
            // Task 5. The lens itself is pinned by tests/comicreader_overlays_harness.qml; THIS pins
            // the half only the shell can answer, and it is the rule the whole feature is judged on:
            //
            //   "never changes page zoom, pan, layout, or reading position"
            //
            // The component cannot break that alone — it owns no reading state and raises one empty
            // intent — so the question is whether the SHELL wires it in a way that can. Drive a
            // whole lens session against the real shell and count every signal, position, zoom,
            // layout and record it could have moved. --
            var lpStore = fakeStoreLp
            lpStore.pages = fivePages()
            loupeStripModel.clear()
            for (var lpi = 0; lpi < 5; lpi++)
                loupeStripModel.append({ pageIndex: lpi, top: lpi * 300, displayWidth: 400,
                                         displayHeight: 300, ready: true, errorCode: 0 })
            fakeCoreLp.stripModel = loupeStripModel
            var lpShell = makeShell({
                "width": 1000, "height": 700, "recordDebounceMs": 20,
                "seriesId": "s-loupe", "seriesTitle": "Loupe", "seriesCover": "file:///f/l.png",
                "core": fakeCoreLp, "progress": fakeProgLp, "pageStore": lpStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            var lens = byName(lpShell, "loupe")
            ck(lens !== null, "loupe: the Loupe (objectName 'loupe') must be mounted in the shell")

            // THE STRIP MUST ACTUALLY BE PAINTING for the rest of this block to mean anything. This
            // harness roots its tree invisible, so `active: visible` resolves false on every mounted
            // surface and each one honestly reports "I am drawing nothing" — which would make every
            // assertion below a comparison of two empty lists. Forcing `active` deliberately breaks
            // that binding, which is safe here and only here: this shell exists for this block.
            lpShell.setLayout("long_strip")
            var strip = byName(lpShell, "stripSurface")
            ck(strip !== null, "loupe: the strip surface must be mounted")
            strip.active = true
            strip.forceRelayout()

            lpShell.activeOverlay = ""
            ck(lens.open === false, "loupe: closed while no surface is open, got " + lens.open)
            ck(lpShell.modalOpen === false, "loupe: a closed Loupe must not hold the keyboard")
            // the shell asks the surfaces NOTHING while the lens is shut — with the strip genuinely
            // painting, an ungated binding would hand back real rows here
            ck(strip.visiblePageRects().length > 0,
               "loupe: fixture - the strip must be painting, or the gate check below proves nothing")
            ck(lpShell.loupePageRects.length === 0,
               "loupe: a closed Loupe must not make the shell interrogate the surfaces, got "
               + lpShell.loupePageRects.length)

            // ---- it opens through the ONE coordinator, from BOTH doors ----
            lpShell.openOverlay("loupe")
            ck(lens.open === true, "loupe: the Loupe command must open it, got " + lens.open)
            ck(lpShell.modalOpen === true,
               "loupe: an OPEN Loupe must make the reader modal — the lens owns the pointer and the "
               + "keyboard while it is up")
            lpShell.activeOverlay = ""
            lpShell.loupeRequested()             // the L key's route (input -> shell -> coordinator)
            ck(lpShell.activeOverlay === "loupe" && lens.open === true,
               "loupe: L must open the SAME surface the Loupe command does, got '" + lpShell.activeOverlay + "'")

            // ---- it is fed exactly what the live surface is drawing ----
            // A plain assignment, not openOverlay(): the lens is already up from the L route above,
            // and openOverlay TOGGLES — re-asking would have closed the very surface under test.
            lpShell.activeOverlay = "loupe"
            var fed = lpShell.loupePageRects
            ck(fed.length > 0,
               "loupe: an open Loupe must be fed the boxes the live surface is painting, got " + fed.length)
            ck(lens.pages === fed, "loupe: ...and the lens must read exactly that list, not a copy")
            ck(JSON.stringify(fed) === JSON.stringify(strip.visiblePageRects()),
               "loupe: ...and it must be the SURFACE's answer, unedited, got " + JSON.stringify(fed))
            if (fed.length) {
                ck(fed[0].width > 0 && fed[0].height > 0 && String(fed[0].url).indexOf("tier=hq") >= 0,
                   "loupe: the fed box must be a real box on the hq tier, got " + JSON.stringify(fed[0]))
                // ...and the lens resolves it back — the two halves of the seam actually meet
                ck(lens.pageAt(fed[0].x + fed[0].width / 2, fed[0].y + fed[0].height / 2) === fed[0].page,
                   "loupe: the lens must resolve the fed box back to its page, got "
                   + lens.pageAt(fed[0].x + fed[0].width / 2, fed[0].y + fed[0].height / 2))
            }
            // ...and the column MOVING re-feeds it, which is the whole Long Strip requirement: the
            // page under a stationary lens changes because the BOOK moved, never because the lens did.
            // A SMALL scroll, deliberately: 100px over 300-tall rows never changes which page is at
            // the viewport centre, so `currentPage` cannot move and the ONLY dependency that can
            // re-drive this binding is the column's own position. (Measured: with a half-book jump
            // instead, dropping contentY from the dependency list still passed — the page change
            // re-drove the binding and the assertion proved nothing about scrolling at all.)
            var pageBeforeScroll = lpShell.currentPage
            var yBeforeScroll = lpShell.loupePageRects[0].y
            strip.haltScrollAt(100)
            ck(lpShell.currentPage === pageBeforeScroll,
               "loupe: fixture - a 100px scroll must NOT change the page, or the re-feed check below "
               + "passes for the wrong reason. Got " + lpShell.currentPage + " (was " + pageBeforeScroll + ")")
            ck(Math.abs(lpShell.loupePageRects[0].y - (yBeforeScroll - 100)) < 0.5,
               "loupe: scrolling the column must re-feed the lens — a lens sampling frozen geometry "
               + "would magnify the wrong part of the page. Got y " + lpShell.loupePageRects[0].y
               + ", want " + (yBeforeScroll - 100))
            strip.haltScrollAt(0)

            // ---- it asks the surface that matches the LAYOUT ----
            ck(lpShell._loupeSurface === strip, "loupe: in Long Strip the lens must ask the STRIP surface")
            lpShell.setLayout("paired_pages")
            ck(lpShell._loupeSurface === byName(lpShell, "doubleSurface"),
               "loupe: in Paired pages the lens must ask the PAIR surface")
            lpShell.setLayout("single_page")
            ck(lpShell._loupeSurface === byName(lpShell, "singleSurface"),
               "loupe: in Single page the lens must ask the SINGLE surface")
            ck(lpShell._loupeSurface !== strip,
               "loupe: ...and it is a real choice, not the strip every time")
            lpShell.setLayout("long_strip")

            // ---- OPENING IT PAUSES AUTO-SCROLL. Already covered by name in section 20, but the
            //      Loupe is the surface the approved rule names FIRST, so it is asserted here too
            //      against the real mounted lens rather than against an ownership token. ----
            lpShell.activeOverlay = ""
            lpShell.setLayout("long_strip")
            lpShell.startAutoScroll()
            ck(lpShell.autoScrollRunning === true, "loupe: fixture - arm the motion before the check")
            lpShell.openOverlay("loupe")
            ck(lpShell.autoScrollRunning === false,
               "loupe: opening the Loupe must pause Auto-scroll immediately")

            // ---- THE WHEEL IS LOCKED OUT OF THE COLUMN while the lens is up ----
            ck(strip !== null && strip.wheelLocked === true,
               "loupe: with the Loupe up the strip's wheel intake must be LOCKED — a notch magnifies "
               + "the lens and must not scroll the column, got " + (strip ? strip.wheelLocked : "<none>"))
            lpShell.activeOverlay = ""
            ck(strip.wheelLocked === false, "loupe: ...and the lock lifts when the lens goes away")
            lpShell.openOverlay("loupe")

            // === THE ONE THAT MATTERS: A WHOLE LENS SESSION MOVES NOTHING ===
            // Every verb the lens has, plus the pointer paths that drive them, against a shell whose
            // navigation signals are all being counted. Anything the Loupe could move is measured
            // BEFORE and AFTER — not merely "no signal fired", because a direct write would fire no
            // signal at all.
            var navCount = 0
            lpShell.backRequested.connect(function () { navCount += 1 })
            lpShell.closeRequested.connect(function () { navCount += 1 })
            lpShell.fullscreenRequested.connect(function () { navCount += 1 })
            lpShell.sourceRequested.connect(function () { navCount += 1 })
            var recBefore = fakeProgLp.records.length
            var openBefore = fakeCoreLp.openCount
            var before = {
                page: lpShell.currentPage, maxSeen: lpShell.maxSeen, frac: lpShell.stripFraction,
                zoom: lpShell.zoomPercent, layout: lpShell.layout, order: lpShell.order,
                width: lpShell.stripWidthPct, gap: lpShell.stripGap, chapter: lpShell.curChapterId,
                contentY: strip.contentY
            }
            var tracker = byName(lens, "loupeTracker")
            ck(tracker !== null, "loupe: fixture - the pointer tracker must exist")
            // follow, pin, drag against the pin, magnify both ways at both clamps, unpin, drag again
            lens.followPointer(400, 300)
            if (tracker) tracker.moved(430, 180)
            lens.clickAt(460, 320)                       // pin
            lens.followPointer(900, 640)                 // ...ignored while pinned
            if (tracker) tracker.moved(120, 500)         // ...and so is the pointer path
            lens.magnifySteps(4)
            lens.magnifySteps(-20)
            lens.setMagnification(3.25)
            if (tracker) tracker.tap(300, 220)           // unpin
            lens.followPointer(520, 410)
            ck(lpShell.currentPage === before.page && lpShell.maxSeen === before.maxSeen,
               "loupe: a lens session must not move the READING POSITION, page " + lpShell.currentPage
               + " (was " + before.page + ")")
            ck(lpShell.stripFraction === before.frac && strip.contentY === before.contentY,
               "loupe: ...nor the column, contentY " + strip.contentY + " (was " + before.contentY + ")")
            ck(lpShell.zoomPercent === before.zoom,
               "loupe: ...nor the page ZOOM, got " + lpShell.zoomPercent + " (was " + before.zoom + ")")
            ck(lpShell.layout === before.layout && lpShell.order === before.order,
               "loupe: ...nor the LAYOUT or the order, got " + lpShell.layout + "/" + lpShell.order)
            ck(lpShell.stripWidthPct === before.width && lpShell.stripGap === before.gap,
               "loupe: ...nor the strip measure, got " + lpShell.stripWidthPct + "/" + lpShell.stripGap)
            ck(lpShell.curChapterId === before.chapter && fakeCoreLp.openCount === openBefore,
               "loupe: ...nor the open ENTRY, got '" + lpShell.curChapterId + "' opens=" + fakeCoreLp.openCount)
            ck(fakeProgLp.records.length === recBefore,
               "loupe: ...and it must not file a Continue record, got "
               + (fakeProgLp.records.length - recBefore) + " new")
            ck(navCount === 0, "loupe: ...and it must raise NO window/navigation signal, got " + navCount)
            // ...and it really did do something, or the block above proves nothing
            ck(Math.abs(lens.magnification - 3.25) < 1e-9 && lens.pinned === false
               && lens.lensX === 520 && lens.lensY === 410,
               "loupe: fixture - the session must actually have driven the lens, got mag "
               + lens.magnification + " pinned " + lens.pinned + " at " + lens.lensX + "," + lens.lensY)

            // ---- and every way OUT gives the screen back without moving anything ----
            lens.dismiss()
            ck(lpShell.activeOverlay === "" && lens.open === false,
               "loupe: the lens's own close action must give the screen back, got '" + lpShell.activeOverlay + "'")
            lpShell.openOverlay("loupe")
            lpShell.closeTop()                            // Escape
            ck(lpShell.activeOverlay === "", "loupe: Escape must close it, got '" + lpShell.activeOverlay + "'")
            lpShell.openOverlay("loupe")
            lpShell.openOverlay("loupe")                  // the Loupe command again
            ck(lpShell.activeOverlay === "", "loupe: the Loupe command must be the way back out too")
            ck(lpShell.currentPage === before.page && lpShell.zoomPercent === before.zoom
               && lpShell.layout === before.layout,
               "loupe: closing it — every way — must leave the book exactly where it was")

            // ================== TASK 11: presentation, resume anchor, damaged pages ==================

            // ===== T11z. THE CARD'S BUTTONS MUST BE REACHABLE BY A REAL CLICK =====
            // The shell's input layer is one full-bleed MouseArea that accepts every left press to
            // resolve the click zones. Qt offers a press to items in reverse paint order and stops
            // at the first that accepts, so with that layer above the surfaces the error card's
            // Retry and Skip would be drawn, hoverable and completely DEAD — a control that lies.
            //
            // Asserted as the ORDERING RULE rather than by faking a click: an offscreen harness has
            // no window to deliver a real press through, so a "click" test here would prove nothing
            // about delivery. What can be pinned is the invariant delivery depends on.
            var t11zStore = storeComp.createObject(harness, {}); t11zStore.pages = fivePages()
            var t11z = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-t11z", "seriesTitle": "Reach", "seriesCover": "file:///f/z.png",
                "core": coreComp.createObject(harness, {}), "progress": progComp.createObject(harness, {}),
                "pageStore": t11zStore,
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            var t11zInput = byName(t11z, "comicInput")
            var t11zStrip = byName(t11z, "stripSurface")
            var t11zPair = byName(t11z, "doubleSurface")
            var t11zSingle = byName(t11z, "singleSurface")
            ck(t11zInput !== null && t11zStrip !== null && t11zPair !== null && t11zSingle !== null,
               "T11z: the input layer and all three surfaces must be findable by objectName")
            if (t11zInput && t11zStrip) {
                ck(t11zInput.z < t11zStrip.z && t11zInput.z < t11zPair.z && t11zInput.z < t11zSingle.z,
                   "T11z: the click-zone layer must sit BENEATH the reading surfaces, or the damaged-"
                   + "page card's Retry/Skip can never receive a press (input z=" + t11zInput.z
                   + ", surfaces z=" + t11zStrip.z + "/" + t11zPair.z + "/" + t11zSingle.z + ")")
            }

            // ===== T11n. A SETTLED shell, kept for the deferred phases, to pin the NEGATIVE =====
            // "Navigating never records" has TWO halves and they fail differently, so both are
            // asserted, and both need a shell that has stopped moving on its own:
            //   * the page you were only SENT to must never appear in the store (the defect
            //     Hemanth reported — coming back to a page he never saw), and
            //   * navigating must not write AT ALL (the disk-churn half; a write that merely
            //     carries the right page is still a QSettings sync per page turn).
            // The second one has to be measured on a shell nothing else is touching: every shell in
            // this harness flips visible once shortly after creation, and hiding legitimately
            // flushes, so a bare count taken during runChecks would read that flush as a violation.
            // Held here, asserted two deferred phases later, by which time the flip is long past.
            var t11nStore = storeComp.createObject(harness, {}); t11nStore.pages = fivePages()
            harness._navProg = progComp.createObject(harness, {})
            harness._navShell = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20,
                "seriesId": "s-t11n", "seriesTitle": "Never Seen", "seriesCover": "file:///f/n.png",
                "core": coreComp.createObject(harness, { "unitIdentity": true }),
                "progress": harness._navProg, "pageStore": t11nStore,
                "seriesRecords": freshRecords('{"s-t11n":{"layout":"single_page"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })

            // ===== T11a. THE STRIP ANCHOR ROUND-TRIPS: page AND the spot inside it =====
            // The approved line: "Long Strip records the current page plus position within that
            // page. Returning to a book should therefore land on the same panel area instead of only
            // the approximate page." So the record has to carry both, and a reopen has to take both
            // back. The two fractions in this fixture are DIFFERENT numbers on purpose — scrollFrac
            // is a fraction of the whole column and pageFraction is a fraction of one page, and a
            // fixture where they matched would pass against code that wrote one into the other.
            var t11aStore = storeComp.createObject(harness, {}); t11aStore.pages = fivePages()
            var t11aCore = coreComp.createObject(harness, {})
            var t11aProg = progComp.createObject(harness, {})
            var t11a = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20,
                "seriesId": "s-t11a", "seriesTitle": "Anchor", "seriesCover": "file:///f/a.png",
                "core": t11aCore, "progress": t11aProg, "pageStore": t11aStore,
                "seriesRecords": freshRecords('{"s-t11a":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t11a.mode === "long_strip", "T11a fixture must be on the strip, got " + t11a.mode)
            t11a.stripFraction = 0.81                 // the column-wide fraction the surface reports
            t11a._onPresented(4, 0.37)                // ...and the WITHIN-page one
            ck(t11a.presentedPage === 4, "T11a presentation must move the presented page, got " + t11a.presentedPage)
            ck(Math.abs(t11a.presentedPageFraction - 0.37) < 1e-9,
               "T11a presentation must carry the within-page fraction, got " + t11a.presentedPageFraction)
            t11a.recordProgress()                     // flush now rather than waiting on the debounce
            var t11aRec = t11aProg.lastRecord ? t11aProg.lastRecord.resume : null
            ck(t11aRec !== null && t11aRec.page === 4,
               "T11a the record must name the PRESENTED page (4), got " + JSON.stringify(t11aRec))
            ck(t11aRec !== null && Math.abs(t11aRec.pageFraction - 0.37) < 1e-9,
               "T11a the record must carry pageFraction 0.37, got " + JSON.stringify(t11aRec))
            ck(t11aRec !== null && Math.abs(t11aRec.scrollFrac - 0.81) < 1e-9,
               "T11a the record must still carry the legacy scrollFrac 0.81 (0.37 is a DIFFERENT "
               + "quantity and must not be written into it), got " + JSON.stringify(t11aRec))

            // ...and the reopen takes both back. A fresh shell reading that record must arm the
            // WITHIN-page fraction, not the legacy column one.
            var t11bStore = storeComp.createObject(harness, {}); t11bStore.pages = fivePages()
            var t11bProg = progComp.createObject(harness, {})
            t11bProg.saved = { "resume": { "chapterId": "ch1", "page": 4, "scrollFrac": 0.81,
                                           "pageFraction": 0.37, "maxSeen": 4 } }
            var t11b = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-t11b", "seriesTitle": "Anchor2", "seriesCover": "file:///f/a.png",
                "core": coreComp.createObject(harness, {}), "progress": t11bProg, "pageStore": t11bStore,
                "seriesRecords": freshRecords('{"s-t11b":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t11b.currentPage === 4, "T11a/round-trip: the page must be restored to 4, got " + t11b.currentPage)
            ck(Math.abs(t11b._pendingPageFraction - 0.37) < 1e-9,
               "T11a/round-trip: the WITHIN-page fraction must be armed (0.37), got " + t11b._pendingPageFraction)
            ck(t11b._stripRestorePending === true, "T11a/round-trip: the restore door must be armed")

            // A record written BEFORE pageFraction existed must resume exactly as it always did:
            // absence is -1 ("no opinion"), which is NOT the same as 0 ("the viewport centre sat on
            // the page's top edge") — 0 would land half a screen higher than the old behaviour.
            var t11cStore = storeComp.createObject(harness, {}); t11cStore.pages = fivePages()
            var t11cProg = progComp.createObject(harness, {})
            t11cProg.saved = { "resume": { "chapterId": "ch1", "page": 4, "scrollFrac": 0.81, "maxSeen": 4 } }
            var t11c = makeShell({
                "width": 640, "height": 480,
                "seriesId": "s-t11c", "seriesTitle": "Legacy", "seriesCover": "file:///f/a.png",
                "core": coreComp.createObject(harness, {}), "progress": t11cProg, "pageStore": t11cStore,
                "seriesRecords": freshRecords('{"s-t11c":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t11c._pendingPageFraction === -1,
               "T11a/legacy: a record with NO pageFraction must arm no within-page opinion (-1), got "
               + t11c._pendingPageFraction)
            ck(Math.abs(t11c._pendingStripFrac - 0.81) < 1e-9,
               "T11a/legacy: ...and must still arm the old column fraction, got " + t11c._pendingStripFrac)

            // ===== T11d. THE ANCHOR SURVIVES A LAYOUT ROUND TRIP =====
            // "Changing layout, order, width, or image settings preserves the visible reading
            // anchor." In Long Strip the visible anchor is a POINT INSIDE a page, so leaving the
            // column and coming back has to bring back both halves of it.
            var t11dStore = storeComp.createObject(harness, {}); t11dStore.pages = fivePages()
            var t11dCore = coreComp.createObject(harness, {})
            var t11d = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20,
                "seriesId": "s-t11d", "seriesTitle": "Reflow", "seriesCover": "file:///f/a.png",
                "core": t11dCore, "progress": progComp.createObject(harness, {}), "pageStore": t11dStore,
                "seriesRecords": freshRecords('{"s-t11d":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            t11d.currentPage = 3
            t11d._onPresented(3, 0.44)
            t11d.setLayout("single_page")
            ck(t11d.currentPage === 3, "T11d: leaving the strip must keep the page, got " + t11d.currentPage)
            ck(t11d._pendingPageFraction === -1,
               "T11d: a paged layout has no column to seek, so nothing may stay armed, got " + t11d._pendingPageFraction)
            // ORDER is the one of the four that reflows nothing — the pair anchor is the same page
            // whichever side it is drawn on — so the anchor must come through untouched.
            t11d.setOrder(t11d.order === "rtl" ? "ltr" : "rtl")
            ck(t11d.currentPage === 3 && t11d.presentedPage === 3,
               "T11d: changing ORDER must not move the anchor, got page " + t11d.currentPage
               + " presented " + t11d.presentedPage)
            t11d.setLayout("long_strip")
            ck(t11d.currentPage === 3, "T11d: returning to the strip must keep the page, got " + t11d.currentPage)
            ck(Math.abs(t11d._pendingPageFraction - 0.44) < 1e-9,
               "T11d: returning to the strip must re-arm the SPOT ON that page (0.44), got "
               + t11d._pendingPageFraction)

            // ...but only while the reader is still on the page it was measured on. A fraction is a
            // statement about ONE page; navigating away and back must not resurrect it, or the
            // reader lands confidently in the wrong place. (This is the same trap section 3b pins
            // for the entry-resume fraction, re-entering through the new arm.)
            t11d.setLayout("paired_pages")
            t11d.goToPageIndex(5)
            var t11dKept = t11d.currentPage
            t11d.setLayout("long_strip")
            ck(t11d._pendingPageFraction === -1,
               "T11d: navigating off the anchor page must retire its fraction, got "
               + t11d._pendingPageFraction + " (armed for page " + t11dKept + ")")

            // IMAGE SETTINGS reflow too — rotation and auto-crop change a page's aspect, so every
            // page below it moves in the column. The anchor must be re-armed against the new geometry.
            var t11eStore = storeComp.createObject(harness, {}); t11eStore.pages = fivePages()
            var t11e = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20, "renderApplyMs": 1,
                "seriesId": "s-t11e", "seriesTitle": "Image", "seriesCover": "file:///f/a.png",
                "core": coreComp.createObject(harness, {}), "progress": progComp.createObject(harness, {}),
                "pageStore": t11eStore,
                "seriesRecords": freshRecords('{"s-t11e":{"layout":"long_strip"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            t11e.currentPage = 2
            t11e._onPresented(2, 0.63)
            t11e._pendingPageFraction = -1            // clear the opening arm so the assertion is honest
            t11e.setRenderProfile({ "rotation": 90 })
            ck(Math.abs(t11e._pendingPageFraction - 0.63) < 1e-9,
               "T11e: an image-settings change must re-arm the visible anchor (0.63), got "
               + t11e._pendingPageFraction)
            ck(t11e._stripRestorePending === true,
               "T11e: ...and open the ONE restore door so the re-land happens after the reflow settles")

            // ===== T11f. A DAMAGED PAGE: retry re-reads, skip moves on, neither marks it read =====
            var t11fStore = storeComp.createObject(harness, {}); t11fStore.pages = fivePages()
            var t11fCore = coreComp.createObject(harness, {})
            t11fCore.unitIdentity = true              // honest unit math, so pageNext/goToPageIndex walk forward
            t11fCore.pageErrors = { 2: "decode_failed" }   // 0-based: page 3 is the broken one
            var t11fProg = progComp.createObject(harness, {})
            var t11f = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20,
                "seriesId": "s-t11f", "seriesTitle": "Damaged", "seriesCover": "file:///f/a.png",
                "core": t11fCore, "progress": t11fProg, "pageStore": t11fStore,
                "seriesRecords": freshRecords('{"s-t11f":{"layout":"single_page"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            ck(t11f.layout === "single_page", "T11f fixture must be Single Page, got " + t11f.layout)
            ck(t11f._pageBroken(3) === true, "T11f page 3 must read as broken")
            ck(t11f._pageBroken(4) === false, "T11f page 4 must read as healthy")

            // RETRY reaches the backend, names THAT page (0-based), and nothing else moves.
            var t11fPageBefore = t11f.currentPage
            t11f.retryPage(3)
            ck(t11fCore.retryCalls.length === 1 && t11fCore.retryCalls[0] === 2,
               "T11f retry must call core.retryPage with the 0-based failed page (2), got "
               + JSON.stringify(t11fCore.retryCalls))
            ck(t11f.currentPage === t11fPageBefore,
               "T11f retry must NOT navigate — it re-reads the page you are on, got " + t11f.currentPage)
            ck(t11f._pageBroken(3) === false,
               "T11f the verdict must come down when the retry is asked for, so the card can give "
               + "way to the placeholder while the re-read runs")

            // SKIP moves past the failed page and does not leave it marked as where the reader is.
            t11fCore.pageErrors = { 2: "decode_failed" }   // still broken: the retry did not repair it
            t11f.goToPageIndex(3)
            t11f._onPresented(3, 0)                        // the placard IS presented — the reader is there
            ck(t11f.presentedPage === 3,
               "T11f a placarded page counts as a POSITION (the reader is genuinely on it), got "
               + t11f.presentedPage)
            var t11fRecBefore = t11fProg.records.length
            t11f.skipPage(3)
            ck(t11f.currentPage === 4,
               "T11f skip must navigate PAST the failed page (3 -> 4), got " + t11f.currentPage)
            ck(t11fProg.records.length === t11fRecBefore,
               "T11f skip is navigation, not presentation — it must record nothing on its own, got "
               + (t11fProg.records.length - t11fRecBefore) + " record(s)")
            t11f._onPresented(4, 0)
            t11f.recordProgress()
            ck(t11fProg.lastRecord && t11fProg.lastRecord.resume.page === 4,
               "T11f after a skip the recorded position is the LANDING page (4), never the failed "
               + "one, got " + JSON.stringify(t11fProg.lastRecord ? t11fProg.lastRecord.resume : null))

            // ...and a broken page is never READ. maxSeen is what makes a volume `finished`, so a
            // volume whose LAST page is damaged must not complete itself just because the reader
            // navigated onto the error card.
            var t11gStore = storeComp.createObject(harness, {}); t11gStore.pages = fivePages()
            var t11gCore = coreComp.createObject(harness, {})
            t11gCore.unitIdentity = true
            t11gCore.pageErrors = { 4: "decode_failed" }   // 0-based: the LAST page (5) is broken
            var t11g = makeShell({
                "width": 640, "height": 480, "recordDebounceMs": 20,
                "seriesId": "s-t11g", "seriesTitle": "Unfinishable", "seriesCover": "file:///f/a.png",
                "core": t11gCore, "progress": progComp.createObject(harness, {}), "pageStore": t11gStore,
                "seriesRecords": freshRecords('{"s-t11g":{"layout":"single_page"}}'),
                "entryKind": "manga", "western": false,
                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
                "chapterId": "ch1", "chapterLabel": "Chapter 1"
            })
            t11g.goToPageIndex(4)
            ck(t11g.maxSeen === 4, "T11g reaching page 4 must advance maxSeen to 4, got " + t11g.maxSeen)
            t11g.goToPageIndex(5)
            ck(t11g.currentPage === 5, "T11g the reader can still GO to the broken last page, got " + t11g.currentPage)
            ck(t11g.maxSeen === 4,
               "T11g a BROKEN page must never advance the read high-water mark (still 4), got " + t11g.maxSeen)
            t11g._onPresented(5, 0)
            t11g.recordProgress()
            ck(t11g.presentedPage === 5,
               "T11g ...but it is still a real POSITION: the record must be able to put the reader "
               + "back on it, got " + t11g.presentedPage)
            var t11gRec = t11g.progress.lastRecord ? t11g.progress.lastRecord.resume : null
            ck(t11gRec !== null && t11gRec.finished === false,
               "T11g a volume whose last page cannot be read must NOT mark itself finished, got "
               + JSON.stringify(t11gRec))

        } catch (e) {
            failures.push("exception during checks: " + e.message)
        }
        // hand off to the deferred phase to observe the DEBOUNCED page-change record + close
        deferredTimer.start()
    }

    // DEFERRED phase — runs after the debounce interval elapses: assert the debounced page-change
    // record fired with the exact §4.1 payload, then assert close flushes immediately + closes.
    // How many records in the sink name `page` as the resume position. Counting THE PAGE rather
    // than the total is what makes these assertions immune to the other flush doors — a hide, a
    // crossing or a shutdown may legitimately write the LAST PRESENTED position at any time, and a
    // bare length comparison would read those as violations. What must never happen is a page the
    // reader was only SENT to turning up in the store at all.
    function recordsNaming(prog, page) {
        var n = 0
        for (var i = 0; i < prog.records.length; i++) {
            var r = prog.records[i]
            if (r && r.resume && r.resume.page === page) n += 1
        }
        return n
    }

    function runDeferred() {
        try {
            // THE NEGATIVE, measured where it counts: page 3 was navigated to and never presented,
            // and the full debounce interval has since elapsed. If navigating still triggered a
            // write, page 3 would be in the store by now. This is the assertion that fails against
            // the pre-Task-11 reader — it is the defect in one line.
            ck(recordsNaming(_mProg, 3) === 0,
               "NAVIGATION ALONE MUST NEVER RECORD — page 3 was requested, never presented, yet the "
               + "store holds " + recordsNaming(_mProg, 3) + " record(s) naming it")

            // ...and now the positive. It is the DISK-BOUND write that is counted, not the function
            // that was called: the count below is read after the debounce has had time to fire, so a
            // writer that only ever queued would fail rather than pass silently.
            _mShell._onPresented(3, 0)
            ck(recordsNaming(_mProg, 3) === 0,
               "presentation must not write synchronously — the write is debounced (the strip "
               + "presents ~12x/sec), got " + recordsNaming(_mProg, 3))

            // THE CHURN HALF, on the settled shell (see T11n). Snapshot, navigate three pages
            // without presenting any of them, and let the next phase check that the store did not
            // move at all. A record that carried the RIGHT page would still be a QSettings sync per
            // page turn, which is what the count catches and the page-name check cannot.
            harness._navBefore = _navProg.records.length
            _navShell.goToPageIndex(2)
            _navShell.goToPageIndex(3)
            _navShell.goToPageIndex(4)
            presentedDeferredTimer.start()
            return
        } catch (e) {
            failures.push("exception during deferred checks: " + e.message)
        }
        bookmarkDebounceTimer.start()
    }

    // PRESENTED-DEFERRED phase — the debounce after the presentation above has now elapsed.
    function runPresentedDeferred() {
        try {
            ck(recordsNaming(_mProg, 3) === 1,
               "ONE presentation must produce EXACTLY ONE record for that page, got "
               + recordsNaming(_mProg, 3))
            ck(deepEqual(_mProg.lastRecord, _expectPageRec),
               "the presentation record must deep-equal the §4.1 payload, got " + JSON.stringify(_mProg.lastRecord))

            // THE CHURN HALF (see the previous phase). Three page turns, no presentation, a full
            // debounce interval elapsed: the store must not have moved by a single write.
            ck(_navProg.records.length === harness._navBefore,
               "NAVIGATION MUST NOT WRITE AT ALL — three page turns with nothing presented added "
               + (_navProg.records.length - harness._navBefore) + " record(s); each one is a "
               + "QSettings disk sync per page turn")
            ck(recordsNaming(_navProg, 4) === 0,
               "...and the page the reader was only SENT to must never appear in the store, got "
               + recordsNaming(_navProg, 4) + " record(s) naming page 4")
            // ...and the same shell DOES write the moment a page is really on screen, so the two
            // assertions above are proving a rule rather than a dead sink.
            _navShell._onPresented(4, 0)
            _navShell.recordProgress()
            ck(recordsNaming(_navProg, 4) === 1,
               "presenting page 4 on that same shell must write it exactly once, got "
               + recordsNaming(_navProg, 4))

            // COALESCING is the disk-churn guarantee. Long Strip raises presented() off its 80ms
            // tracking flush — ~12 times a second while scrolling, continuously for a whole
            // Auto-scroll run — so a write per presentation would storm QSettings. Three
            // presentations inside one window must reach the sink ONCE, carrying the LAST position
            // (nothing is dropped, the newest wins).
            var beforeBurst = _mProg.records.length
            _mShell._onPresented(3, 0)
            _mShell._onPresented(4, 0)
            _mShell._onPresented(5, 0)
            ck(_mProg.records.length === beforeBurst,
               "a burst of presentations must not write per presentation, got "
               + (_mProg.records.length - beforeBurst) + " immediate writes")
            harness._burstBefore = beforeBurst
            burstDeferredTimer.start()
            return
        } catch (e) {
            failures.push("exception during presented-deferred checks: " + e.message)
        }
        bookmarkDebounceTimer.start()
    }

    function runBurstDeferred() {
        try {
            ck(_mProg.records.length === harness._burstBefore + 1,
               "three presentations inside one debounce window must coalesce to ONE disk write, got "
               + (_mProg.records.length - harness._burstBefore))
            ck(recordsNaming(_mProg, 4) === 0,
               "...and the SWALLOWED middle position must never reach the store on its own, got "
               + recordsNaming(_mProg, 4) + " record(s) naming page 4")
            ck(_mProg.lastRecord && _mProg.lastRecord.resume
               && _mProg.lastRecord.resume.page === 5,
               "the coalesced write must carry the LAST position (5), got "
               + JSON.stringify(_mProg.lastRecord ? _mProg.lastRecord.resume : null))

            // CLOSE: immediate flush + core.closeEntry() (must NOT wait for the debounce)
            var recBeforeClose = _mProg.records.length
            _mShell.shutdown()
            ck(_mProg.records.length > recBeforeClose, "close must flush a final progress.record (immediate)")
            ck(_mProg.lastRecord && _mProg.lastRecord.resume
               && _mProg.lastRecord.resume.page === 5,
               "close must record the last PRESENTED page (5), got "
               + JSON.stringify(_mProg.lastRecord ? _mProg.lastRecord.resume : null))
            ck(_mCore.closed === true, "close (shutdown) must call core.closeEntry()")
        } catch (e) {
            failures.push("exception during burst-deferred checks: " + e.message)
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
    // Task 9: the ONE shell scenario that needs the strip surface to be genuinely PAINTING, so that
    // "the lens is fed what the reader is drawing" can be asserted on real boxes instead of on two
    // empty lists. (Measured: without this, deleting the shell's closed-Loupe gate outright still
    // passed — this harness roots its tree invisible, so every mounted surface reads inactive and
    // answers with nothing whatever the shell does.) A component of its own rather than a field on
    // FakeCore: a `stripModel` on the shared fake would build a live ListView in every other
    // scenario in this file.
    component FakeLoupeCore: QtObject {
        property var stripModel: null
        property int stripWidthPct: 78
        property int stripGap: 0
        property int openCount: 0
        property string couplingState: "auto:normal:1.0"
        signal entryChanged()
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        signal pairingChanged()
        signal bookmarksChanged()
        function openEntry(entryId, pages, direction, persisted) { openCount += 1 }
        function closeEntry() {}
        function setVisible(pages) {}
        function unitForPage(page) { return { rightIndex: page, leftIndex: -1, spread: false } }
        function pageInfo(page) { return { sourceWidth: 1200, sourceHeight: 1800 } }
        function imageUrl(page, tier) {
            return "image://comicreader/1/" + page + "?rev=0&tier="
                   + ((tier === undefined || tier === "") ? "hq" : String(tier))
        }
        function setStripLayout(w, g) { stripWidthPct = w; stripGap = g }
        function bookmarks() { return [] }
        function persistedState() { return ({}) }
    }
    ListModel { id: loupeStripModel }
    FakeLoupeCore { id: fakeCoreLp }  FakeProgress { id: fakeProgLp }  FakePageStore { id: fakeStoreLp }
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
    // ...and again after the presentation raised in that phase has had its own debounce window
    Timer { id: presentedDeferredTimer; interval: 150; running: false; onTriggered: harness.runPresentedDeferred() }
    Timer { id: burstDeferredTimer; interval: 150; running: false; onTriggered: harness.runBurstDeferred() }
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
