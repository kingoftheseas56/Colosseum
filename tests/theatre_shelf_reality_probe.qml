// Prints every INDEX shelf's real top-20 titles through the real engine. Run via
// colosseum.exe (context properties registered), offscreen. Output lines:
//   SHELF <pageKey>/<key> (<n>): Title (year) | Title (year) | ...
import QtQuick
import "../qml/TheatreCatalogRules.js" as Rules

Item {
    Timer {
        interval: 50; running: true; repeat: false
        onTriggered: {
            var imdb = (typeof ImdbCatalog !== "undefined") ? ImdbCatalog : null;
            if (!imdb || !imdb.ready()) { console.log("REALITY: NO DB"); Qt.exit(2); }
            ["movies", "shows"].forEach(function(pageKey) {
                var defs = Rules.defaultRows(pageKey);
                if (pageKey === "movies")
                    defs = defs.concat(Rules.dailyRows(Date.UTC(2026, 7, 2), 6));
                defs.forEach(function(def) {
                    var queries = Rules.indexQueriesFor(def.recipe);
                    if (!queries.length) return;                       // live shelf
                    var rows = [];
                    queries.forEach(function(q) {
                        var part = imdb.titleCatalog(q, 0, 20) || [];
                        for (var i = 0; i < part.length; i++) rows.push(part[i]);
                    });
                    var line = rows.slice(0, 20).map(function(r) {
                        return r.title + " (" + r.year + (r.isAnime ? " ANIME" : "") + ")";
                    }).join(" | ");
                    console.log("SHELF " + pageKey + "/" + def.key + " (" + rows.length + "): " + line);
                });
            });
            console.log("REALITY_PROBE_DONE");
            Qt.exit(0);
        }
    }
}
