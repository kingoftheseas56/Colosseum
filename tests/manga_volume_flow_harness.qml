// manga_volume_flow_harness.qml — deterministic gate for the v2.3 MangaTankobanLibrary flow
// (arc-08, adopted 2026-08-21). Pins: flow centring/resume, the dynamic cover clamp (never
// cropped), virtualization, stable Read/Retry Read/Reading-when-ready vocabulary plus a
// separate acquire-only Download action, the name-caption rule
// (a redundant "Volume N" name collapses to nothing), long-series keyboard step mapping
// (PageUp/PageDown/Home/End), and that a zero-volume series reserves no action bar.
//
// Ported from the Preflight arc-08 package's own harness of the same name, adapted to the
// LIVE component surface: the `chapters` construct property does not exist on the adopted
// MangaTankobanLibrary.qml (catalogue-independence Slice 3 already deleted the WC
// thumb-scrape ladder that property used to feed), so it is not passed here.
import QtQuick
import QtQuick.Window

Window {
    id: harness
    width: 1706
    height: 620
    visible: true

    component FakeService: QtObject {
        property var rows: []
        signal volumesChanged(string seriesId)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(sid) { return rows }
        function statusOf(vid) { return { "state": "none" } }
        function localPages(vid) { return [] }
        function cancel(vid) {}
    }

    component FakeProgress: QtObject {
        property var record: ({ "chapterId": "v7", "page": 22, "max": 180 })
        function get(kind, sid) { return kind === "tankoban" ? record : null }
    }

    component FakeDownloads: QtObject {
        signal thumbReady(string chapterId, string url)
        signal progress(string chapterId, real done, real total)
        signal finished(string chapterId)
        signal failed(string chapterId, string reason)
        signal removed(string chapterId)
        function fetchThumb(seriesId, chapterId) {}
        function statusOf(cid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(cid) { return [] }
    }

    FakeService { id: service }
    FakeService { id: emptyService; rows: [] }
    FakeProgress { id: progress }
    FakeDownloads { id: downloads }

    property var library: null
    property int sourceSignals: 0
    property int readSignals: 0
    property var lastSourceContext: null

    function ck(value, message) {
        if (!value) throw new Error(message)
    }

    // Volume 4 carries a genuine BookWalker-style display name; volume 9 carries a redundant
    // "Volume 9" name that the caption rule must collapse to nothing (POLISH-DELTA ruling #1).
    // The row carries a baked `cover` field (possibly empty) — the shape TankobanCatalog
    // seeding actually emits on master.
    function makeRows(count) {
        var out = []
        for (var i = 1; i <= count; ++i) {
            var state = i === 3 ? "ready" : (i === 5 ? "downloading" : (i === 11 ? "failed" : "none"))
            var name = i === 4 ? "Herr Doctor Tenma" : (i === 9 ? "Volume 9" : "")
            out.push({
                "id": "v" + i, "seriesId": "S", "number": String(i),
                "title": name, "cover": "", "state": state
            })
        }
        return out
    }

    function run() {
        try {
            service.rows = makeRows(115)
            var comp = Qt.createComponent("../qml/MangaTankobanLibrary.qml")
            if (comp.status === Component.Error) throw new Error(comp.errorString())
            library = comp.createObject(harness, {
                "width": harness.width, "height": harness.height,
                "seriesId": "S", "seriesTitle": "Oyasumi Punpun",
                "service": service, "progress": progress, "downloader": downloads
            })
            if (!library) throw new Error("candidate library did not instantiate")
            library.sourcesRequested.connect(function(ctx) {
                harness.sourceSignals += 1
                harness.lastSourceContext = ctx
            })
            library.readVolumeRequested.connect(function(volumeId) { harness.readSignals += 1 })

            Qt.callLater(function() {
                try {
                    ck(library.showVolumes, "qualified series must expose the volume flow")
                    ck(library.focusIndex === 6, "resume must land on volume 7")
                    ck(library.flowCurrentIndex === library.focusIndex, "ListView current index must follow focus")
                    ck(library.bookHeight <= 276 && library.bookHeight >= 190,
                       "book height must stay in the normal Tankoban range")
                    ck(library.maxScaledVolumeHeight <= library.flowViewportHeight,
                       "the selected volume plus caption must fit the flow viewport at its drawn (scaled) size — never cropped")
                    ck(library.liveVolumeTiles > 0 && library.liveVolumeTiles < library.volumeRows.length,
                       "long series must stay virtualized")
                    ck(library.currentActionLabel === "Read Vol. 7",
                       "an unowned selected volume must keep the stable Read verb")

                    // caption vocabulary (POLISH-DELTA ruling #1): the real name reaches the
                    // caption, a redundant "Volume N" name collapses to nothing, and no range
                    // vocabulary exists anywhere in the candidate any more.
                    ck(library.volumeNameFor(library.volumeRows[3]) === "Herr Doctor Tenma",
                       "a genuine volume name must reach the caption")
                    ck(library.volumeNameFor(library.volumeRows[8]) === "",
                       "a redundant Volume N name must collapse to nothing")
                    ck(typeof library.shelfRangeFor === "undefined" && typeof library.chapterSpanFor === "undefined"
                       && typeof library.chipTextFor === "undefined" && typeof library.liveCaptionFor === "undefined",
                       "range/chip caption vocabulary must not exist in the v2.3 candidate")
                    ck(typeof library.chapters === "undefined" && typeof library.requestCovers === "undefined"
                       && typeof library.curatedCovers === "undefined",
                       "the chapters/WC-thumb-prefetch surface must not exist — covers are catalogue-baked/local-only")

                    // state vocabulary: Get / Read / Retry / percent, and the caption's own state
                    // line for a failed volume.
                    library.focusAtIndex(10)   // volume 11, seeded "failed"
                    ck(library.currentActionLabel === "Retry Read",
                       "a failed selected volume must preserve the Read intent in its retry label")
                    ck(library.stateLineFor(library.volumeRows[10]) === "failed",
                       "a failed volume's caption state line must read exactly 'failed'")
                    library.focusAtIndex(4)    // volume 5, seeded "downloading"
                    ck(library.currentActionLabel === "Read Vol. 5",
                       "a background in-flight volume still means Read until the user adopts it")
                    library.pendingReadVolumeId = "v5"
                    service.progress("v5", 62, 100)
                    ck(library.currentActionLabel === "Reading when ready",
                       "a Read-adopted in-flight volume must expose the waiting intent")
                    ck(library.currentIntentProgressLabel === "62%",
                       "a Read-adopted in-flight volume must keep its live progress beside the verb")
                    library.pendingReadVolumeId = ""

                    // long-series keyboard/wheel-equivalent step mapping (POLISH-DELTA ruling #7):
                    // PageUp/PageDown and Shift+wheel both route through jumpBy(10); Home/End
                    // reach the rails.
                    library.focusAtIndex(20)
                    var before = library.focusIndex
                    library.jumpBy(10)
                    ck(library.focusIndex === before + 10, "a 10-volume jump must move exactly 10 volumes")
                    library.jumpBy(-10)
                    ck(library.focusIndex === before, "a reverse 10-volume jump must return to the start index")
                    library.focusAtIndex(0)
                    ck(library.focusIndex === 0, "the Home rail must reach the first volume")
                    library.focusAtIndex(library.volumeRows.length - 1)
                    ck(library.focusIndex === library.volumeRows.length - 1,
                       "the End rail must reach the last volume")

                    library.focusAtIndex(6)
                    var beforeSrc = sourceSignals
                    var beforeRead = readSignals
                    library.pressVolume(7)
                    ck(library.focusIndex === 7 && sourceSignals === beforeSrc && readSignals === beforeRead,
                       "first click on a neighbour must only focus it")
                    library.pressVolume(7)
                    ck(readSignals === beforeRead + 1 && sourceSignals === beforeSrc,
                       "second click on the focused unowned volume must express Read, not Download")
                    library.downloadAction(library.currentRow)
                    ck(sourceSignals === beforeSrc + 1,
                       "the separate Download action must be the acquisition-only source route")
                    ck(lastSourceContext && lastSourceContext.intent === "acquire",
                       "Download must carry explicit acquire intent into the source picker")

                    library.focusAtIndex(2)
                    Qt.callLater(function() {
                        try {
                            ck(library.currentActionLabel === "Read Vol. 3",
                               "ready volume must keep the same stable Read verb")
                            var readyBefore = readSignals
                            library.activateCurrent()
                            ck(readSignals === readyBefore + 1,
                               "Read must emit exactly one semantic read-volume request")

                            // A series with no catalogued volumes never shows "VOLUMES 0" and
                            // never reserves the action bar — the honest shelf-less page lives one
                            // level up (MangaSeries.qml), but this component must still degrade
                            // cleanly if it is ever handed zero rows.
                            var noVolumes = comp.createObject(harness, {
                                "width": 1200, "height": 560,
                                "seriesId": "Z", "seriesTitle": "Unqualified Series",
                                "service": emptyService, "progress": progress, "downloader": downloads
                            })
                            ck(noVolumes !== null, "zero-volume candidate must instantiate")
                            Qt.callLater(function() {
                                try {
                                    ck(!noVolumes.showVolumes, "a series with no catalogued volumes must not expose the flow")
                                    ck(noVolumes.actionBarHeight === 0, "a series with no volumes must not reserve the action bar")
                                    console.log("MANGA_VOLUME_FLOW_OK")
                                    Qt.exit(0)
                                } catch (zeroErr) {
                                    console.log("MANGA_VOLUME_FLOW_FAIL: " + zeroErr.message); Qt.exit(1)
                                }
                            })
                        } catch (inner) {
                            console.log("MANGA_VOLUME_FLOW_FAIL: " + inner.message); Qt.exit(1)
                        }
                    })
                } catch (e) {
                    console.log("MANGA_VOLUME_FLOW_FAIL: " + e.message); Qt.exit(1)
                }
            })
        } catch (e) {
            console.log("MANGA_VOLUME_FLOW_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Timer {
        interval: 120
        running: true
        repeat: false
        onTriggered: harness.run()
    }
    Timer {
        interval: 8000
        running: true
        repeat: false
        onTriggered: {
            console.log("MANGA_VOLUME_FLOW_FAIL: timeout")
            Qt.exit(1)
        }
    }
}
