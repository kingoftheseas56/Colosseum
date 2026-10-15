// Residency contract for LazyPosterShelf (Catalogue Poster & Shelf Polish, Task 5). Drives the
// shelf with EXPLICIT viewport geometry rather than a real wheel: the shelf reserves its full
// height always, mounts its PosterRail only inside a one-viewport activation band, retains it out
// to a two-viewport band (hysteresis), keeps exact height while unloaded, and preserves the
// horizontal position across unload/remount. Viewport positions are computed FROM the shelf's real
// reservedHeight so the assertions hold regardless of the profile's exact rail height.
// NEVER throw offscreen: collect fails, print LAZY_POSTER_SHELF_OK when clean, single Qt.exit.
import QtQuick
import "../qml" as UI

Item {
    id: h
    width: 800; height: 800

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    function mk(n) {
        var out = [];
        for (var i = 0; i < n; i++)
            out.push({ id: "tt" + i, title: "Title " + i, cover: "", imdbRating: "8." + i });
        return out;
    }
    property var row: ({ title: "Recently Released", ranked: false, items: mk(8),
                         sourceKind: "house", sourceLabel: "", seeAllPin: { rowKey: "rr" } })

    UI.LazyPosterShelf {
        id: shelf
        width: 500
        visualProfile: "gallery"
        row: h.row
        viewportHeight: 600
        y: 2000
    }

    Timer {
        interval: 100; running: true; repeat: false
        onTriggered: {
            var vh = 600;
            var H = shelf.reservedHeight;

            // ── the shelf reserves a full, exact height ──
            ok(H > 0, "reserved height computed, got " + H);
            ok(Math.abs(shelf.height - shelf.reservedHeight) < 1, "shelf height equals reserved height");

            // ── far below the viewport (>1 viewport) → unloaded ──
            shelf.viewportTop = 0;                       // viewport [0,600]; shelf at y=2000
            ok(!shelf.railLoaded, "far shelf starts unloaded");

            // ── viewport approaches within one viewport → loads ──
            shelf.viewportTop = shelf.y - 1.5 * vh;      // viewport bottom sits ~0.5vh above the shelf
            ok(shelf.railLoaded, "loads inside one-viewport activation margin");

            // ── record a scrolled horizontal position ──
            shelf.testSetRailContentX(173);

            // ── viewport pushed far past (shelf above the two-viewport retention band) → unloads ──
            shelf.viewportTop = shelf.y + H + 2 * vh + 200;
            ok(!shelf.railLoaded, "unloads outside two-viewport retention margin");
            ok(Math.abs(shelf.height - shelf.reservedHeight) < 1, "unloaded shelf keeps exact reserved height");

            // ── return: remounts and restores the horizontal position within 1px ──
            shelf.viewportTop = shelf.y - 0.5 * vh;
            ok(shelf.railLoaded, "remounts on return");
            ok(Math.abs(shelf.restoredContentX - 173) <= 1,
               "horizontal position restored to 173, got " + shelf.restoredContentX);

            // ── edit mode must NOT force a far shelf live ──
            shelf.viewportTop = 0;
            ok(!shelf.railLoaded, "far shelf unloaded before the edit-mode check");
            shelf.editMode = true;
            ok(!shelf.railLoaded, "edit mode does not force a far shelf live");
            shelf.editMode = false;

            // ── a missing/zero viewport keeps the rail mounted (offscreen-harness fallback) ──
            shelf.viewportHeight = 0;
            ok(shelf.railLoaded, "zero viewport height mounts the rail (offscreen fallback)");

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("LAZY_POSTER_SHELF_OK");
            Qt.exit(h.fails.length);
        }
    }
}
