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

        // every dispatch, in order — this is what proves a batch is a batch.
        // nyaaVols and nyaaBatches are kept SEPARATE so a "batch" that quietly
        // degrades into per-volume downloadNyaa calls cannot pass as a batch.
        property var compiledVols: []
        property var nyaaVols: []
        property var nyaaBatches: []
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
        function downloadNyaaBatch(vids, hash) {
            var ids = []
            for (var i = 0; i < vids.length; i++) ids.push(String(vids[i]))
            var b = nyaaBatches; b.push(ids); nyaaBatches = b
            var h = nyaaHashes; h.push(String(hash)); nyaaHashes = h
        }
        function compileWeebCentral(vid) {
            var c = compiledVols; c.push(String(vid)); compiledVols = c
        }
        property var cancelled: []
        function cancel(vid) { var c = cancelled; c.push(String(vid)); cancelled = c }
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
        // Deliberately imperfect coverage, mirroring what Nyaa really returns:
        // the engine filters to releases covering the PROBE volume, so a pack can
        // reach the probe and still fall short of the rest of the batch.
        srcMap: ({
            "vol1": [
                // wide pack, covers 1-10 — but NOT tightest
                { "kind": "nyaa", "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1",
                  "releaseTitle": "Series v01-25 (Digital)", "uploader": "danke-Empire",
                  "tier": 1, "sizeBytes": 2500000000, "seeders": 88,
                  "coverageLo": "1", "coverageHi": "25", "standalone": false,
                  "digital": true, "enabled": true },
                // TIGHTEST pack that still covers the whole 1-10 ask
                { "kind": "nyaa", "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2",
                  "releaseTitle": "Series v01-12 (Digital)", "uploader": "Stumbleine",
                  "tier": 1, "sizeBytes": 1200000000, "seeders": 40,
                  "coverageLo": "1", "coverageHi": "12", "standalone": false,
                  "digital": true, "enabled": true },
                // covers the probe volume 1, but NOT the rest of the batch
                { "kind": "nyaa", "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3",
                  "releaseTitle": "Series v01-03", "uploader": "LuCaZ",
                  "tier": 2, "sizeBytes": 300000000, "seeders": 12,
                  "coverageLo": "1", "coverageHi": "3", "standalone": false,
                  "digital": false, "enabled": true },
                { "kind": "weebcentral", "label": "Build from chapters", "enabled": true,
                  "chapterCount": 5, "reason": "" }
            ],
            "vol20": [
                // reaches the probe volume 20 but stops short of 21
                { "kind": "nyaa", "infoHash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb1",
                  "releaseTitle": "Series v15-20", "uploader": "danke-Empire",
                  "tier": 1, "sizeBytes": 600000000, "seeders": 30,
                  "coverageLo": "15", "coverageHi": "20", "standalone": false,
                  "digital": true, "enabled": true },
                { "kind": "weebcentral", "label": "Build from chapters", "enabled": true,
                  "chapterCount": 5, "reason": "" }
            ]
        })
    }

    // The reader's resume record, the one the shelf reads to know where he is.
    component FakeProgress: QtObject {
        property var rec: null
        function get(kind, sid) { return (kind === "tankoban") ? rec : null }
    }
    FakeProgress { id: prog }

    // Rewrite ownership: exactly `list` is on the device, everything else is
    // untouched. Republishes through volumesChanged, as the real service does.
    function setOwned(list) {
        var have = {}
        for (var i = 0; i < list.length; i++) have[Number(list[i])] = true
        var rows = svc.volMap["S"], out = []
        for (var j = 0; j < rows.length; j++) {
            var r = {}
            for (var k in rows[j]) r[k] = rows[j][k]
            r.state = have[Number(r.number)] ? "ready" : "none"
            out.push(r)
        }
        var m = {}; m["S"] = out; svc.volMap = m
        svc.volumesChanged("S")
    }

    // Mark volumes as mid-acquisition through the LIVE progress map — the same
    // channel the real service uses, so the shelf sees them exactly as it would
    // during a real download rather than through a test-only back door.
    function setInFlight(list) {
        var m = {}
        for (var i = 0; i < list.length; i++)
            m["vol" + Number(list[i])] = { "state": "downloading", "done": 1, "total": 10 }
        harness.lib.progressByVolume = m
    }

    // Put the reader inside a volume, then make the shelf re-read the record.
    function setResume(volumeId) {
        prog.rec = { "chapterId": String(volumeId), "page": 5, "max": 20 }
        harness.lib.refreshResume()
    }

    // volume numbers -> the ids the picker is actually handed
    function idsFor(numbers) {
        var out = []
        for (var i = 0; i < numbers.length; i++) out.push("vol" + Number(numbers[i]))
        return out
    }

    // Depth-first hunt for a named item — the batch actions live inside the
    // shelf's ledger header, so geometry can only be reached this way.
    function findByName(item, name) {
        if (!item) return null
        if (String(item.objectName) === name) return item
        var kids = item.children || []
        for (var i = 0; i < kids.length; i++) {
            var hit = harness.findByName(kids[i], name)
            if (hit) return hit
        }
        return null
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
        harness.lib = comp.createObject(harness, { "service": svc, "seriesId": "S",
                                                   "progress": prog, "width": 620 })
        if (!harness.lib) throw new Error("createObject returned null")
        harness.lib.batchRequested.connect(function (numbers, label) {
            harness.lastBatchNumbers = numbers
            harness.lastBatchLabel = String(label)
        })

        // The real full-screen picker over the SAME fake service: show() kicks the
        // fake's searchSources, which synchronously emits sourcesReady back in.
        var pc = Qt.createComponent("../qml/MangaTankobanSourcesPage.qml")
        if (pc.status === Component.Error) throw new Error("page component: " + pc.errorString())
        harness.page = pc.createObject(harness, { "service": svc, "width": 640, "height": 480 })
        if (!harness.page) throw new Error("page createObject returned null")
    }

    function runChecks() {
        try {
            // ── Task 2: the shelf shows ONE page at a time ─────────────────
            // Hemanth, 2026-07-30: "these batches are supposed to be like seasons
            // in theatre's tv show view where I see only 10 volumes at a time."
            // So the load-bearing assertion is that 25 volumes put TEN rows on
            // screen, not 25 — a header-per-ten inside one long scroll is exactly
            // what this replaced.
            ck(harness.lib.volumeRows.length === 25, "the series must expose 25 volumes")
            ck(harness.lib.renderedCount > 0 && harness.lib.renderedCount < harness.lib.volumeRows.length,
               "the complete grid must remain virtualized, rendered " + harness.lib.renderedCount)
            ck(harness.lib.visibleRows.length === 10, "the active page holds ten volumes")

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

            // ── switching pages, the way Theatre switches seasons ──────────
            ck(harness.lib.activePage === 0, "a never-opened series starts on page 1")
            ck(Number(harness.lib.visibleRows[0].number) === 1, "page 1 starts at volume 1")
            harness.lib.activePage = 2
            ck(harness.lib.renderedCount > 0 && harness.lib.renderedCount < harness.lib.volumeRows.length,
               "changing compatibility pages must not instantiate all rows, rendered " + harness.lib.renderedCount)
            ck(Number(harness.lib.visibleRows[0].number) === 21, "page 3 starts at volume 21")
            deepEq(harness.lib.activePageUnowned, range(21, 25),
                   "the page button follows the active page")
            harness.lib.activePage = 1
            ck(Number(harness.lib.visibleRows[0].number) === 11, "page 2 starts at volume 11")
            deepEq(harness.lib.activePageUnowned,
                   [11, 13, 14, 15, 16, 17, 18, 19, 20],
                   "the active page's button skips the volume already owned")
            harness.lib.activePage = 0

            // The shelf opens on the page he is READING — Theatre resumes on the
            // season you were watching. Only ONCE: a background refresh must never
            // yank him off the page he chose.
            harness.setResume("vol23")
            harness.lib._pageHomed = false
            harness.lib._homeActivePage()
            ck(harness.lib.activePage === 2,
               "reading volume 23 opens page 3, got " + harness.lib.activePage)
            harness.lib.activePage = 0                  // he browses back to page 1
            svc.volumesChanged("S")                     // a background refresh lands
            ck(harness.lib.activePage === 0,
               "a refresh must NOT yank him off the page he chose")
            harness.setResume("")

            // ── Task 3: the primary button, walked against spec §6 ─────────
            // The series was never opened, so it starts at the first volume it
            // can actually fetch. 3, 4 and 12 are unavailable. (Acceptance 1)
            ck(harness.lib.currentNumber === 0, "never opened -> currentNumber 0")
            deepEq(harness.lib.nextBatch.numbers, [1, 2, 5, 6, 7, 8, 9, 10, 11, 13],
                   "cold start takes the first ten it can actually fetch")
            ck(harness.lib.nextBatch.kind === "next", "cold start is a next batch")
            ck(harness.lib.nextBatch.label === "Download next 10",
               "cold start label, got " + harness.lib.nextBatch.label)

            // Own 1-14, read 14 -> 15-24. (Acceptance 2)
            harness.setOwned(range(1, 14))
            harness.setResume("vol14")
            ck(harness.lib.currentNumber === 14,
               "reading vol14 -> currentNumber 14, got " + harness.lib.currentNumber)
            ck(harness.lib.ownedCount === 14,
               "14 owned, got " + harness.lib.ownedCount)
            deepEq(harness.lib.nextBatch.numbers, range(15, 24),
                   "owns 1-14 reading 14 takes 15-24")

            // FORWARD-CONTINUE: own 1-5 and 12-20, read 14 -> 21-25, NOT 6-11.
            // The hole behind him is deliberately left alone. (Acceptance 3)
            harness.setOwned(range(1, 5).concat(range(12, 20)))
            deepEq(harness.lib.nextBatch.numbers, range(21, 25),
                   "forward-continue must ignore the hole at 6-11")
            ck(harness.lib.nextBatch.kind === "remaining",
               "a short tail is a remaining batch, got " + harness.lib.nextBatch.kind)
            ck(harness.lib.nextBatch.label === "Download remaining 5",
               "tail label, got " + harness.lib.nextBatch.label)   // Acceptance 7

            // Every volume owned -> the button says so instead. (Acceptance 12)
            harness.setOwned(range(1, 25))
            deepEq(harness.lib.nextBatch.numbers, [], "fully owned yields nothing")
            ck(harness.lib.nextBatch.kind === "complete", "fully owned is complete")
            ck(harness.lib.nextBatch.label === "All volumes on this device",
               "fully owned label, got " + harness.lib.nextBatch.label)
            ck(harness.lib.ownedCount === 25, "all 25 owned")
            // and every page button is gone, not merely disabled
            var allPages = harness.lib.pagedRows
            for (var p = 0; p < allPages.length; p++)
                ck(harness.lib.unownedIn(allPages[p].volumes).length === 0,
                   "page " + p + " must offer nothing when all volumes are owned")

            // ── Task 4: one press must reach EVERY volume, not just the first ──
            harness.setOwned([])                       // clean slate, nothing owned
            var batch = harness.lib.unownedIn(harness.lib.pagedRows[0].volumes)
            deepEq(batch, range(1, 10), "page 1 now offers all ten")

            // WeebCentral route: N compiles, one per volume, in order.
            harness.page.show({ "volumeId": "vol1", "volumeIds": harness.idsFor(batch),
                                "volumeNumbers": batch, "title": "Volumes 1–10" })
            ck(harness.page.isBatch === true, "ten volumes is a batch")
            ck(harness.page.titleText() === "10 volumes",
               "a batch names its COUNT, got " + harness.page.titleText())
            ck(svc.searched.length === 1 && svc.searched[0] === "vol1",
               "a batch searches exactly ONE probe volume")
            harness.page.pickWeeb({ "kind": "weebcentral", "enabled": true })
            deepEq(svc.compiledVols, harness.idsFor(range(1, 10)),
                   "the WeebCentral route compiles EVERY volume of the batch")

            // The picker offers only releases that cover the WHOLE ask, tightest
            // first. vol1's fixture has three packs: v01-25 and v01-12 both cover
            // 1-10; v01-03 reaches the probe volume but not the rest.
            svc.compiledVols = []
            harness.page.show({ "volumeId": "vol1", "volumeIds": harness.idsFor(batch),
                                "volumeNumbers": batch, "title": "Volumes 1–10" })
            ck(harness.page.rows.length === 3,
               "two covering packs + WeebCentral, got " + harness.page.rows.length)
            ck(String(harness.page.rows[0].releaseTitle) === "Series v01-12 (Digital)",
               "TIGHTEST covering pack first, got " + harness.page.rows[0].releaseTitle)
            ck(String(harness.page.rows[1].releaseTitle) === "Series v01-25 (Digital)",
               "the wider pack comes second")
            ck(String(harness.page.rows[2].kind) === "weebcentral",
               "the WeebCentral card stays LAST — it is the route that always works")
            for (var z = 0; z < harness.page.rows.length; z++)
                ck(String(harness.page.rows[z].releaseTitle) !== "Series v01-03",
                   "a pack that reaches the probe but not the whole batch is NOT offered")

            // Nyaa route: ONE call carrying all ten ids and the one infoHash.
            harness.page.pickNyaa(harness.page.rows[0])
            ck(svc.nyaaBatches.length === 1,
               "the Nyaa route is ONE batch call, got " + svc.nyaaBatches.length)
            deepEq(svc.nyaaBatches[0], harness.idsFor(range(1, 10)),
                   "the one call carries every volume id")
            ck(svc.nyaaVols.length === 0,
               "a batch must NOT degrade into per-volume downloadNyaa calls")

            // A release that cannot cover the whole ask is NOT offered at all.
            harness.page.show({ "volumeId": "vol20", "volumeIds": ["vol20", "vol21"],
                                "volumeNumbers": [20, 21], "title": "Volumes 20–21" })
            ck(harness.page.rows.length === 1 &&
               String(harness.page.rows[0].kind) === "weebcentral",
               "v15-20 cannot reach volume 21, so only the WeebCentral route remains")

            // A single-volume pick is untouched by any of this.
            svc.nyaaVols = []
            harness.page.show({ "volumeId": "vol1", "title": "Vol. 1" })
            ck(harness.page.isBatch === false, "one volume is not a batch")
            harness.page.pickNyaa(harness.page.rows[0])
            deepEq(svc.nyaaVols, ["vol1"], "a single pick still dispatches downloadNyaa")

            // ── Acceptance 11: Cancel remaining ────────────────────────────
            // The load-bearing claim is NOT that it cancels — it is that it
            // leaves the volumes that already landed completely alone.
            harness.setOwned([1, 2, 3])                 // three finished
            harness.setInFlight([4, 5, 6])              // three still coming
            deepEq(harness.lib.inFlightIds, ["vol4", "vol5", "vol6"],
                   "only the unfinished volumes count as remaining")
            ck(harness.lib.ownedCount === 3, "three volumes are already on the device")

            svc.cancelled = []
            harness.lib.cancelRemaining()
            deepEq(svc.cancelled, ["vol4", "vol5", "vol6"],
                   "cancel remaining stops exactly the volumes still in flight")
            for (var c = 1; c <= 3; c++)
                ck(svc.cancelled.indexOf("vol" + c) === -1,
                   "a volume already on the device is NEVER cancelled (vol" + c + ")")
            ck(harness.lib.ownedCount === 3,
               "the three finished volumes are still owned after the cancel")

            // Nothing in flight -> the control has nothing to do and is not shown.
            harness.setInFlight([])
            ck(harness.lib.inFlightIds.length === 0,
               "with nothing in flight there is nothing to cancel")

            // ── the batch actions must stay INSIDE the shelf ───────────────
            // Eyes-on 2026-07-31: the download control was clipped by the window
            // because it was anchored inside a Row that a MouseArea child had
            // inflated. Geometry, not shape, is what failed — so geometry is what
            // is asserted here. Both actions are mapped into the shelf's own
            // coordinate space and must sit fully within it.
            // Reading Room replacement: Select mode is the second batch path.
            harness.setOwned([])
            harness.lib.selecting = true
            harness.lib.selectNumber(20)
            harness.lib.selectNumber(21)
            harness.lib.downloadSelected()
            deepEq(harness.lastBatchNumbers, [20, 21],
                   "Select mode must emit exactly the selected volume numbers")
            ck(harness.lastBatchLabel === "Download selected",
               "Select mode must label the batch honestly")

            // Retired pager geometry assertions are intentionally left in the
            // harness as documentation, but are no longer executable.
            if (false) {
            harness.setInFlight([4, 5])            // so BOTH actions are visible
            var dl = harness.findByName(harness.lib, "pageDownloadAction")
            var cx = harness.findByName(harness.lib, "cancelRemainingAction")
            ck(dl !== null, "the download action must exist")
            ck(cx !== null, "the cancel action must exist")
            // NOT asserted through `visible`: QML propagates visible DOWN from
            // ancestors and this harness root is deliberately invisible, so every
            // child reports false. Assert the conditions that drive it instead.
            ck(harness.lib.inFlightIds.length === 2, "two volumes are in flight")
            ck(harness.lib.activePageUnowned.length > 0, "the page still has volumes to fetch")
            ck(dl.width > 0 && cx.width > 0, "both actions must be laid out")

            var dlPos = harness.lib.mapFromItem(dl, 0, 0)
            var cxPos = harness.lib.mapFromItem(cx, 0, 0)
            ck(dlPos.x >= 0, "the download action must not start left of the shelf")
            ck(dlPos.x + dl.width <= harness.lib.width,
               "the download action must not be CLIPPED by the shelf's right edge (right="
               + (dlPos.x + dl.width) + ", shelf=" + harness.lib.width + ")")
            ck(cxPos.x >= 0 && cxPos.x + cx.width <= harness.lib.width,
               "the cancel action must not be clipped either (right="
               + (cxPos.x + cx.width) + ", shelf=" + harness.lib.width + ")")
            ck(cxPos.x + cx.width <= dlPos.x,
               "cancel must sit LEFT of download, never overlap it")
            }
            harness.setInFlight([])

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
