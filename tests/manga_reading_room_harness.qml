// RED/GREEN contract harness for the Tankoban Reading Room.
// It deliberately uses only fake service seams: no network, no live app state.
//
// Catalogue-independence Slice 3 (2026-08-20): fixtures are now catalogue-shaped rows
// (number/cover/title, no chapterStart/chapterEnd â€” a baked TankobanCatalog row carries
// no chapter range at all). The WC thumb-scrape machinery this harness used to pin
// (bounded cover-request bursts, range captions) is gone from MangaTankobanLibrary; this
// harness proves the opposite contract â€” fetchThumb is NEVER called, and a card's cover
// resolves off the catalogue-baked field or, once a volume is on disk, its own first local
// page, else the honest NO COVER glass.
//
// Catalogue-independence Slice 5 (2026-08-20): the old "chapter-only room" case (a
// MangaReadingRoom seeded with a bare `chapters` array and asserted to render a chapter
// tail) is replaced â€” chapters are deleted completely, on-disk bytes included, per
// Hemanth's lock. The room and its shelf no longer have any chapter-shaped property or
// signal at all; a shelf-less series (no known volumes) now renders zero rows and the
// chapter API surface is asserted fully absent (typeof undefined), not just unused.
//
// arc-08 v2.3 adoption (2026-08-21): the vertical GridView shelf is now a horizontal
// Pages/Flow ListView continuum. Grid-shape assertions (columns/cellHeight/chip-word
// captions) become flow-shape (bookHeight/bookWidth clamp, flowCurrentIndex, the
// volumeNameFor/stateLineFor caption vocabulary). Every zero-chapter/cover/
// fetchThumb-never assertion below is unchanged â€” those contracts did not move.
// ListView layout (positionViewAtIndex/currentIndex settling, delegate realization) runs
// on a deferred Qt.callLater the same way the flow's own centring does (see
// MangaTankobanLibrary.qml's centreFlow()/focusAtNumber()), so every assertion that reads
// flow-layout state is itself wrapped in a matching Qt.callLater â€” checking synchronously,
// in the same call stack as object construction, would race the deferred layout and fail
// vacuously.
import QtQuick

