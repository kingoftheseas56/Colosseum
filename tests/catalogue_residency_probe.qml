// Deterministic long-page residency probe for the Catalogue Poster & Shelf Polish arc (Task 8).
// It drives the REAL composed TheatreCatalogPage with 25 fake gallery rows and a 900px viewport,
// steps the viewport top→middle→bottom→top, and prints machine-readable PROBE markers for the
// live shelf count (and a derived live-card ceiling) at each position plus a horizontal-restore
// check. capture_catalogue_perf.ps1 parses these and fails if counts grow with the row total, if
// the returned-to-top plateau balloons, or if the restored contentX drifts beyond 1px.
// Offscreen and deterministic — no rendering, no network. Residency is synchronous on viewport
// change, so after an initial layout settle the steps run in one JS turn.
import QtQuick
import "../qml" as UI

Item {
    id: h
    width: 1400; height: 1000

    property int cardsPerRow: 12
    function mkItems(key) {
        var out = [];
        for (var i = 0; i < h.cardsPerRow; i++)
            out.push({ id: key + "-" + i, title: "Item " + i, cover: "" });
        return out;
    }
    function rows25() {
        var out = [];
        for (var r = 0; r < 25; r++)
            out.push({ key: "row-" + r, title: "Shelf " + r, placement: r, ranked: r === 0,
                       sourceKind: "house", sourceLabel: "Colosseum",
                       items: mkItems("row-" + r),
                       seeAllPin: { pageKey: "movies", sourceKind: "house", rowKey: "row-" + r } });
        return out;
    }
    function fakeLoader(pageKey, options, push) {
        push({ pageKey: pageKey, generation: options.generation, rows: h.rows25(), loading: false, error: "" });
    }

    UI.TheatreCatalogPage {
        id: page
        width: 1300
        pageKey: "movies"
        visualProfile: "gallery"
        viewportHeight: 900
        viewportTop: 0
        catalogLoader: h.fakeLoader
    }

    property int total: 0
    property int liveTop: 0
    property int liveMiddle: 0
    property int liveBottom: 0
    property int liveReturnTop: 0
    property real restoreWant: 137
    property real restoreGot: -1

    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            h.total = page.mainRows.length + page.extensionRows.length;

            // ── TOP ──
            page.viewportTop = 0;
            h.liveTop = page.liveShelfCount;
            // record a scrolled position on a top shelf, to verify restore after a round trip
            var s0 = page.mainShelfAt(0);
            if (s0) s0.testSetRailContentX(h.restoreWant);

            // ── MIDDLE ──
            page.viewportTop = Math.max(0, page.implicitHeight / 2 - 450);
            h.liveMiddle = page.liveShelfCount;

            // ── BOTTOM ── (this unloads the top shelf, capturing its contentX)
            page.viewportTop = page.implicitHeight;
            h.liveBottom = page.liveShelfCount;

            // ── RETURN TO TOP ── (top shelf remounts and restores its horizontal position)
            page.viewportTop = 0;
            h.liveReturnTop = page.liveShelfCount;
            var s0b = page.mainShelfAt(0);
            h.restoreGot = s0b ? s0b.restoredContentX : -1;

            console.log("PROBE total=" + h.total);
            console.log("PROBE liveShelfCount top=" + h.liveTop + " middle=" + h.liveMiddle
                        + " bottom=" + h.liveBottom + " returnedTop=" + h.liveReturnTop);
            console.log("PROBE liveCardCeiling top=" + (h.liveTop * h.cardsPerRow)
                        + " middle=" + (h.liveMiddle * h.cardsPerRow)
                        + " bottom=" + (h.liveBottom * h.cardsPerRow)
                        + " returnedTop=" + (h.liveReturnTop * h.cardsPerRow));
            console.log("PROBE restoreContentX want=" + h.restoreWant + " got=" + h.restoreGot
                        + " err=" + Math.abs(h.restoreGot - h.restoreWant));
            console.log("CATALOGUE_RESIDENCY_PROBE_DONE");
            Qt.exit(0);
        }
    }
}
