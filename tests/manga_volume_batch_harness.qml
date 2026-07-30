// Offscreen harness for the volume BATCH DOWNLOAD surface (design 2026-07-30).
//
// tests/manga_volume_batch_test.mjs proves the pure selection maths. THIS proves
// the shelf actually wires it: that a 25-volume series really pages into three
// groups while still rendering all 25 rows, that a page button never offers a
// volume already on the device or already coming, and (from Task 4) that one
// press really dispatches across EVERY volume of a batch rather than the first.
//
// Verdict rides the sentinel + exit code: a thrown QML error HANGS qml.exe
// offscreen, so every check is wrapped in try/catch → Qt.exit(1).
import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

    property var lib: null
    property var page: null
    property var lastBatchNumbers: null
    property string lastBatchLabel: ""

    // ── the fake native service ────────────────────────────────────────────
    // Mirrors MangaTankobanService's QML API over plain in-memory maps, and
    // RECORDS every acquisition call so a batch can be asserted call-by-call.
    component FakeService: QtObject {
        property var modeMap: ({})
        property var volMap: ({})
        property var srcMap: ({})

        // every dispatch, in order — this is what proves a batch is a batch
        property var compiledVols: []
        property var nyaaVols: []
        property var nyaaHashes: []
        property var searched: []

        signal volumesChanged(string seriesId)
        signal sourcesReady(string volumeId, var results)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)

        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function modeEnabled(sid) { return modeMap[sid] === true }
        function setModeEnabled(sid, enabled) { var m = modeMap; m[sid] = enabled; modeMap = m }
        function statusOf(vid) { return {} }
        function localPages(vid) { return [] }
        function searchSources(vid) {
            var s = searched; s.push(String(vid)); searched = s
            sourcesReady(String(vid), srcMap[vid] !== undefined ? srcMap[vid] : [])
        }
        function downloadNyaa(vid, hash) {
            var v = nyaaVols; v.push(String(vid)); nyaaVols = v
            var h = nyaaHashes; h.push(String(hash)); nyaaHashes = h
        }
        function compileWeebCentral(vid) {
            var c = compiledVols; c.push(String(vid)); compiledVols = c
        }
        function cancel(vid) {}
        function remove(vid) { return {} }
    }

    // 25 volumes: enough to prove three pages (10 / 10 / 5) off one series.
    // Volumes 3 and 12 are already on the device; volume 4 is mid-download. All
    // three must be invisible to every batch.
    function buildVolumes() {
        var out = []
        for (var i = 1; i <= 25; i++) {
            var st = (i === 3 || i === 12) ? "ready"
                   : (i === 4) ? "downloading" : "none"
            out.push({ "id": "vol" + i, "seriesId": "S", "number": String(i),
                       "title": "Volume " + i, "cover": "", "chapterCount": 5,
                       "state": st })
        }
        return out
    }

    FakeService {
        id: svc
        volMap: ({ "S": harness.buildVolumes() })
        srcMap: ({
            "vol1": [
                { "kind": "nyaa", "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1",
                  "releaseTitle": "Series v01-25 (Digital)", "uploader": "danke-Empire",
                  "tier": 1, "sizeBytes": 2500000000, "seeders": 88,
                  "coverageLo": "1", "coverageHi": "25", "standalone": false,
                  "digital": true, "enabled": true },
                { "kind": "weebcentral", "label": "Build from chapters", "enabled": true,
                  "chapterCount": 5, "reason": "" }
            ]
        })
    }

    function ck(cond, msg) { if (!cond) throw new Error(msg) }
    function deepEq(a, b, msg) {
        if (JSON.stringify(a) !== JSON.stringify(b))
            throw new Error(msg + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")")
    }
    function range(a, b) { var o = []; for (var i = a; i <= b; i++) o.push(i); return o }

    function setup() {
        var comp = Qt.createComponent("../qml/MangaTankobanLibrary.qml")
        if (comp.status === Component.Error) throw new Error("component: " + comp.errorString())
        harness.lib = comp.createObject(harness, { "service": svc, "seriesId": "S", "width": 620 })
        if (!harness.lib) throw new Error("createObject returned null")
        harness.lib.batchRequested.connect(function (numbers, label) {
            harness.lastBatchNumbers = numbers
            harness.lastBatchLabel = String(label)
        })
    }

    function runChecks() {
        try {
            // ── Task 2: the shelf pages in tens ────────────────────────────
            ck(harness.lib.volumeRows.length === 25, "the series must expose 25 volumes")
            ck(harness.lib.renderedCount === 25,
               "ALL 25 rows must still render, got " + harness.lib.renderedCount)

            var pages = harness.lib.pagedRows
            ck(pages.length === 3, "25 volumes must page into 3 groups, got " + pages.length)
            ck(pages[0].label === "Volumes 1–10", "page 1 label, got " + pages[0].label)
            ck(pages[1].label === "Volumes 11–20", "page 2 label, got " + pages[1].label)
            ck(pages[2].label === "Volumes 21–25", "short final page label, got " + pages[2].label)
            ck(pages[2].volumes.length === 5, "the short final page must hold 5")

            // ── a batch never offers a volume already here or already coming ──
            var un = harness.lib.unavailableNumbers
            ck(un[3] === true, "volume 3 is READY and must be unavailable to a batch")
            ck(un[12] === true, "volume 12 is READY and must be unavailable to a batch")
            ck(un[4] === true, "volume 4 is DOWNLOADING and must be unavailable to a batch")
            ck(un[5] === undefined, "volume 5 is untouched and must be available")

            // page 1 offers 1-10 minus the owned 3 and the in-flight 4
            deepEq(harness.lib.unownedIn(pages[0].volumes), [1, 2, 5, 6, 7, 8, 9, 10],
                   "page 1 must skip the owned 3 and the in-flight 4")
            deepEq(harness.lib.unownedIn(pages[1].volumes),
                   [11, 13, 14, 15, 16, 17, 18, 19, 20], "page 2 must skip the owned 12")
            deepEq(harness.lib.unownedIn(pages[2].volumes), range(21, 25),
                   "page 3 offers all five")

            console.log("MANGA_VOLUME_BATCH_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("MANGA_VOLUME_BATCH_FAIL " + e.message)
            Qt.exit(1)
        }
    }

    Component.onCompleted: {
        try {
            setup()
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("MANGA_VOLUME_BATCH_FAIL setup: " + e.message)
            Qt.exit(1)
        }
    }

    // Safety net: never spin forever offscreen.
    Timer {
        interval: 6000; running: true
        onTriggered: { console.log("MANGA_VOLUME_BATCH_FAIL timeout"); Qt.exit(1) }
    }
}