Item {
    id: harness
    width: 1440
    height: 820
    visible: true

    component FakeService: QtObject {
        property var volMap: ({})
        // vid -> [{url}] â€” a volume's own extracted pages once it is "ready" on disk
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
    // A separate no-resume progress fake for the shelf-less room below: FakeProgress.get()
    // ignores its seriesId argument (a fixture shortcut, not production behaviour), so
    // sharing `progress` would leak the "S" series' resume record onto the "C" series and
    // mask the honest "Search nyaa" label behind a bogus "Continue" one.
    FakeProgress { id: noResumeProgress; record: null }
    FakeDownloads { id: downloads }

    // Catalogue-shaped rows only: number (string), cover, title â€” no chapterStart/
    // chapterEnd. Volume "9" carries a baked catalogue cover+title (the harvest-covered
    // case); volume "2" carries no catalogue cover but is "ready" with a local first page
    // (the on-disk case); volume "12" carries a redundant "Volume 12" name (must collapse
    // to nothing); every other row is deliberately bare (the NO COVER case).
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
                "title": i === 9 ? "Real Volume Title" : (i === 12 ? "Volume 12" : ""),
                "cover": i === 9 ? "file:///fixtures/vol9-cover.jpg" : "",
                "state": state
            })
        }
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

    function fail(e) {
        console.log("MANGA_READING_ROOM_FAIL: " + e.message)
        Qt.exit(1)
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
                "primaryAction": "get",
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
               "the flow must auto-land on the continue volume index")

            // â”€â”€ the masthead's one contextual action honours the shelf's own truth â”€â”€
            // A shelved series (root.library.showVolumes true) carries no masthead CTA â€”
            // the flow's own action bar is the one contextual action.
            function findByName(item, name) {
                if (!item) return null
                if (item.objectName === name) return item
                var kids = item.children || []
                for (var k = 0; k < kids.length; k++) {
                    var found2 = findByName(kids[k], name)
                    if (found2) return found2
                }
                return null
            }
            ck(findByName(room, "tankobanSeriesPrimaryAction") === null,
               "visible Search Nyaa hero action must stay removed")

            // â”€â”€ catalogue-independence Slice 3: no live thumb scraping, ever â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            ck(downloads.asked.length === 0,
               "the shelf must never call fetchThumb â€” covers are catalogue-baked or local-only now")
            ckNegativeControl(function () {
                downloads.fetchThumb("S", "some-chapter-id")   // simulate a reintroduced call site
                ck(downloads.asked.length === 0, "negative control for fetchThumb-never-called")
            }, "the fetchThumb-never-called assertion must fail when a call site is reintroduced")

            // â”€â”€ catalogue-independence Slice 3: the cover ladder â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
            ck(typeof lib.chapters === "undefined" && typeof lib.curatedCovers === "undefined",
               "the chapters/curated-cover surfaces must be fully removed, not just unused")
            ck(typeof lib.stateWordFor === "undefined" && typeof lib.chipTextFor === "undefined"
               && typeof lib.liveCaptionFor === "undefined" && typeof lib.volumeCaptionFor === "undefined",
               "the pre-v2.3 grid caption vocabulary must be fully removed, not just unused")

            // â”€â”€ v2.3 caption vocabulary: real name, redundant-name collapse, state line â”€â”€
            ck(lib.volumeNameFor(rowByNumber(lib.volumeRows, "9")) === "Real Volume Title",
               "a genuine volume name must reach the caption")
            ck(lib.volumeNameFor(rowByNumber(lib.volumeRows, "12")) === "",
               "a redundant 'Volume N' name must collapse to nothing")
            ck(lib.stateLineFor(rowByNumber(lib.volumeRows, "7")) === "failed",
               "a failed volume's caption state line must read exactly 'failed'")
            ck(lib.stateLineFor(rowByNumber(lib.volumeRows, "6")).indexOf("downloading") >= 0,
               "an in-flight volume's caption state line must name downloading")
            ck(lib.stateLineFor(rowByNumber(lib.volumeRows, "2")) === "",
               "an owned (ready) volume's caption state line is empty â€” Read lives on the action bar")

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
            lib.selecting = false
            lib.selectedNumbers = []
            lib.focusAtNumber("74")

            // catalogue-independence Slice 5 (2026-08-20): chapters are gone completely.
            // A shelf-less series (no volumes known at all) renders an honest empty shelf
            // â€” no chapter tail, no chapter API surface anywhere, on the room or the
            // library it owns. It ALSO gets the masthead CTA back (search primary action,
            // v2.3's own reconciliation) â€” the flow reserves no action bar with zero rows.
            var chapterOnlyComp = Qt.createComponent("../qml/MangaReadingRoom.qml")
            chapterOnlyRoom = chapterOnlyComp.createObject(harness, {
                "width": 1000, "height": 720, "seriesId": "C",
                "seriesTitle": "No Shelf", "primaryAction": "search",
                "service": service, "progress": noResumeProgress, "downloader": downloads
            })
            ck(chapterOnlyRoom.library.showVolumes === false,
               "a series with no known volumes must not expose a populated shelf")
            ck(chapterOnlyRoom.library.volumeRows.length === 0,
               "a series with no known volumes must render zero rows, never a chapter fallback")
            ck(chapterOnlyRoom.library.actionBarHeight === 0,
               "a series with no volumes must not reserve the flow's action bar")
            ck(chapterOnlyRoom.continueText === "Search nyaa",
               "the shelf-less honest primary label must read Search nyaa")
            ck(findByName(chapterOnlyRoom, "tankobanSeriesPrimaryAction") === null,
               "shelf-less room must not resurrect visible Search Nyaa hero action")
            ck(typeof chapterOnlyRoom.chapters === "undefined"
               && typeof chapterOnlyRoom.chapterDisplayRows === "undefined"
               && typeof chapterOnlyRoom.openChapterRequested === "undefined"
               && typeof chapterOnlyRoom.chapterDownloadRequested === "undefined",
               "the room's chapter API (chapters/chapterDisplayRows/chapter signals) must be fully removed")
            ck(typeof chapterOnlyRoom.library.chapters === "undefined"
               && typeof chapterOnlyRoom.library.chapterRows === "undefined"
               && typeof chapterOnlyRoom.library.openChapterRequested === "undefined"
               && typeof chapterOnlyRoom.library.chapterDownloadRequested === "undefined",
               "the shelf's chapter API (chapters/chapterRows/chapter signals) must be fully removed")

            // Special canonical volume tokens must remain exact through focus and
            // selection; they are not guaranteed to be integers.
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

            // â”€â”€ flow-layout-dependent checks (ListView currentIndex settling, delegate
            // realization) run on the same deferred tick the flow's own centreFlow() uses,
            // so they must be read after a Qt.callLater, never in this synchronous call
            // stack â€” checking here would race the deferred positionViewAtIndex and either
            // pass vacuously (stale -1) or fail spuriously. â”€â”€
            Qt.callLater(function () {
                try {
                    ck(lib.flowCurrentIndex === lib.focusIndex,
                       "the flow must keep ListView currentIndex on the focused token")
                    ck(lib.liveVolumeTiles > 0 && lib.liveVolumeTiles < lib.volumeRows.length,
                       "the volume flow must virtualize a long canonical model, live " + lib.liveVolumeTiles)
                    ck(lib.bookHeight >= 190 && lib.bookHeight <= 276,
                       "book height must stay in the normal Tankoban range, got " + lib.bookHeight)
                    ckNegativeControl(function () {
                        ck(lib.flowCurrentIndex === lib.focusIndex + 1, "negative control for flow centring")
                    }, "the flow-centring assertion must fail when the current index is deliberately off by one")

                    Qt.callLater(function () {
                        try {
                            ck(tokenRoom.library.flowCurrentIndex === 1,
                               "named volume token focus must center its actual ListView row")
                            console.log("MANGA_READING_ROOM_OK")
                            Qt.exit(0)
                        } catch (e3) { fail(e3) }
                    })
                } catch (e2) { fail(e2) }
            })
        } catch (e) {
            fail(e)
        }
    }

    Timer { interval: 100; running: true; repeat: false; onTriggered: harness.runChecks() }
    Timer { interval: 8000; running: true
        onTriggered: { console.log("MANGA_READING_ROOM_FAIL timeout"); Qt.exit(1) } }
}
