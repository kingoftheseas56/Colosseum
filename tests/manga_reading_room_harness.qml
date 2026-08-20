// RED/GREEN contract harness for the locked Tankoban Reading Room.
// It deliberately uses only fake service seams: no network, no live app state.
//
// Catalogue-independence Slice 3 (2026-08-20): fixtures are now catalogue-shaped rows
// (number/cover/title, no chapterStart/chapterEnd — a baked TankobanCatalog row carries
// no chapter range at all). The WC thumb-scrape machinery this harness used to pin
// (bounded cover-request bursts, range captions) is gone from MangaTankobanLibrary; this
// harness now proves the opposite contract — fetchThumb is NEVER called, and a card's
// cover resolves off the catalogue-baked field or, once a volume is on disk, its own
// first local page, else the honest NO COVER glass.
import QtQuick

Item {
    id: harness
    width: 1440
    height: 820
    visible: true

    component FakeService: QtObject {
        property var volMap: ({})
        // vid -> [{url}] — a volume's own extracted pages once it is "ready" on disk
        // (app-owned bytes; distinct from a catalogue-baked cover).
        property var localPageMap: ({})
        signal volumesChanged(string seriesId)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return localPageMap[vid] !== undefined ? localPageMap[vid] : [] }
        function cancel(vid) {}
    }

    component FakeProgress: QtObject {
        property var record: null
        function get(kind, sid) { return kind === "tankoban" ? record : null }
    }

    component FakeDownloads: QtObject {
        property var asked: []
        signal thumbReady(string chapterId, string url)
        signal progress(string chapterId, real done, real total)
        signal finished(string chapterId)
        signal failed(string chapterId, string reason)
        signal removed(string chapterId)
        function fetchThumb(seriesId, chapterId) {
            var next = asked.slice()
            next.push({ "seriesId": String(seriesId), "chapterId": String(chapterId) })
            asked = next
        }
        function statusOf(cid) { return { "state": "none", "done": 0, "total": 0 } }
        function downloadChapter(cid, sid, title, label) {}
    }

    FakeService { id: service }
    FakeProgress { id: progress }
    FakeDownloads { id: downloads }

    // Catalogue-shaped rows only: number (string), cover, title — no chapterStart/
    // chapterEnd. Volume "9" carries a baked catalogue cover+title (the harvest-covered
    // case); volume "2" carries no catalogue cover but is "ready" with a local first page
    // (the on-disk case); every other row is deliberately bare (the NO COVER case).
    function volumes(count) {
        var out = []
        for (var i = 1; i <= count; i++) {
            var state = i === 2 ? "ready"
                       : i === 3 ? "resolving"
                       : i === 4 ? "ingesting"
                       : i === 5 ? "packing"
                       : i === 6 ? "downloading"
                       : i === 7 ? "failed"
                       : "none"
            out.push({
                "id": "v" + i, "seriesId": "S", "number": String(i),
                "title": i === 9 ? "Real Volume Title" : "",
                "cover": i === 9 ? "file:///fixtures/vol9-cover.jpg" : "",
                "state": state
            })
        }
        return out
    }

    function chapters(count) {
        var out = []
        for (var i = 1; i <= count; i++)
            out.push({ "id": "c" + i, "number": i, "name": "Chapter " + i })
        return out
    }

    function rowByNumber(rows, number) {
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].number) === String(number)) return rows[i]
        return null
    }

    property var room: null
    property var chapterOnlyRoom: null
    property var lastBatch: null

    function ck(condition, message) {
        if (!condition) throw new Error(message)
    }

    // House idiom (already established in this file): prove an assertion is actually
    // live by temporarily inverting it and confirming it throws, then restoring. Used
    // below for the fetchThumb-never-called proof and the coveredCount proof.
    function ckNegativeControl(fn, message) {
        var caught = false
        try { fn() } catch (e) { caught = true }
        ck(caught, message)
    }

    function runChecks() {
        try {
            service.volMap = ({ "S": volumes(115), "C": [] })
            service.localPageMap = ({ "v2": [{ "url": "file:///local/v2-page1.jpg" }] })
            progress.record = { "chapterId": "v74", "page": 93, "max": 210 }

            var roomComp = Qt.createComponent("../qml/MangaReadingRoom.qml")
            if (roomComp.status === Component.Error)
                throw new Error("room component: " + roomComp.errorString())

            room = roomComp.createObject(harness, {
                "width": 1320, "height": 720, "seriesId": "S",
                "seriesTitle": "One Piece", "author": "Eiichiro Oda",
                "status": "Ongoing", "year": 1997, "score": 9.2,
                "genres": ["Action", "Adventure", "Fantasy"],
                "synopsis": "A long synopsis used to prove the rail's three-line floor.",
                "service": service, "progress": progress, "downloader": downloads
            })
            if (!room) throw new Error("room createObject returned null")
            room.library.batchRequested.connect(function(numbers, label) {
                harness.lastBatch = { "numbers": numbers, "label": label }
            })

            var lib = room.library
            ck(room.contentHeight === room.height && lib.height < room.height,
               "the Reading Room must stay fixed-height while the pane owns the shorter body")
            var rejectedBrokenHeight = false
            try { ck(lib.height >= room.height, "negative fixed-height control") }
            catch (negativeHeight) { rejectedBrokenHeight = true }
            ck(rejectedBrokenHeight,
               "the fixed-height assertion must fail when the contract is inverted")
            ck(lib.renderedCount === lib.volumeRows.length,
               "the continuum must retain the complete canonical model, rendered " + lib.renderedCount)
            ck(lib.visibleContinuumCount === 9,
               "the desktop continuum must expose nine visible positions at this width")
            ck(lib.autoLandNumber === 74 && lib.autoLandIndex === 73,
               "the grid must auto-land on the continue volume index")
            ck(lib.flowCurrentIndex === lib.focusIndex,
               "the reader-style flow must keep GridView currentIndex on the focused token")
            ck(lib.liveVolumeTiles > 0 && lib.liveVolumeTiles < lib.volumeRows.length,
               "the volume flow must virtualize a long canonical model, live " + lib.liveVolumeTiles)

            // ── catalogue-independence Slice 3: no live thumb scraping, ever ──────────
            ck(downloads.asked.length === 0,
               "the shelf must never call fetchThumb — covers are catalogue-baked or local-only now")
            ckNegativeControl(function () {
                downloads.fetchThumb("S", "some-chapter-id")   // simulate a reintroduced call site
                ck(downloads.asked.length === 0, "negative control for fetchThumb-never-called")
            }, "the fetchThumb-never-called assertion must fail when a call site is reintroduced")

            // ── catalogue-independence Slice 3: the cover ladder ──────────────────────
            var v9 = rowByNumber(lib.volumeRows, "9")
            var v2 = rowByNumber(lib.volumeRows, "2")
            var v1 = rowByNumber(lib.volumeRows, "1")
            ck(lib.coverFor(v9) === "file:///fixtures/vol9-cover.jpg",
               "a card with a baked catalogue cover must show it")
            ck(lib.coverFor(v2) === "file:///local/v2-page1.jpg",
               "a ready volume with no catalogue cover falls back to its own first local page")
            ck(lib.coverFor(v1) === "",
               "an uncovered, un-downloaded volume resolves to NO COVER, never a stale field")

            var shelfState = null
            for (var ci = 0; ci < lib.children.length && !shelfState; ci++)
                if (lib.children[ci].objectName === "tankobanShelfState") shelfState = lib.children[ci]
            if (!shelfState) throw new Error("tankobanShelfState bridge item not found")
            ck(shelfState.rowCount === 115, "tankobanShelfState.rowCount mirrors the canonical model")
            ck(shelfState.coveredCount === 2,
               "tankobanShelfState.coveredCount counts exactly the catalogue-covered and " +
               "local-ready rows (v9 + v2), coveredCount=" + shelfState.coveredCount)
            ckNegativeControl(function () {
                ck(shelfState.coveredCount === 999, "negative control for coveredCount")
            }, "the coveredCount assertion must fail when flipped to a wrong value")

            ck(typeof lib.shelfRangeFor === "undefined" && typeof lib.chapterSpanFor === "undefined",
               "the chapter-range caption functions must be fully removed, not just unused")
            ck(typeof lib.requestCovers === "undefined" && typeof lib.visibleRowsForCovers === "undefined"
               && typeof lib.visibleGridRows === "undefined" && typeof lib._firstChapterIdIn === "undefined",
               "the WC thumb-scrape machinery must be fully removed, not just unused")

            ck(lib.stateWordFor(lib.volumeRows[1]) === "On this device",
               "ready volume state must be drawn as On this device")
            ck(lib.stateWordFor(lib.volumeRows[2]) === "Finding source",
               "resolving state must use the canon word")
            ck(lib.effectiveState(lib.volumeRows[3]) === "ingesting",
               "ingesting state must be present in the tile contract")
            ck(lib.effectiveState(lib.volumeRows[4]) === "packing",
               "packing state must be present in the tile contract")
            ck(lib.effectiveState(lib.volumeRows[5]) === "downloading",
               "downloading state must be present in the tile contract")
            ck(lib.stateWordFor(lib.volumeRows[3]).indexOf("Adding") === 0,
               "ingesting state must use the canon word")
            ck(lib.stateWordFor(lib.volumeRows[4]).indexOf("Building") === 0,
               "packing state must use the canon word")
            ck(lib.stateWordFor(lib.volumeRows[5]).indexOf("Downloading") === 0,
               "downloading state must use the canon word")
            ck(lib.stateWordFor(lib.volumeRows[6]) === "Retry source",
               "failed state must use the canon word")
            var rejectedWrongState = false
            try { ck(lib.effectiveState(lib.volumeRows[1]) === "failed", "negative state control") }
            catch (negativeState) { rejectedWrongState = true }
            ck(rejectedWrongState,
               "the tile-state assertion must fail when a ready row is mislabeled")

            lib.selecting = true
            ck(lib.pressVolume(lib.focusIndex),
               "the active volume tile must accept a Select-mode pointer activation")
            ck(lib.selectedNumbers.indexOf("74") >= 0,
               "Select-mode pointer activation must retain the active canonical token")
            lib.focusAtIndex(lib.focusIndex + 1)
            ck(lib.pressVolume(lib.focusIndex),
               "the next realized volume tile must accept a Select-mode pointer activation")
            lib.downloadSelected()
            ck(lastBatch !== null, "select mode must emit one batch")
            ck(lastBatch.numbers.length === 2 && lastBatch.numbers[0] === "74"
               && lastBatch.numbers[1] === "75",
               "selected batch must contain exactly the selected canonical volume tokens")
            ck(lastBatch.label === "Download selected",
               "selected batch must carry the exact action label")

            var chapterOnlyComp = Qt.createComponent("../qml/MangaReadingRoom.qml")
            chapterOnlyRoom = chapterOnlyComp.createObject(harness, {
                "width": 1000, "height": 720, "seriesId": "C",
                "seriesTitle": "Chapter Only", "chapters": chapters(42),
                "service": service, "progress": progress, "downloader": downloads
            })
            ck(chapterOnlyRoom.library.showVolumes === false,
               "chapter-only series must not expose an empty shelf")
            ck(chapterOnlyRoom.library.chapterRows.length === 42,
               "chapter-only series must show its full chapter run in the tail")

            // Special canonical volume tokens must remain exact through focus
            // and selection; they are not guaranteed to be integers.
            service.volMap = ({ "S": service.volMap["S"], "C": service.volMap["C"],
                "T": [
                    { "id": "v10p5", "seriesId": "T", "number": "10.5", "cover": "", "title": "", "state": "none" },
                    { "id": "vExtra", "seriesId": "T", "number": "Extra", "cover": "", "title": "", "state": "none" }
                ] })
            var tokenRoom = roomComp.createObject(harness, {
                "width": 1000, "height": 720, "seriesId": "T", "seriesTitle": "Token Test",
                "service": service, "progress": progress, "downloader": downloads
            })
            ck(tokenRoom.library.indexOfNumber("10.5") === 0,
               "fractional volume tokens must remain addressable")
            tokenRoom.library.focusAtNumber("Extra")
            ck(tokenRoom.library.focusToken === "Extra" && tokenRoom.library.focusIndex === 1,
               "named volume tokens must remain the focused identity")
            ck(tokenRoom.library.flowCurrentIndex === 1,
               "named volume token focus must center its actual GridView row")

            console.log("MANGA_READING_ROOM_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("MANGA_READING_ROOM_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Timer { interval: 100; running: true; repeat: false; onTriggered: harness.runChecks() }
    Timer { interval: 8000; running: true
        onTriggered: { console.log("MANGA_READING_ROOM_FAIL timeout"); Qt.exit(1) } }
}
