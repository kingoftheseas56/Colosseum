// Offscreen contract for VOLUME COVERS.
//
// A volume's cover is the first page of its FIRST chapter, scraped on demand the
// same way a chapter row gets its own thumbnail: Downloads.fetchThumb(seriesId,
// chapterId) -> thumbReady(chapterId, url). Chapter thumbnails are NOT in
// WeebCentral's chapter-list HTML, which is why looking there found nothing and
// covers were briefly hardcoded empty (2026-07-30). This pins the wiring so that
// cannot happen silently again.
//
// Drives the real MangaTankobanLibrary against fakes for both seams.
// Verdict rides the sentinel + exit code: a thrown QML error HANGS qml.exe
// offscreen, so every check is wrapped in try/catch -> Qt.exit(1).
import QtQuick

Item {
    id: harness
    width: 1200; height: 800
    visible: false

    // records every fetchThumb call, and replies only when told to
    component FakeDownloads: QtObject {
        property var asked: []                 // [{seriesId, chapterId}]
        signal thumbReady(string chapterId, string url)
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
        signal removed(string cid)
        function fetchThumb(seriesId, chapterId) {
            var a = asked.slice()
            a.push({ "seriesId": String(seriesId), "chapterId": String(chapterId) })
            asked = a
        }
    }

    component FakeService: QtObject {
        property var volMap: ({})
        property var localMap: ({})            // volumeId -> [pages]
        signal volumesChanged(string seriesId)
        signal sourcesReady(string volumeId, var results)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function modeEnabled(sid) { return true }
        function setModeEnabled(sid, on) {}
        function searchSources(vid) {}
        function downloadNyaa(vid, h) {}
        function compileWeebCentral(vid) {}
        function cancel(vid) {}
        function remove(vid) {}
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return localMap[vid] !== undefined ? localMap[vid] : [] }
    }

    component FakeProgress: QtObject {
        function get(kind, id) { return ({}) }
    }

    FakeDownloads { id: dl }
    FakeProgress { id: prog }
    FakeService {
        id: svc
        volMap: ({
            "S": [
                { "id": "v1", "seriesId": "S", "number": "1", "title": "Romance Dawn",
                  "chapterStart": "1", "chapterEnd": "8", "cover": "", "state": "none" },
                { "id": "v2", "seriesId": "S", "number": "2", "title": "Buggy the Clown",
                  "chapterStart": "9", "chapterEnd": "17", "cover": "", "state": "none" },
                // a DOWNLOADED volume — must show its OWN first page, not a scrape
                { "id": "v3", "seriesId": "S", "number": "3", "title": "Don't Get Fooled",
                  "chapterStart": "18", "chapterEnd": "26", "cover": "", "state": "ready" }
            ]
        })
        localMap: ({ "v3": [{ "index": 0, "url": "file:///local/v3/page0.png" }] })
    }

    // deliberately out of order, and 8.5 exists so "first" must mean LOWEST, not first-seen
    readonly property var chapterList: [
        { "id": "c8_5", "number": 8.5, "name": "Omake" },
        { "id": "c9",   "number": 9,   "name": "" },
        { "id": "c1",   "number": 1,   "name": "" },
        { "id": "c18",  "number": 18,  "name": "" },
        { "id": "c2",   "number": 2,   "name": "" }
    ]

    property var lib: null
    function ck(cond, msg) { if (!cond) throw new Error(msg) }
    function askedFor(cid) {
        for (var i = 0; i < dl.asked.length; i++)
            if (dl.asked[i].chapterId === cid) return true
        return false
    }

    function runChecks() {
        try {
            var comp = Qt.createComponent("../qml/MangaTankobanLibrary.qml")
            if (comp.status === Component.Error) throw new Error("component: " + comp.errorString())
            harness.lib = comp.createObject(harness, {
                "service": svc, "progress": prog, "downloader": dl,
                "seriesId": "S", "width": 1200
            })
            ck(harness.lib, "createObject returned null")

            // 1. no chapters yet -> nothing can be asked for. A cover needs BOTH.
            ck(dl.asked.length === 0, "no chapter list must mean no thumb requests")

            // 2. chapters land -> one request per volume, keyed on its FIRST chapter
            harness.lib.chapters = harness.chapterList
            ck(harness.askedFor("c1"), "volume 1 must ask for chapter 1 (its lowest)")
            ck(harness.askedFor("c9"), "volume 2 must ask for chapter 9")
            ck(harness.askedFor("c18"), "volume 3 must ask for chapter 18")
            ck(!harness.askedFor("c2"), "a volume must ask for ONE chapter, not every chapter")
            ck(!harness.askedFor("c8_5"), "8.5 is inside volume 1 but is not its lowest chapter")

            // 3. a reply routes back to the volume that asked
            dl.thumbReady("c1", "https://cdn/one/p0.jpg")
            ck(harness.lib.coverFor(svc.volMap["S"][0]) === "https://cdn/one/p0.jpg",
               "volume 1 must wear the page its own chapter returned")
            ck(harness.lib.coverFor(svc.volMap["S"][1]) === "",
               "a reply must not leak onto a volume that did not ask for it")

            // 4. an empty reply is not a cover
            dl.thumbReady("c9", "")
            ck(harness.lib.coverFor(svc.volMap["S"][1]) === "",
               "an empty url must never become a cover")

            // 5. a DOWNLOADED volume shows its own first page - the real book
            ck(harness.lib.coverFor(svc.volMap["S"][2]) === "file:///local/v3/page0.png",
               "a downloaded volume must show its own first page")

            // 6. the request map ACCUMULATES: a second pass must not orphan the
            //    replies still in flight from the first (the bug this pins).
            var before = dl.asked.length
            harness.lib.requestCovers()
            dl.thumbReady("c9", "https://cdn/two/p0.jpg")
            ck(harness.lib.coverFor(svc.volMap["S"][1]) === "https://cdn/two/p0.jpg",
               "a reply must still route after a second requestCovers pass")

            // 7. already-covered volumes are not re-requested
            ck(dl.asked.length === before, "a volume that already has a cover must not be re-asked")

            // 8. switching series drops the old covers
            harness.lib.seriesId = "OTHER"
            ck(harness.lib.coverFor(svc.volMap["S"][0]) === "",
               "covers must not survive a series change")

            console.log("MANGA_VOLUME_COVER_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("MANGA_VOLUME_COVER_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Component.onCompleted: Qt.callLater(runChecks)
    Timer { interval: 6000; running: true
        onTriggered: { console.log("MANGA_VOLUME_COVER_FAIL timeout"); Qt.exit(1) } }
}
