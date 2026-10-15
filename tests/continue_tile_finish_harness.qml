// Finish-harmonization contract for the ContinueTile "world" variant (follow-up to the Catalogue
// Poster & Shelf Polish arc, 2026-08-03). Continue Watching / Next Up tiles adopt the catalogue
// posters' finish — 148x222 geometry, genuine rounded crop, cheap depth — WITHOUT losing what a
// Continue tile is for: the progress value, the at-rest title, and the watched state must survive.
// The genuine mask + depth are visual (eyes-on); here we lock geometry + the functional contract.
// NEVER throw offscreen: collect fails, print CONTINUE_TILE_FINISH_OK when clean, single Qt.exit.
import QtQuick
import "../qml" as UI

Item {
    id: h
    width: 400; height: 400

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    property var videoEntry: ({ id: "tt1", kind: "video", title: "Attack on Titan", sub: "S4 E12",
                                cover: "", c1: "#334", c2: "#112", progress: 0.63, watched: false })

    UI.ContinueTile { id: tile; variant: "world"; entry: h.videoEntry }

    Timer {
        interval: 80; running: true; repeat: false
        onTriggered: {
            // ── geometry harmonized to the gallery poster (148x222) ──
            ok(tile.width === 148, "world tile width harmonized to 148, got " + tile.width);
            ok(tile.height === 222, "world tile height harmonized to 222, got " + tile.height);

            // ── function preserved: progress, at-rest title, watched state ──
            ok(Math.abs(tile.prog - 0.63) < 0.001, "progress value preserved, got " + tile.prog);
            ok(tile.label === "Attack on Titan", "title shown at rest, got " + tile.label);
            ok(tile.watched === false, "watched state preserved (false)");

            // ── a watched entry still clamps progress and reads watched ──
            tile.entry = ({ id: "tt2", kind: "manga", title: "Berserk", sub: "Ch 364",
                            cover: "", progress: 1.2, watched: true });
            ok(tile.prog === 1, "progress clamps to 1, got " + tile.prog);
            ok(tile.watched === true, "watched entry reads watched");
            ok(tile.label === "Berserk", "manga title preserved at rest");

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("CONTINUE_TILE_FINISH_OK");
            Qt.exit(h.fails.length);
        }
    }
}
