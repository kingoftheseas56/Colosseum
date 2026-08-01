// Offscreen structure/edit-mode/generation contract for the composed deep TheatreCatalogPage
// (Task 9). A fake catalog loader, a fake content preference, and a real (temp-INI) row-
// preference store are injected so the page's composition — Top 10 first, extension split,
// customization, stale-generation fence, and progressive merge — is observable without the
// live API. NEVER throw offscreen: collect fails, one Qt.exit(fails.length).
import QtQuick
import QtCore
import "../qml" as UI

Item {
    id: h
    width: 1200; height: 800

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    property var loaderOptions: null
    property int loaderCalls: 0
    property var lastPush: null

    function houseRow(key, title, placement, ranked) {
        return { key: key, title: title, placement: placement, ranked: ranked === true,
                 sourceKind: "house", sourceLabel: "Colosseum",
                 items: [{ id: "i-" + key, title: "Item " + key, cover: "" }],
                 seeAllPin: { pageKey: "movies", sourceKind: "house", rowKey: key } };
    }
    function extRow(key, name, placement, kind) {
        return { key: key, title: name, placement: placement, ranked: false,
                 sourceKind: kind, sourceLabel: name,
                 items: [{ id: "i-" + key, title: "Item", cover: "" }],
                 seeAllPin: { pageKey: "movies", sourceKind: kind, rowKey: key, extName: name } };
    }
    function rowsPartial() { return [ houseRow("top-10", "Top 10", 0, true),
                                      houseRow("top-rated", "Top Rated", 20, false) ]; }
    function rowsFull() {
        return [ houseRow("top-10", "Top 10", 0, true),
                 houseRow("top-rated", "Top Rated", 20, false),
                 houseRow("hidden-gems", "Hidden Gems", 30, false),
                 extRow("ext:netflix", "Netflix", 15, "service-extension"),
                 extRow("ext:docu", "Docu World", 2000, "extension") ];
    }

    function fakeLoader(pageKey, options, push) {
        h.loaderCalls += 1;
        h.loaderOptions = options;
        h.lastPush = push;
        // progressive: a partial set (loading) then the full set (done)
        push({ pageKey: pageKey, generation: options.generation, rows: h.rowsPartial(), loading: true, error: "" });
        push({ pageKey: pageKey, generation: options.generation, rows: h.rowsFull(), loading: false, error: "" });
    }

    QtObject { id: fakePrefs; property bool showExplicit: false; signal changed() }

    UI.TheatreRowPreferences {
        id: rowPrefs
        settingsLocation: StandardPaths.writableLocation(StandardPaths.TempLocation)
                          + "/colosseum_theatre_page_test.ini"
    }

    UI.TheatreCatalogPage {
        id: page
        width: 1100
        pageKey: "movies"
        catalogLoader: h.fakeLoader
        contentPreferences: fakePrefs
        rowPreferences: rowPrefs
    }

    function keyIdx(rows, key) { for (var i = 0; i < rows.length; i++) if (rows[i].key === key) return i; return -1; }

    Timer {
        interval: 120; running: true; repeat: false
        onTriggered: {
            // clean slate for the row-preference store
            rowPrefs.reset("movies"); rowPrefs.reset("shows"); rowPrefs.reset("anime");
            page.load();   // reload after clearing prefs

            // ── options threaded to the loader ──
            ok(h.loaderOptions !== null, "catalog loader was called");
            ok(typeof h.loaderOptions.generation === "number", "generation passed to loader");
            ok(h.loaderOptions.showExplicit === false, "showExplicit mirrors the content preference");
            ok(typeof h.loaderOptions.explicitFilter === "function", "an explicit filter is passed to the API");

            // ── Top 10 first; house+service in main; extension split out; no blurb ──
            var main = page.mainRows;
            ok(main.length > 0 && main[0].key === "top-10", "Top 10 renders first by default");
            ok(h.keyIdx(main, "ext:netflix") !== -1, "recognized service row sits in the main list");
            ok(h.keyIdx(main, "ext:docu") === -1, "unknown extension is NOT in the main list");
            ok(page.extensionRows.length === 1 && page.extensionRows[0].key === "ext:docu",
               "unknown extension lands under From Your Extensions");
            ok(page.hasExtensionSection === true, "From Your Extensions section shows when non-empty");
            var noBlurb = true;
            for (var i = 0; i < main.length; i++) if (main[i].sub !== undefined || main[i].blurb !== undefined) noBlurb = false;
            ok(noBlurb, "no main row carries sub/blurb copy");

            // ── progressive load never cleared the earlier rows ──
            ok(h.keyIdx(main, "top-10") !== -1 && h.keyIdx(main, "hidden-gems") !== -1,
               "both the early and later rows are present after progressive push");

            // ── hide (out of edit mode the hidden row disappears) ──
            rowPrefs.toggleHidden("movies", "top-rated");
            ok(h.keyIdx(page.mainRows, "top-rated") === -1, "hidden row disappears outside edit mode");
            // ── edit mode reveals hidden rows (flagged hidden) ──
            page.editMode = true;
            var em = page.mainRows;
            var tr = null; for (var e = 0; e < em.length; e++) if (em[e].key === "top-rated") tr = em[e];
            ok(tr !== null && tr.hidden === true, "edit mode shows the hidden row, flagged hidden");
            page.editMode = false;
            rowPrefs.toggleHidden("movies", "top-rated");   // restore

            // ── rename updates the rendered title ──
            rowPrefs.rename("movies", "top-10", "Chart Toppers");
            var renamed = null; var mm = page.mainRows;
            for (var r = 0; r < mm.length; r++) if (mm[r].key === "top-10") renamed = mm[r];
            ok(renamed && renamed.title === "Chart Toppers", "rename updates the rendered row title");

            // ── move reorders the rendered model ──
            rowPrefs.move("movies", ["top-10", "top-rated", "hidden-gems"], "top-10", 1);
            ok(page.mainRows[0].key !== "top-10", "move reorders the rendered model");

            // ── reset restores defaults (Top 10 first again, default name) ──
            rowPrefs.reset("movies");
            ok(page.mainRows[0].key === "top-10" && page.mainRows[0].title === "Top 10",
               "reset restores default order and names");

            // ── generation increments on a content-preference change, loader re-runs ──
            var gBefore = page.generation;
            var callsBefore = h.loaderCalls;
            fakePrefs.changed();
            ok(page.generation > gBefore, "explicit-setting change increments generation");
            ok(h.loaderCalls > callsBefore, "a preference change reloads the surface");

            // ── stale-generation push is ignored ──
            var staleGen = gBefore;          // an old generation
            var mainKeysNow = page.mainRows.map(function(x){ return x.key; }).join(",");
            h.lastPush({ pageKey: "movies", generation: staleGen, rows: [ h.houseRow("bogus", "Bogus", 0, false) ], loading: false, error: "" });
            ok(page.mainRows.map(function(x){ return x.key; }).join(",") === mainKeysNow,
               "a stale-generation push does not alter the rows");

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("THEATRE_PAGE_OK");
            Qt.exit(h.fails.length);
        }
    }
}
