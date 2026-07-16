// comic_series_notavailable_race_harness.qml — regression for the stale-async-callback race
// that painted "Not available from sources yet" OVER a DB-backed series (Avatar The Promise).
//
// The bug: ComicSeriesPage is REUSED across series opens (Main.openComicSeries reassigns
// item.locgId without unloading). A non-DB series takes the live attach() path, firing an
// async Resolve.resolve. If the user navigates to a DB-backed series before that callback
// returns, the DB path sets notAvailable=false and shows the ledger — but the STALE callback
// for the previous series later returns attached:false and clobbers notAvailable=true, so the
// honest-empty overlay paints over a fully-catalogued series.
//
// Faithful reproduction against the REAL ComicSeriesPage: seed ComicsDb with Avatar, defer the
// non-DB series' searchFn callback, navigate to Avatar, THEN fire the stale callback. With the
// generation guard the stale callback is ignored; without it, notAvailable flips true. Verdict
// rides the exit code (Qt.exit(0) pass / non-zero fail) — the house headless-QML pattern.
import QtQuick
import QtQuick.Controls
import "../qml/ComicsDb.js" as ComicsDb
import "../qml/ComicResolve.js" as Resolve

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: false

    property var capturedCb: null      // the deferred searchFn callback for the non-DB series
    property int searchCalls: 0        // how many times the live path called searchFn

    // Seed the .pragma-library singletons BEFORE the page loads. The Loader is gated inactive
    // until seeding is done: a child Loader's onLoaded otherwise fires before the parent's
    // Component.onCompleted, which would drive attach() against an unseeded DB/Resolve.
    Component.onCompleted: {
        // DB carries Avatar → its open must take the DB (ledger) path.
        ComicsDb.setData({ series: [ {
            locg_id: "avatar", title: "Avatar The Last Airbender The Promise",
            publisher: "Dark Horse Comics", rank: 46, cover: "",
            editions: [ { format: "Collected Edition",
                          title: "Avatar The Last Airbender The Promise",
                          available: false, slug: "avatar-promise" } ]
        } ] })
        // Live attach() wiring: no saved map, no slug lane, searchFn DEFERS (simulates a slow
        // network resolve we can fire on demand to recreate the race).
        Resolve.store = { get: function() { return null; }, set: function() {} };
        Resolve.slugFn = null;
        Resolve.searchFn = function(q, cb) { window.searchCalls += 1; window.capturedCb = cb; };
        ld.active = true
    }

    Loader {
        id: ld
        anchors.fill: parent
        active: false
        source: "../qml/ComicSeriesPage.qml"
        onLoaded: {
            // (1) open a NON-DB series → live path → searchFn captures the callback (deferred).
            item.seriesTitle = "Some Unlisted Series"
            item.locgId = "locg:notindb"
            Qt.callLater(window.step2)
        }
    }

    property var staleCb: null

    function step2() {
        var item = ld.item
        if (window.capturedCb === null) { console.log("RACE_FAIL: no deferred resolve callback captured"); Qt.exit(4); return }
        window.staleCb = window.capturedCb  // the notindb (stale) callback — fire it after we move on
        // (2) navigate to Avatar (in DB) → DB path → ledger shows, notAvailable=false.
        window.capturedCb = null            // probe: does Avatar re-enter the live path?
        item.seriesTitle = "Avatar The Last Airbender The Promise"
        item.locgId = "locg:avatar"
        if (window.capturedCb !== null) { Qt.exit(70 + window.searchCalls); return }  // Avatar went LIVE (unexpected)
        if (!item.dbSeries) { console.log("RACE_FAIL: Avatar not resolved from DB"); Qt.exit(5); return }
        if (item.notAvailable) { console.log("RACE_FAIL: notAvailable already true after DB nav"); Qt.exit(6); return }
        // diagnostic: after two attaches (notindb gen1 + avatar gen2), attachGen must be 2.
        if (item.attachGen !== 2) { Qt.exit(60 + item.attachGen); return }
        // (3) the STALE callback for the previous non-DB series returns "no source".
        window.staleCb([])   // no hits → resolve → done({attached:false})
        Qt.callLater(window.step3)
    }

    function step3() {
        var item = ld.item
        if (item.notAvailable) {
            console.log("COMIC_SERIES_RACE_FAIL: stale callback set notAvailable=true on a DB-backed series")
            Qt.exit(50 + item.attachGen); return    // 50+gen so the exit code reveals the guard state
        }
        console.log("COMIC_SERIES_RACE_OK")
        Qt.exit(0)
    }

    Timer {
        interval: 6000
        running: true
        onTriggered: { console.log("COMIC_SERIES_RACE_FAIL: timeout"); Qt.exit(3) }
    }
}
