// Offscreen construct proof of DiscoverPage. Extensions context property is absent
// here — the typeof guards keep construction safe. NEVER throw (hangs offscreen).
import QtQuick
import "../qml" as UI

Item {
    UI.DiscoverPage { id: p; width: 1200; height: 700 }
    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            var fails = [];
            if (p.currentType !== "movie") fails.push("currentType default: " + p.currentType);
            if (p.keyboardMode !== false) fails.push("keyboardMode default: " + p.keyboardMode);
            // the catalogue model (the sidebar's source since 2026-09-29) is always an array
            if (!Array.isArray(p.catalogMenuModel)) fails.push("catalogMenuModel not array: " + p.catalogMenuModel);
            if (fails.length) console.log("FAILS: " + fails.join("; "));
            else console.log("discover_page_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
