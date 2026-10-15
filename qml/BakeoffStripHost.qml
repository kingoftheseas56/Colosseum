// BakeoffStripHost — long-strip reader bakeoff adapter (spec 2026-07-15, §10).
//
// A page-only surface hosting the PRODUCTION MangaReader over the canonical
// fixture pages injected by native/main.cpp (COLOSSEUM_BAKEOFF_STRIP). The
// store below only makes the same bytes consumable — it changes no easing,
// caching, sizing, or decode behavior. Never loaded outside the bakeoff env.
import QtQuick

Item {
    id: host
    anchors.fill: parent

    // MangaReader's store contract as a real QObject (its `Connections { target:
    // store }` needs one) — localPages/statusOf over the fixture, inert download
    // verbs (the fixture is always local), and the progress/finished/failed
    // signals so the wiring binds cleanly without ever firing.
    QtObject {
        id: bakeoffStore
        signal progress(string cid, int done, int total)
        signal finished(string cid)
        signal failed(string cid, string reason)
        readonly property bool running: false
        function localPages(id) { return DevBakeoffStripPages }
        function statusOf(id) { return "done" }
        function downloadChapter() {}
        function downloadIssue() {}
        function restart() {}
    }

    MangaReader {
        anchors.fill: parent
        pageStore: bakeoffStore
        entryKind: "bakeoff"          // Continue records namespace away from real ones
        seriesId: "bakeoff-fixture"
        seriesTitle: "Bakeoff"
        chapters: [{ id: "bakeoff", number: 1, name: "Fixture" }]
        Component.onCompleted: openChapterById("bakeoff", false)
    }
}
