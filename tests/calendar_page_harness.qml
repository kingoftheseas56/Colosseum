// Construct-proof for CalendarPage.qml — it must build offscreen with NO context
// properties present (Collection/Progress absent → typeof guards → empty shelf),
// imports/Theme/HouseScrollBar/ScrollGlide all resolve. NEVER throw; Qt.exit(fails).
import QtQuick
import "../qml" as UI

Item {
    UI.CalendarPage { id: page; width: 1200; height: 800 }
    Timer {
        interval: 400; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(c, label) { if (!c) fails.push(label); }
            ok(page !== null, "page constructed");
            ok(Array.isArray(page.entries) && page.entries.length === 0, "empty entries with no Collection");
            ok(page.shelfEmpty === true, "shelfEmpty true with no data");
            ok(page.viewMonth >= 1 && page.viewMonth <= 12, "viewMonth valid: " + page.viewMonth);
            // month nav mutates state without error
            var m0 = page.viewMonth;
            page.nextMonth();
            ok(page.viewMonth !== m0 || page.viewYear !== 2026, "nextMonth advanced");
            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("calendar_page_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
