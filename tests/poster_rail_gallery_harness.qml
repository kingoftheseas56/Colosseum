// Gallery-rail contract for PosterRail (Catalogue Poster & Shelf Polish, Task 4). Proves the
// gallery geometry (148px poster, 20px gap, reserved two-line title, no subtitle band), the
// horizontal-position seam (restore an initial contentX, emit on change) that LazyPosterShelf
// relies on, and keyboard/remote traversal — all WITHOUT touching the horizontal scroll physics.
// Keyboard is driven through the same functions the Keys handler calls, so it's deterministic
// offscreen. NEVER throw offscreen: collect fails, print POSTER_RAIL_GALLERY_OK when clean.
import QtQuick
import "../qml" as UI

Item {
    id: h
    width: 900; height: 700

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    function mk(n) {
        var out = [];
        for (var i = 0; i < n; i++)
            out.push({ id: "tt" + i, title: "Title " + i, cover: "", imdbRating: "8." + i });
        return out;
    }

    property var sixItems: mk(6)
    property real capturedX: -1
    property var activatedItem: null

    UI.PosterRail {
        id: rail
        width: 700
        visualProfile: "gallery"
        items: h.sixItems
        initialContentX: 45
    }
    UI.PosterRail {
        id: rankedRail
        width: 700
        visualProfile: "gallery"
        ranked: true
        items: h.sixItems
    }

    Connections {
        target: rail
        function onHorizontalPositionChanged(x) { h.capturedX = x }
        function onItemRequested(item) { h.activatedItem = item }
    }

    Timer {
        interval: 100; running: true; repeat: false
        onTriggered: {
            // ── gallery geometry ──
            ok(rail.posterWidth === 148, "ordinary gallery poster width 148, got " + rail.posterWidth);
            ok(rail.cardGap === 20, "gallery card gap 20, got " + rail.cardGap);
            ok(rail.titleReserve === 45, "two-line title area reserved (10+35), got " + rail.titleReserve);
            // the reserved list height is exactly poster height + the two-line title block — no subtitle band.
            ok(rail.railListHeight === rail.posterHeight + rail.titleReserve,
               "list height reserves poster + two-line title only, got " + rail.railListHeight);

            // ── ranked rail keeps its numeral AND the same 148 poster ──
            ok(rankedRail.posterWidth === 148, "ranked poster still 148, got " + rankedRail.posterWidth);
            ok(rankedRail.cellWidth > rankedRail.posterWidth, "ranked cell grows for the numeral, got " + rankedRail.cellWidth);
            ok(rankedRail.ranked === true, "ranked flag preserved (numerals present)");

            // ── initial horizontal position restored (clamped into range) ──
            ok(Math.abs(rail.currentContentX - 45) <= 1,
               "initial contentX restored to 45, got " + rail.currentContentX);

            // ── changing contentX emits the new value ──
            h.capturedX = -1;
            rail.testSetContentX(90);
            ok(Math.abs(h.capturedX - 90) <= 1, "contentX change emits the new value, got " + h.capturedX);

            // ── Left/Right move the keyboard currentIndex ──
            rail.currentIndex = 0;
            rail.navigate(1);
            ok(rail.currentIndex === 1, "Right advances currentIndex, got " + rail.currentIndex);
            rail.navigate(1);
            ok(rail.currentIndex === 2, "Right advances again, got " + rail.currentIndex);
            rail.navigate(-1);
            ok(rail.currentIndex === 1, "Left retreats currentIndex, got " + rail.currentIndex);
            rail.navigate(-1); rail.navigate(-1);   // clamp at 0
            ok(rail.currentIndex === 0, "currentIndex clamps at 0");

            // ── Enter emits the ORIGINAL item at the current index ──
            rail.currentIndex = 3;
            h.activatedItem = null;
            rail.activateFocused();
            ok(h.activatedItem === h.sixItems[3], "Enter emits the original item at currentIndex");

            // ── signals preserved: itemRequested carries identity (checked above); seeAll still declared ──
            ok(typeof rail.seeAllRequested === "function", "seeAllRequested signal preserved");

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("POSTER_RAIL_GALLERY_OK");
            Qt.exit(h.fails.length);
        }
    }
}
