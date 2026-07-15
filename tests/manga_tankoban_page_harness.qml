// Offscreen logic harness for MangaTankobanLibrary + MangaTankobanSourcesPage
// (Task 9) + the generalized MangaReader (Task 10).
//
// A grep test proves the wiring strings exist; THIS proves the volume-first
// surface actually behaves. It supplies a FAKE `TankobanVolumes` (an in-memory
// object with the same invokables/signals as the native service), builds the
// library for TWO different series sharing that one service, and proves:
//   * Off is the default, and enabling series A does NOT enable series B
//     (per-series persistence lives in the service, keyed by seriesId).
//   * EVERY canonical volume renders as a row, even one with no source.
//   * "Choose source" raises sourcesRequested with the volume identity, and the
//     full-screen MangaTankobanSourcesPage renders the service's sources VERBATIM
//     (Nyaa rows in order, the WeebCentral fallback LAST) — a Nyaa pick dispatches
//     downloadNyaa(volumeId, infoHash), an enabled WeebCentral pick compiles.
//   * A `progress` for one volume attaches to that volume's row only — and never
//     to another SERIES' library that shares the same service.
//
// Task 10 adds the reader contract: the SAME MangaReader, given an injected
// `pageStore`, reads a READY volume through localPages(volumeId), namespaces its
// Continue record under the "tankoban" kind (a chapter reader keeps "manga", so
// they never overwrite), and — on a DESCENDING volume model — crosses off the end
// of a ready volume into the next higher one, or emits sourceRequested when it
// isn't ready yet (without moving curChapterId).
//
// Verdict rides the sentinel + exit code: a thrown QML error HANGS qml.exe
// offscreen, so every check is wrapped in try/catch → Qt.exit(1).
import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

    // ── the fake native service ────────────────────────────────────────────
    // Mirrors MangaTankobanService's QML API over plain in-memory maps. modeMap
    // is per-series (the real service persists per-series QSettings); srcMap holds
    // the service-ordered source results (Nyaa first, WeebCentral card LAST — the
    // real onSourcesFound always appends the WeebCentral card even with no Nyaa).
    component FakeService: QtObject {
        property var modeMap: ({})
        property var volMap: ({})
        property var srcMap: ({})

        // records the last picker dispatch so the page test can assert the exact call
        property string lastNyaaVol: ""
        property string lastNyaaHash: ""
        property string lastCompileVol: ""

        signal volumesChanged(string seriesId)
        signal sourcesReady(string volumeId, var results)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)

        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function modeEnabled(sid) { return modeMap[sid] === true }
        function setModeEnabled(sid, enabled) {
            var m = {}
            for (var k in modeMap) m[k] = modeMap[k]
            m[sid] = enabled
            modeMap = m
            volumesChanged(sid)
        }
        function searchSources(vid) { sourcesReady(vid, srcMap[vid] !== undefined ? srcMap[vid] : []) }
        function downloadNyaa(vid, infoHash) { lastNyaaVol = String(vid); lastNyaaHash = String(infoHash) }
        function compileWeebCentral(vid) { lastCompileVol = String(vid) }
        function cancel(vid) { /* no-op */ }
        function remove(vid) { /* no-op */ }
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return [] }
    }

    // ── the fake PAGE store the reader is injected with (Task 10) ─────────────
    // Same localPages/statusOf shape the reader reads from Downloads/Comics. Only
    // a whitelisted set of volumes are "ready" (localPages non-empty); everything
    // else reads empty, which is exactly how the reader tells ready from not-ready.
    // It records the last localPages() arg so the harness can prove the page model
    // was sourced from localPages(volumeId), and flags any (wrong) chapter-download
    // call so a tankoban startDownload leaking to the chapter API is caught.
    component FakePageStore: QtObject {
        property var readyPages: ({})        // volumeId -> [page,...]
        property string lastLocalPagesArg: ""
        property bool chapterDownloadCalled: false

        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)

        function localPages(vid) {
            lastLocalPagesArg = String(vid)
            return readyPages[vid] !== undefined ? readyPages[vid] : []
        }
        function statusOf(vid) {
            var ready = readyPages[vid] !== undefined && readyPages[vid].length > 0
            return { "state": ready ? "ready" : "none", "done": 0, "total": 0 }
        }
        function downloadChapter(cid, sid, title, label) { chapterDownloadCalled = true }
        function downloadIssue(cid, url, sid, title, label, bytes) { chapterDownloadCalled = true }
    }

    FakeService {
        id: svc
        modeMap: ({})
        volMap: ({
            "A": [
                { "id": "volA1", "seriesId": "A", "number": "1", "title": "Romance Dawn",
                  "cover": "", "chapterCount": 9, "state": "none" },
                { "id": "volA2", "seriesId": "A", "number": "2", "title": "Buggy the Clown",
                  "cover": "", "chapterCount": 8, "state": "none",
                  "synopsis": "Nami joins.", "synopsisSource": "apple",
                  "synopsisSourceUrl": "https://books.apple.com/x" },
                { "id": "volA3", "seriesId": "A", "number": "3", "title": "Don't Get Fooled",
                  "cover": "", "chapterCount": 0, "state": "none" }
            ],
            "B": [
                { "id": "volB1", "seriesId": "B", "number": "1", "title": "B One",
                  "cover": "", "chapterCount": 5, "state": "none" },
                { "id": "volB2", "seriesId": "B", "number": "2", "title": "B Two",
                  "cover": "", "chapterCount": 5, "state": "none" }
            ]
        })
        srcMap: ({
            "volA1": [
                { "kind": "nyaa", "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1",
                  "releaseTitle": "One Piece v01 (Digital)", "uploader": "Stumbleine",
                  "tier": 1, "sizeBytes": 120000000, "seeders": 42,
                  "coverageLo": "1", "coverageHi": "1", "standalone": true, "digital": true,
                  "enabled": true },
                { "kind": "nyaa", "infoHash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb2",
                  "releaseTitle": "One Piece v01-03", "uploader": "danke-Empire",
                  "tier": 2, "sizeBytes": 360000000, "seeders": 12,
                  "coverageLo": "1", "coverageHi": "3", "standalone": false, "digital": false,
                  "enabled": true },
                { "kind": "weebcentral", "label": "Build from chapters", "enabled": true,
                  "chapterCount": 9, "reason": "" }
            ],
            // volA3 has NO Nyaa source — only the always-last WeebCentral card, disabled.
            "volA3": [
                { "kind": "weebcentral", "label": "Build from chapters", "enabled": false,
                  "chapterCount": 0, "reason": "No WeebCentral chapters map to this volume yet." }
            ]
        })
    }

    // Volumes 1 and 3 are READY (3 local pages each); volume 2 is NOT — so crossing
    // toward volA2 from EITHER side (forward off volA1, backward off volA3) must ask
    // for volA2's source rather than navigate onto an unreadable volume.
    FakePageStore {
        id: pageStore
        readyPages: ({
            "volA3": [
                { "index": 0, "url": "file:///fake/A/v3/p0.png", "group": 0 },
                { "index": 1, "url": "file:///fake/A/v3/p1.png", "group": 0 },
                { "index": 2, "url": "file:///fake/A/v3/p2.png", "group": 0 }
            ],
            "volA1": [
                { "index": 0, "url": "file:///fake/A/v1/p0.png", "group": 0 },
                { "index": 1, "url": "file:///fake/A/v1/p1.png", "group": 0 },
                { "index": 2, "url": "file:///fake/A/v1/p2.png", "group": 0 }
            ]
        })
    }

    property var libA: null
    property var libB: null
    property var page: null               // MangaTankobanSourcesPage (full-screen picker)
    property var readerT: null            // tankoban (volume) reader
    property var readerC: null            // chapter (manga) reader
    property var readerComp: null         // the MangaReader component (reused for the backward-cross reader)
    property string lastSourceReq: ""     // last reader.sourceRequested(entryId)
    property var lastLibReq: null          // last library.sourcesRequested(context)

    // the reader's DESCENDING volume model (highest volume first) — the series page
    // builds exactly this from the ascending library so curIndex-1 is the next HIGHER
    // volume, preserving MangaReader's newest-first crossing law.
    readonly property var volEntriesDesc: [
        { "id": "volA3", "number": "3", "name": "Don't Get Fooled" },
        { "id": "volA2", "number": "2", "name": "Buggy the Clown" },
        { "id": "volA1", "number": "1", "name": "Romance Dawn" }
    ]

    function ck(cond, msg) { if (!cond) throw new Error(msg) }
    function rowById(lib, id) {
        var rows = lib.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === String(id)) return rows[i]
        return null
    }

    function setup() {
        var comp = Qt.createComponent("../qml/MangaTankobanLibrary.qml")
        if (comp.status === Component.Error) throw new Error("component: " + comp.errorString())
        harness.libA = comp.createObject(harness, { "service": svc, "seriesId": "A", "width": 620 })
        harness.libB = comp.createObject(harness, { "service": svc, "seriesId": "B", "width": 620 })
        if (!harness.libA || !harness.libB) throw new Error("createObject returned null")
        harness.libA.sourcesRequested.connect(function (ctx) { harness.lastLibReq = ctx })

        // The full-screen picker, sharing the SAME fake service. show() kicks the fake's
        // searchSources, which synchronously emits sourcesReady back into the page.
        var pc = Qt.createComponent("../qml/MangaTankobanSourcesPage.qml")
        if (pc.status === Component.Error) throw new Error("page component: " + pc.errorString())
        harness.page = pc.createObject(harness, { "service": svc, "width": 640, "height": 480 })
        if (!harness.page) throw new Error("page createObject returned null")

        // Task 10: the SAME reader, opened on a READY volume through the injected store.
        var rc = Qt.createComponent("../qml/MangaReader.qml")
        if (rc.status === Component.Error) throw new Error("reader component: " + rc.errorString())
        harness.readerComp = rc            // reused later for the backward-cross reader
        harness.readerT = rc.createObject(harness, {
            "width": 640, "height": 480, "seriesId": "A", "seriesTitle": "One Piece",
            "pageStore": pageStore, "entryKind": "tankoban", "entryLabelPrefix": "Vol. ",
            "chapters": harness.volEntriesDesc, "chapterId": "volA1", "chapterLabel": "Vol. 1"
        })
        if (!harness.readerT) throw new Error("reader createObject returned null")
        harness.readerT.sourceRequested.connect(function (id) { harness.lastSourceReq = String(id) })

        // …and a chapter reader for the SAME series — a SEPARATE progress namespace.
        harness.readerC = rc.createObject(harness, {
            "width": 640, "height": 480, "seriesId": "A", "seriesTitle": "One Piece",
            "chapters": [{ "id": "chA9", "number": "9", "name": "" }],
            "chapterId": "chA9", "chapterLabel": "Chapter 9"
        })
        if (!harness.readerC) throw new Error("chapter reader createObject returned null")
    }

    function runChecks() {
        try {
            // 1. Off is the default (nothing enabled yet).
            ck(svc.modeEnabled("A") === false, "series A must default OFF")
            ck(svc.modeEnabled("B") === false, "series B must default OFF")

            // 2. Enabling A must not enable B — per-series persistence.
            svc.setModeEnabled("A", true)
            ck(svc.modeEnabled("A") === true, "series A must be ON after enable")
            ck(svc.modeEnabled("B") === false, "enabling A must not enable B")

            // 3. EVERY canonical volume renders as a row, even the source-less one.
            ck(harness.libA.volumeRows.length === 3, "A must expose 3 canonical volumes")
            ck(harness.libA.renderedCount === 3, "A must RENDER 3 rows, got " + harness.libA.renderedCount)
            ck(harness.libB.renderedCount === 2, "B must RENDER 2 rows, got " + harness.libB.renderedCount)

            // 4. "Choose source" RAISES sourcesRequested with the volume identity (the
            //    library no longer renders an inline chooser); MangaSeries merges the
            //    series id/title and opens the full-screen picker.
            harness.lastLibReq = null
            harness.libA.chooseSource("volA1")
            ck(harness.lastLibReq !== null, "chooseSource must emit sourcesRequested")
            ck(String(harness.lastLibReq.volumeId) === "volA1", "the request must carry the volumeId")
            ck(String(harness.lastLibReq.number) === "1", "the request must carry the volume number")
            ck(harness.lastLibReq.title === "Romance Dawn", "the request must carry the volume title")

            // 4b. The full-screen picker renders the service's sources VERBATIM — the two
            //     Nyaa rows in order, the WeebCentral fallback LAST.
            harness.page.show({ "volumeId": "volA1", "seriesTitle": "One Piece",
                                "volumeNumber": "1", "volumeTitle": "Romance Dawn", "cover": "" })
            ck(harness.page.rows.length === 3, "the picker must render all 3 sources, got " + harness.page.rows.length)
            ck(harness.page.rows[0].kind === "nyaa" && harness.page.rows[0].uploader === "Stumbleine",
               "picker row 0 must be the service's first Nyaa row")
            ck(harness.page.rows[1].kind === "nyaa" && harness.page.rows[1].uploader === "danke-Empire",
               "picker row 1 must keep service order")
            ck(harness.page.rows[2].kind === "weebcentral", "the WeebCentral row must be LAST")

            // 4c. Picking the (enabled) WeebCentral fallback compiles from chapters, then closes.
            svc.lastCompileVol = ""
            harness.page.pickWeeb(harness.page.rows[2])
            ck(svc.lastCompileVol === "volA1", "a WeebCentral pick must call compileWeebCentral(volumeId)")
            ck(harness.page.open === false, "choosing the WeebCentral fallback closes the picker")

            // 4d. Picking a Nyaa row calls downloadNyaa(volumeId, infoHash) and closes.
            harness.page.show({ "volumeId": "volA1", "seriesTitle": "One Piece", "volumeNumber": "1", "cover": "" })
            var firstNyaa = harness.page.rows[0]
            svc.lastNyaaVol = ""; svc.lastNyaaHash = ""
            harness.page.pickNyaa(firstNyaa)
            ck(svc.lastNyaaVol === "volA1", "a Nyaa pick must call downloadNyaa with the volumeId")
            ck(svc.lastNyaaHash === firstNyaa.infoHash, "a Nyaa pick must pass the row's infoHash")
            ck(harness.page.open === false, "choosing a source closes the picker")

            // 4e. A source-less volume still shows the WeebCentral fallback last; when that
            //     fallback is DISABLED, picking it does nothing (no compile).
            harness.page.show({ "volumeId": "volA3", "seriesTitle": "One Piece", "volumeNumber": "3", "cover": "" })
            ck(harness.page.rows.length === 1 && harness.page.rows[0].kind === "weebcentral",
               "a source-less volume must still show the WeebCentral fallback")
            svc.lastCompileVol = ""
            harness.page.pickWeeb(harness.page.rows[0])
            ck(svc.lastCompileVol === "", "a DISABLED WeebCentral fallback must not compile")

            // 5. progress attaches to that volume's row ONLY — not sibling volumes,
            //    and not another series' library sharing the same service.
            svc.progress("volA1", 3, 10)
            ck(harness.libA.progressByVolume["volA1"] !== undefined, "volA1 must receive its progress")
            ck(Number(harness.libA.progressByVolume["volA1"].done) === 3, "volA1 progress done must be 3")
            ck(harness.libA.progressByVolume["volA2"] === undefined, "volA2 must NOT receive volA1's progress")
            ck(harness.libB.progressByVolume["volA1"] === undefined, "series B must NOT receive series A's progress")

            // 6. Apple attribution URL flows through: the apple-sourced row carries a
            //    non-empty synopsisSourceUrl (service now forwards it); a non-apple row
            //    does not — so the link lights up ONLY for apple + a real URL.
            var rowApple = harness.rowById(harness.libA, "volA2")
            var rowPlain = harness.rowById(harness.libA, "volA1")
            ck(rowApple && rowApple.synopsisSource === "apple", "volA2 must be apple-sourced")
            ck(rowApple && String(rowApple.synopsisSourceUrl || "").length > 0,
               "an apple-sourced volume row must expose a non-empty synopsisSourceUrl")
            ck(rowPlain && String(rowPlain.synopsisSourceUrl || "").length === 0,
               "a non-apple volume row must NOT expose a synopsisSourceUrl")

            // ── Task 10: the generalized reader ──────────────────────────────
            var rT = harness.readerT
            var rC = harness.readerC
            // 7. the injected page store WINS as the reader's store…
            ck(rT.store === pageStore, "an injected pageStore must win as the reader store")
            // …and null pageStore falls back to the default (undefined offscreen -> null).
            ck(rC.store === null, "a chapter reader with no injected store falls back to the default")

            // 8. the page model is sourced from localPages(volumeId).
            ck(pageStore.lastLocalPagesArg === "volA1", "reader must ask the store for localPages(volumeId)")
            ck(rT.max === 3, "reader must load Volume 1's 3 local pages, got " + rT.max)

            // 9. progress NAMESPACES on the entry kind: a volume record ("tankoban") and a
            //    chapter record ("manga") for the SAME series get different ProgressStore
            //    keys (kind\x1fid), so neither can overwrite the other.
            ck(rT.progressKind === "tankoban", "a volume reader must record under kind 'tankoban'")
            ck(rC.progressKind === "manga", "a chapter reader must record under kind 'manga'")
            ck(rT.progressKind !== rC.progressKind,
               "tankoban and manga progress must be SEPARATE namespaces (no overwrite)")

            // 10. the reader model is DESCENDING (highest volume first) so curIndex-1 is the
            //     next HIGHER volume — MangaReader's newest-first crossing law, intact.
            ck(String(rT.chapters[0].id) === "volA3", "reader model must be DESCENDING (highest volume first)")
            ck(rT.curChapterId === "volA1" && rT.curIndex === 2, "reader opened on Volume 1 (last in the descending model)")

            // 11. crossing off the end of a READY volume into a NOT-ready one asks the series
            //     page for its source and does NOT move curChapterId.
            harness.lastSourceReq = ""
            rT.goNextChapter()
            ck(harness.lastSourceReq === "volA2", "a not-ready next volume must raise sourceRequested(volA2), got '" + harness.lastSourceReq + "'")
            ck(rT.curChapterId === "volA1", "curChapterId must stay on the ready volume when the next isn't ready")

            // 12. startDownload() in tankoban mode routes to the source chooser, never the
            //     chapter download API.
            harness.lastSourceReq = ""
            rT.startDownload()
            ck(harness.lastSourceReq === "volA1", "tankoban startDownload must emit sourceRequested(curChapterId)")
            ck(pageStore.chapterDownloadCalled === false, "tankoban startDownload must NOT hit the chapter download API")

            // 13. crossing is SYMMETRIC: paging BACKWARD off a READY volume (volA3) into a
            //     NOT-ready LOWER volume (volA2) also routes to the source chooser and leaves
            //     curChapterId put — the mirror of check 11. (Built here, after the localPages
            //     assertion above, so it can't disturb it.)
            var rB = harness.readerComp.createObject(harness, {
                "width": 640, "height": 480, "seriesId": "A", "seriesTitle": "One Piece",
                "pageStore": pageStore, "entryKind": "tankoban", "entryLabelPrefix": "Vol. ",
                "chapters": harness.volEntriesDesc, "chapterId": "volA3", "chapterLabel": "Vol. 3"
            })
            ck(rB, "backward-cross reader createObject returned null")
            rB.sourceRequested.connect(function (id) { harness.lastSourceReq = String(id) })
            ck(rB.curChapterId === "volA3" && rB.curIndex === 0, "backward reader opens on the highest volume")
            harness.lastSourceReq = ""
            rB.goPrevChapter(true)
            ck(harness.lastSourceReq === "volA2", "a not-ready LOWER volume must raise sourceRequested(volA2), got '" + harness.lastSourceReq + "'")
            ck(rB.curChapterId === "volA3", "curChapterId must stay put when the lower volume isn't ready")

            console.log("MANGA_TANKOBAN_PAGE_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("MANGA_TANKOBAN_PAGE_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Component.onCompleted: {
        try {
            setup()
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("MANGA_TANKOBAN_PAGE_FAIL setup: " + e.message)
            Qt.exit(1)
        }
    }

    // Safety net: never spin forever offscreen.
    Timer {
        interval: 6000; running: true
        onTriggered: { console.log("MANGA_TANKOBAN_PAGE_FAIL timeout"); Qt.exit(1) }
    }
}
