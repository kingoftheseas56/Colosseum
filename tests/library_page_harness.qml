// Offscreen construct proof of LibraryPage. Collection/Progress/LocalDownloads context
// properties are absent here — the typeof guards keep construction safe (allRows → []).
// NEVER throw (hangs offscreen); collect fails, Qt.exit(fails.length).
import QtQuick
import "../qml" as UI

Item {
    UI.LibraryPage { id: p; width: 1280; height: 760 }
    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }
            ok(p.sortMode === "lastWatched", "sortMode default: " + p.sortMode);
            ok(p.typeFilter === "", "typeFilter default: " + p.typeFilter);
            ok(p.airingFilter === "", "airingFilter default: " + p.airingFilter);
            ok(p.stateFilter === "", "stateFilter default: " + p.stateFilter);
            ok(p.query === "", "query default: " + p.query);
            ok(p.allRows.length === 0, "allRows empty offscreen: " + p.allRows.length);
            ok(p.visibleRows.length === 0, "visibleRows empty offscreen");
            ok(p.counts && p.counts.saved === 0, "counts zeroed: " + (p.counts ? p.counts.saved : "nil"));
            ok(p.menuRow === null, "menu closed by default");
            // toggleStateFilter is the ledger's contract
            p.toggleStateFilter("watched");
            ok(p.stateFilter === "watched", "toggle sets stateFilter");
            p.toggleStateFilter("watched");
            ok(p.stateFilter === "", "toggle again clears stateFilter");
            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("library_page_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
