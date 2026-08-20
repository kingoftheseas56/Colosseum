// RED/GREEN contract harness for MangaSeries.qml's catalogue-fed identity + masthead
// (catalogue-independence Slice 2, amended 2026-08-20). Instantiates the real page bare
// (Qt.createComponent + createObject) with fake MalCatalog/TankobanCatalog/TankobanVolumes
// seams injected via the page's own injectable properties (malCatalogRef/tankobanCatalogRef/
// tankobanVolumesRef) — no network, no live app state, mirrors manga_reading_room_harness's
// house convention (sentinel + Qt.exit).
import QtQuick

Item {
    id: harness
    width: 1440
    height: 900
    visible: true

    component FakeMalCatalog: QtObject {
        property bool readyVal: true
        property var rows: ({})       // malId(string) -> Jikan-shaped manga row
        property var titleMap: ({})   // title -> [malId, ...] (matchByTitle candidates)
        function ready() { return readyVal }
        function mangaById(id) {
            var r = rows[String(id)]
            return r !== undefined ? r : ({})
        }
        function matchByTitle(title, year, medium) {
            var ids = titleMap[title] || []
            var out = []
            for (var i = 0; i < ids.length; i++) out.push({ "mal_id": ids[i] })
            return out
        }
    }

    component FakeTankobanCatalog: QtObject {
        property bool readyVal: true
        property var infoMap: ({})    // malId(string) -> {volumeCount, countBasis}
        function ready() { return readyVal }
        function seriesInfo(id) {
            var v = infoMap[String(id)]
            return v !== undefined ? v : ({ "volumeCount": 0, "countBasis": "" })
        }
    }

    // Same shape as manga_reading_room_harness.qml's FakeService — the nested
    // MangaReadingRoom -> MangaTankobanLibrary chain needs the full surface.
    component FakeVolumesService: QtObject {
        property var volMap: ({})     // seriesId -> [{number,state,...}]
        signal volumesChanged(string seriesId)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return [] }
        function cancel(vid) {}
    }

    FakeMalCatalog { id: malCatalog }
    FakeTankobanCatalog { id: tankCatalog }
    FakeVolumesService { id: volService }

    function ck(condition, message) {
        if (!condition) throw new Error(message)
    }

    property var pageComp: null
    function makePage(malId, seriesTitle) {
        if (!pageComp) {
            pageComp = Qt.createComponent("../qml/MangaSeries.qml")
            if (pageComp.status === Component.Error)
                throw new Error("MangaSeries component: " + pageComp.errorString())
        }
        var p = pageComp.createObject(harness, {
            "width": 1320, "height": 860,
            "malCatalogRef": malCatalog, "tankobanCatalogRef": tankCatalog,
            "tankobanVolumesRef": volService
        })
        if (!p) throw new Error("MangaSeries createObject returned null")
        // Mirror production's exact sequencing (Main.qml: malId set BEFORE seriesTitle —
        // that is what triggers the one resolve() via onSeriesTitleChanged).
        p.malId = malId || ""
        p.seriesTitle = seriesTitle || ""
        return p
    }

    // Case 4's sub-cases live behind one flag so the negative control (flip the "get"
    // expectation) touches exactly one line and is trivially revertible.
    property bool _negControlFlipGet: false

    function runChecks() {
        try {
            var monsterRow = {
                "mal_id": 1, "title": "Monster", "title_english": "Monster",
                "score": 9.1, "members": 500000, "status": "Finished", "volumes": 18,
                "year": 1994, "published": { "prop": { "from": { "year": 1994 } } },
                "images": { "jpg": { "large_image_url": "http://cover/1" } },
                "synopsis": "A synopsis about a doctor and a monster.",
                "authors": [ { "name": "Naoki Urasawa" } ],
                "genres": [ { "name": "Mystery" }, { "name": "Seinen" } ]
            }

            // ── Case 1: malId-open renders fake-row facts, ready==true, seriesId=="mal:1" ──
            malCatalog.rows = ({ "1": monsterRow })
            malCatalog.titleMap = ({})
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })   // nothing downloaded yet

            var p1 = makePage("1", "Monster")
            ck(p1.loading === false, "case1: malId-open resolves synchronously (loading false)")
            ck(p1.resolvedMalId === 1, "case1: resolvedMalId must be 1, got " + p1.resolvedMalId)
            ck(p1.seriesId === "mal:1", "case1: seriesId must be mal:1, got " + p1.seriesId)
            ck(p1.seriesTitle === "Monster", "case1: displayTitle (seriesTitle) must be Monster")
            ck(p1.author === "Naoki Urasawa", "case1: author must come from the catalogue authors list")
            ck(p1.status === "Finished", "case1: status must come from the catalogue row")
            ck(p1.year === 1994, "case1: year must come from the catalogue row")
            ck(Math.abs(p1.score - 9.1) < 0.001, "case1: score must come from the catalogue row")
            ck(p1.synopsis.indexOf("doctor") >= 0, "case1: synopsis must come from the catalogue row")
            ck(p1.cover === "http://cover/1",
               "case1: poster must come from images.jpg.large_image_url, got " + p1.cover)
            ck(p1.hasShelf === true, "case1: hasShelf must be true when catalogue volumeCount>0")
            ck(p1.primaryAction === "get",
               "case1: primaryAction must be get (shelf present, nothing downloaded), got " + p1.primaryAction)
            ck(p1.errorMsg === "", "case1: errorMsg must stay empty (no WeebCentral search fired)")

            // ── Case 2: exact-title resolve — a single matchByTitle candidate resolves
            //    the SAME way malId-open does (masthead facts still come from mangaById) ──
            malCatalog.titleMap = ({ "Monster": [1] })
            var p2 = makePage("", "Monster")
            ck(p2.resolvedMalId === 1, "case2: single exact-title candidate must resolve to malId 1")
            ck(p2.seriesId === "mal:1", "case2: exact-title resolve must set seriesId mal:1")
            ck(p2.author === "Naoki Urasawa", "case2: masthead facts must come from mangaById after title resolve")
            ck(p2.hasShelf === true, "case2: exact-title resolve must still see the catalogue shelf")

            // ── Case 3: ambiguous / unmatched title -> the honest shelf-less page ──
            malCatalog.titleMap = ({ "Ambiguous Title": [1, 2] })
            var p3a = makePage("", "Ambiguous Title")
            ck(p3a.resolvedMalId === 0, "case3a: an ambiguous title must leave resolvedMalId 0")
            ck(p3a.seriesId === "", "case3a: an ambiguous title must leave seriesId empty")
            ck(p3a.hasShelf === false, "case3a: an ambiguous title must show no shelf")
            ck(p3a.primaryAction === "search",
               "case3a: an ambiguous title's primaryAction must be search, got " + p3a.primaryAction)
            ck(p3a.loading === false, "case3a: the page must still reveal (never hang) when ambiguous")

            malCatalog.titleMap = ({})   // "Unknown Series" -> zero candidates
            var p3b = makePage("", "Unknown Series")
            ck(p3b.resolvedMalId === 0, "case3b: an unmatched title must leave resolvedMalId 0")
            ck(p3b.seriesId === "", "case3b: an unmatched title must leave seriesId empty")
            ck(p3b.primaryAction === "search",
               "case3b: an unmatched title's primaryAction must be search, got " + p3b.primaryAction)

            // ── Case 4: the primary-button truth table ──
            // 4a: volume 1 downloaded -> "open" wins over a present shelf
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [ { "number": "1", "state": "ready" } ] })
            var p4a = makePage("1", "Monster")
            ck(p4a.primaryAction === "open",
               "case4a: volume 1 ready must give primaryAction open, got " + p4a.primaryAction)

            // 4b: shelf known, nothing downloaded -> "get"
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p4b = makePage("1", "Monster")
            // NEGATIVE CONTROL toggle (flip the "get" expectation) — flip _negControlFlipGet
            // to true, rerun, confirm exactly this line reds, then flip back and restore green.
            var expectGet = harness._negControlFlipGet ? "open" : "get"
            ck(p4b.primaryAction === expectGet,
               "case4b: a known shelf with nothing downloaded must give primaryAction get, got " + p4b.primaryAction)

            // 4c: no shelf at all -> "search"
            tankCatalog.infoMap = ({ "1": { "volumeCount": 0, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p4c = makePage("1", "Monster")
            ck(p4c.primaryAction === "search",
               "case4c: no shelf must give primaryAction search, got " + p4c.primaryAction)

            // ── Case 5: the WC error-copy path is gone ──
            tankCatalog.infoMap = ({ "1": { "volumeCount": 18, "countBasis": "mal" } })
            volService.volMap = ({ "mal:1": [] })
            var p5 = makePage("1", "Monster")
            ck(p5.errorMsg === "", "case5: errorMsg must never be set (no WeebCentral search/error path)")
            ck(p5.errorText === "", "case5: errorText must never surface WeebCentral copy")
            ck(p5.errorText.indexOf("WeebCentral") < 0, "case5: no WeebCentral string reaches errorText")

            console.log("MANGA_SERIES_CATALOGUE_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("MANGA_SERIES_CATALOGUE_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Timer { interval: 100; running: true; repeat: false; onTriggered: harness.runChecks() }
    Timer { interval: 8000; running: true
        onTriggered: { console.log("MANGA_SERIES_CATALOGUE_FAIL timeout"); Qt.exit(1) } }
}
