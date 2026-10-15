// comic_series_notavailable_honest_harness.qml — the OTHER side of the race fix: a series that
// is genuinely NOT in the DB and whose live resolve finds no source MUST still show the honest
// "Not available from sources yet" empty state. Guards against the fix over-correcting and
// suppressing the legitimate empty state. Verdict rides the exit code (Qt.exit(0) pass).
import QtQuick
import QtQuick.Controls
import "../qml/ComicsDb.js" as ComicsDb
import "../qml/ComicResolve.js" as Resolve

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: false

    property var capturedCb: null

    Component.onCompleted: {
        ComicsDb.setData({ series: [] })    // DB carries nothing → the series must go live
        Resolve.store = { get: function() { return null; }, set: function() {} };
        Resolve.slugFn = null;
        Resolve.searchFn = function(q, cb) { window.capturedCb = cb; };
        ld.active = true
    }

    Loader {
        id: ld
        anchors.fill: parent
        active: false
        source: "../qml/ComicSeriesPage.qml"
        onLoaded: {
            item.seriesTitle = "Some Unlisted Series"
            item.locgId = "locg:notindb"
            Qt.callLater(window.step2)
        }
    }

    function step2() {
        var item = ld.item
        if (window.capturedCb === null) { Qt.exit(4); return }   // live path not taken
        if (item.dbSeries) { Qt.exit(5); return }                // should NOT be a DB series
        // resolve returns no match, SAME generation (no navigation) → honest empty state must show.
        window.capturedCb([])
        Qt.callLater(window.step3)
    }

    function step3() {
        var item = ld.item
        if (!item.notAvailable) {
            console.log("HONEST_EMPTY_FAIL: no-source series did NOT show notAvailable")
            Qt.exit(2); return
        }
        console.log("HONEST_EMPTY_OK")
        Qt.exit(0)
    }

    Timer { interval: 6000; running: true; onTriggered: { console.log("HONEST_EMPTY_FAIL: timeout"); Qt.exit(3) } }
}
