// Offscreen logic harness for MangaTankobanLibrary (Task 9).
//
// A grep test proves the wiring strings exist; THIS proves the volume-first
// surface actually behaves. It supplies a FAKE `TankobanVolumes` (an in-memory
// object with the same invokables/signals as the native service), builds the
// library for TWO different series sharing that one service, and proves:
//   * Off is the default, and enabling series A does NOT enable series B
//     (per-series persistence lives in the service, keyed by seriesId).
//   * EVERY canonical volume renders as a row, even one with no source.
//   * The Nyaa cards keep the service's order and the WeebCentral card is LAST.
//   * A `progress` for one volume attaches to that volume's row only — and never
//     to another SERIES' library that shares the same service.
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
        function downloadNyaa(vid, infoHash) { /* recorded elsewhere; no-op here */ }
        function compileWeebCentral(vid) { /* no-op */ }
        function cancel(vid) { /* no-op */ }
        function remove(vid) { /* no-op */ }
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return [] }
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

    property var libA: null
    property var libB: null

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

            // 4. Nyaa cards keep service order; WeebCentral card is LAST.
            harness.libA.chooseSource("volA1")
            var r = harness.libA.expandedByVolume["volA1"]
            ck(r !== undefined && r.length === 3, "volA1 must expose 3 source cards")
            ck(r[0].kind === "nyaa" && r[0].uploader === "Stumbleine", "first card must be the service's first Nyaa row")
            ck(r[1].kind === "nyaa" && r[1].uploader === "danke-Empire", "second card must keep service order")
            ck(r[2].kind === "weebcentral", "the WeebCentral card must be LAST")

            // ...even a volume with no Nyaa source still gets the WeebCentral card last.
            harness.libA.chooseSource("volA3")
            var r3 = harness.libA.expandedByVolume["volA3"]
            ck(r3 !== undefined && r3.length === 1 && r3[0].kind === "weebcentral",
               "a source-less volume must still show the WeebCentral card")

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
