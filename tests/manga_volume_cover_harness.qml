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
            ],
            // 12 volumes = two pages. Proves the shelf asks for the page on screen
            // and NOT for the whole series (eyes-on 2026-07-31: One Piece queued
            // 115 scrapes at once, WeebCentral throttled, and the later volumes
            // plus the chapter rows were left permanently blank).
            "BIG": harness.bigVolumes()
        })
        localMap: ({ "v3": [{ "index": 0, "url": "file:///local/v3/page0.png" }] })
    }

    // volume N covers chapters (N-1)*10+1 .. N*10
    function bigVolumes() {
        var out = []
        for (var i = 1; i <= 115; i++)
            out.push({ "id": "b" + i, "seriesId": "BIG", "number": String(i),
                       "title": "Book " + i,
                       "chapterStart": String((i - 1) * 10 + 1),
                       "chapterEnd": String(i * 10),
                       "cover": "", "state": "none" })
        return out
    }
    function bigChapters() {
        var out = []
        for (var i = 1; i <= 1150; i++) out.push({ "id": "bc" + i, "number": i, "name": "" })
        return out
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
    function askedCount(cid) {
        var n = 0
        for (var i = 0; i < dl.asked.length; i++)
            if (dl.asked[i].chapterId === cid) n++
        return n
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

            // 7. AN EMPTY REPLY MUST BE RETRYABLE, a cover must not be re-asked.
            //    Before 2026-07-31 an empty answer was final on BOTH sides — the
            //    QML kept the request recorded and MangaDownloader cached the empty
            //    string, so fetchThumb returned it forever. A volume that lost its
            //    scrape to a throttle showed a numbered placeholder for the whole
            //    session. Step 4's empty on c9 must therefore have been re-asked.
            ck(dl.asked.length === before + 1,
               "the volume whose scrape came back EMPTY must be asked again")
            ck(harness.askedCount("c9") === 2, "c9 is retried after its empty reply")
            ck(harness.askedCount("c1") === 1,
               "a volume that already HAS a cover is never re-asked")

            // 8. switching series drops the old covers
            harness.lib.seriesId = "OTHER"
            ck(harness.lib.coverFor(svc.volMap["S"][0]) === "",
               "covers must not survive a series change")

            // 9. THE BURST: a paged shelf asks only for the page ON SCREEN.
            // Drop the old chapter list FIRST: series S's chapters (1, 9, 18…)
            // fall inside BIG's volume ranges too, so leaving them attached would
            // have the shelf ask for them against the new series - a fixture
            // artifact, not product behaviour, but it would mask the real count.
            harness.lib.chapters = []
            harness.lib.seriesId = "BIG"
            dl.asked = []
            harness.lib.chapters = harness.bigChapters()
            ck(harness.lib.volumeRows.length === 115, "the big series has 115 volumes")
            ck(dl.asked.length > 0 && dl.asked.length < 115,
               "only the viewport may be asked for, got " + dl.asked.length)
            ck(harness.askedFor("bc1"),
               "the initial viewport must ask for the first volume cover")
            ck(!harness.askedFor("bc1150"),
               "the last volume must not be requested on initial open")

            // 10. turning the page asks for the rest — and only the rest.
            harness.lib.jumpToNumber(101)
            ck(harness.askedFor("bc1001"), "jumping near volume 101 asks for its cover")
            ck(dl.asked.length < 115, "jumping must remain bounded, never all volumes")

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
