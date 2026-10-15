// Comic Reader — PUBLIC CALLER CONTRACT oracle (Task 1).
//
// Instantiates the REAL qml/MangaReader.qml offscreen with a fake page store (5 local pages)
// and asserts the contract every caller depends on, so the Task 13 rebuild (MangaReader becomes
// a thin wrapper over the new shell) can prove it never broke a caller. What it pins:
//   * reader.max === 5 once pages load (the page-count property is `max`);
//   * the progress namespace flips manga -> comic -> tankoban as western/entryKind change
//     (reader.progressKind — the exact value the Progress.record call stamps into "kind");
//   * every required signal exists, connects, and emits;
//   * the injected store contract is exercised — localPages(curChapterId) is called, and
//     downloadChapter / downloadIssue receive the documented arguments; a not-ready tankoban
//     entry routes startDownload() to sourceRequested() instead of a download API.
//
// SCOPE BOUNDARY: a green COMICREADER_CONTRACT_OK does NOT prove the Progress.record resume
// payload survived — `Progress` is a C++ context property, uninjectable offscreen, so that payload
// is documented (contract §4.1), not asserted here. Its guarantee lives in Task 8 (progressPayload
// unit test) + Task 13 (migration acceptance). Don't read "oracle green" as "Continue/resume safe".
//
// HOUSE HARNESS PATTERN: a thrown error HANGS qml.exe offscreen. `ck` NEVER throws — it collects
// failures; the run prints exactly ONE `COMICREADER_CONTRACT_OK` when clean, else one
// `COMICREADER_CONTRACT_FAIL: <msg>` per failure and Qt.exit(1). Full contract:
// docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md
//
// Progress is a C++ context property (native/main.cpp), undefined under qml.exe and guarded by
// the reader — so it cannot be injected here. The namespace is asserted via reader.progressKind,
// which is the source of truth the record call reads.

import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

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
    property var failures: []

    // signal probes
    property bool   gotBack: false
    property bool   gotMin: false
    property bool   gotFull: false
    property bool   gotClose: false
    property string gotSource: ""

    function ck(cond, msg) { if (!cond) failures.push(msg) }

    function report() {
        if (failures.length === 0) {
            console.log("COMICREADER_CONTRACT_OK")
            Qt.exit(0)
        } else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_CONTRACT_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    function setup() {
        var rc = Qt.createComponent("../qml/MangaReader.qml")
        if (rc.status === Component.Error) throw new Error("reader component: " + rc.errorString())
        // styleOv pinned so onCompleted's load() takes the deterministic long-strip path
        // regardless of any app-wide persisted reading style.
        harness.rdr = rc.createObject(harness, {
            "width": 640, "height": 480,
            "seriesId": "s1", "seriesTitle": "Contract Series", "seriesCover": "file:///f/cover.png",
            "pageStore": pageStore, "entryKind": "manga", "western": false, "styleOv": "long_strip",
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1"
        })
        if (!harness.rdr) throw new Error("reader createObject returned null")
    }

    function wireSignals() {
        var r = harness.rdr
        r.backRequested.connect(function () { harness.gotBack = true })
        r.minimizeRequested.connect(function () { harness.gotMin = true })
        r.fullscreenRequested.connect(function () { harness.gotFull = true })
        r.closeRequested.connect(function () { harness.gotClose = true })
        r.sourceRequested.connect(function (id) { harness.gotSource = String(id) })
    }

    function runChecks() {
        try {
            var r = harness.rdr

            // ---- 1. pages loaded: the page-count property is `max` ----
            ck(r.max === 5, "reader.max must be 5 after pages load, got " + r.max)
            ck(String(pageStore.lastLocalPagesArg) === "ch1",
               "reader must call store.localPages(curChapterId='ch1'), got '" + pageStore.lastLocalPagesArg + "'")

            // ---- 2. required signals exist, connect + emit ----
            ck(typeof r.backRequested === "function", "signal backRequested() must exist")
            ck(typeof r.minimizeRequested === "function", "signal minimizeRequested() must exist")
            ck(typeof r.fullscreenRequested === "function", "signal fullscreenRequested() must exist")
            ck(typeof r.closeRequested === "function", "signal closeRequested() must exist")
            ck(typeof r.sourceRequested === "function", "signal sourceRequested(string) must exist")

            r.backRequested();       ck(harness.gotBack,  "backRequested must connect + emit")
            r.minimizeRequested();   ck(harness.gotMin,   "minimizeRequested must connect + emit")
            r.fullscreenRequested(); ck(harness.gotFull,  "fullscreenRequested must connect + emit")
            r.closeRequested();      ck(harness.gotClose, "closeRequested must connect + emit")

            // ---- 3a. namespace = manga (western=false, entryKind='manga') ----
            r.western = false; r.entryKind = "manga"
            ck(r.progressKind === "manga", "progressKind must be 'manga', got '" + r.progressKind + "'")
            // manga download path -> store.downloadChapter(cid, sid, title, label)
            r.startDownload()
            var dc = pageStore.lastDownloadChapter
            ck(dc !== null && dc.cid === "ch1" && dc.sid === "s1" && dc.label === "Chapter 1",
               "manga startDownload must call store.downloadChapter(ch1, s1, .., 'Chapter 1'), got " + JSON.stringify(dc))

            // ---- 3b. namespace flips to comic (western=true) ----
            r.western = true
            ck(r.progressKind === "comic", "progressKind must be 'comic' when western, got '" + r.progressKind + "'")
            // western download path -> store.downloadIssue(cid, url, sid, title, label, bytes)
            r.startDownload()
            var di = pageStore.lastDownloadIssue
            ck(di !== null && di.cid === "ch1" && di.sid === "s1" && di.bytes === 0,
               "western startDownload must call store.downloadIssue(ch1, '', s1, .., 0), got " + JSON.stringify(di))

            // ---- 3c. namespace flips to tankoban (entryKind='tankoban') ----
            r.western = false; r.entryKind = "tankoban"
            ck(r.progressKind === "tankoban", "progressKind must be 'tankoban', got '" + r.progressKind + "'")
            // a tankoban entry is not page-downloaded: startDownload routes to sourceRequested(entryId)
            harness.gotSource = ""
            r.startDownload()
            ck(harness.gotSource === "ch1",
               "tankoban startDownload must emit sourceRequested('ch1'), got '" + harness.gotSource + "'")
        } catch (e) {
            failures.push("exception during checks: " + e.message)
        }
        report()
    }

    Component.onCompleted: {
        try { setup(); wireSignals(); Qt.callLater(runChecks) }
        catch (e) { console.log("COMICREADER_CONTRACT_FAIL: setup: " + e.message); Qt.exit(1) }
    }

    // safety net — a hang (not a thrown error) still fails loudly instead of stalling CI
    Timer {
        interval: 8000; running: true
        onTriggered: { console.log("COMICREADER_CONTRACT_FAIL: timeout"); Qt.exit(1) }
    }
}
