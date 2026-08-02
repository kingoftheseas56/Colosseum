// Composition + lazy-residency contract for TheatreCatalogPage (Catalogue Poster & Shelf Polish,
// Task 6). A fake catalog loader, a fake content preference, and a real (temp-INI) row-preference
// store are injected so the page's composition — Top 10 first, extension split, customization,
// stale-generation fence, progressive merge — AND the new lazy-shelf residency are observable
// without the live API. The page now mounts each row through a LazyPosterShelf host bounded to a
// driven viewport; live shelves must plateau below the total row count while implicitHeight (the
// full reserved height) stays constant. Phase 1 checks composition/customization on a stable layout;
// phase 2 checks lazy residency after resetting to defaults. NEVER throw offscreen: collect fails,
// one Qt.exit(fails.length).
import QtQuick
import QtCore
import "../qml" as UI

Item {
    id: h
    width: 1200; height: 900

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
        // 13 main rows (3 named house + 1 recognized service + 9 more house) and 2 extension rows,
        // so a 600px viewport plainly loads only a subset — enough to prove residency plateaus.
        var rows = [ houseRow("top-10", "Top 10", 0, true),
                     houseRow("top-rated", "Top Rated", 20, false),
                     houseRow("hidden-gems", "Hidden Gems", 30, false),
                     extRow("ext:netflix", "Netflix", 15, "service-extension") ];
        for (var i = 0; i < 9; i++)
            rows.push(houseRow("house-" + i, "House " + i, 40 + i, false));
        rows.push(extRow("ext:docu", "Docu World", 2000, "extension"));
        rows.push(extRow("ext:indie", "Indie Reel", 2001, "extension"));
        return rows;
    }

    function fakeLoader(pageKey, options, push) {
        h.loaderCalls += 1;
        h.loaderOptions = options;
        h.lastPush = push;
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
        visualProfile: "gallery"
        viewportHeight: 600
        viewportTop: 0
        catalogLoader: h.fakeLoader
        contentPreferences: fakePrefs
        rowPreferences: rowPrefs
    }

    function keyIdx(rows, key) { for (var i = 0; i < rows.length; i++) if (rows[i].key === key) return i; return -1; }

    // ── phase 1: composition + customization on the fully-loaded page ──
    Timer {
        interval: 150; running: true; repeat: false
        onTriggered: {
            rowPrefs.reset("movies"); rowPrefs.reset("shows"); rowPrefs.reset("anime");
            page.load();

            ok(h.loaderOptions !== null, "catalog loader was called");
            ok(typeof h.loaderOptions.generation === "number", "generation passed to loader");
            ok(h.loaderOptions.showExplicit === false, "showExplicit mirrors the content preference");
            ok(typeof h.loaderOptions.explicitFilter === "function", "an explicit filter is passed to the API");

            var main = page.mainRows;
            ok(main.length > 0 && main[0].key === "top-10", "Top 10 renders first by default");
            ok(h.keyIdx(main, "ext:netflix") !== -1, "recognized service row sits in the main list");
            ok(h.keyIdx(main, "ext:docu") === -1, "unknown extension is NOT in the main list");
            ok(page.extensionRows.length === 2, "both unknown extensions land under From Your Extensions");
            ok(page.extensionRows[0].key === "ext:docu", "extension order preserved");
            ok(page.hasExtensionSection === true, "From Your Extensions section shows when non-empty");
            var noBlurb = true;
            for (var i = 0; i < main.length; i++) if (main[i].sub !== undefined || main[i].blurb !== undefined) noBlurb = false;
            ok(noBlurb, "no main row carries sub/blurb copy");

            ok(h.keyIdx(main, "top-10") !== -1 && h.keyIdx(main, "hidden-gems") !== -1,
               "both the early and later rows are present after progressive push");

            rowPrefs.toggleHidden("movies", "top-rated");
            ok(h.keyIdx(page.mainRows, "top-rated") === -1, "hidden row disappears outside edit mode");
            page.editMode = true;
            var em = page.mainRows;
            var tr = null; for (var e = 0; e < em.length; e++) if (em[e].key === "top-rated") tr = em[e];
            ok(tr !== null && tr.hidden === true, "edit mode shows the hidden row, flagged hidden");
            page.editMode = false;
            rowPrefs.toggleHidden("movies", "top-rated");

            rowPrefs.rename("movies", "top-10", "Chart Toppers");
            var renamed = null; var mm = page.mainRows;
            for (var r = 0; r < mm.length; r++) if (mm[r].key === "top-10") renamed = mm[r];
            ok(renamed && renamed.title === "Chart Toppers", "rename updates the rendered row title");

            rowPrefs.move("movies", ["top-10", "top-rated", "hidden-gems"], "top-10", 1);
            ok(page.mainRows[0].key !== "top-10", "move reorders the rendered model");

            rowPrefs.reset("movies");
            ok(page.mainRows[0].key === "top-10" && page.mainRows[0].title === "Top 10",
               "reset restores default order and names");

            var gBefore = page.generation;
            var callsBefore = h.loaderCalls;
            fakePrefs.changed();
            ok(page.generation > gBefore, "explicit-setting change increments generation");
            ok(h.loaderCalls > callsBefore, "a preference change reloads the surface");

            var staleGen = gBefore;
            var mainKeysNow = page.mainRows.map(function(x){ return x.key; }).join(",");
            h.lastPush({ pageKey: "movies", generation: staleGen, rows: [ h.houseRow("bogus", "Bogus", 0, false) ], loading: false, error: "" });
            ok(page.mainRows.map(function(x){ return x.key; }).join(",") === mainKeysNow,
               "a stale-generation push does not alter the rows");

            // hand off to the lazy-residency phase on a clean, default layout
            rowPrefs.reset("movies"); page.editMode = false; page.viewportTop = 0;
            phase2.start();
        }
    }

    // ── phase 2: lazy shelf residency on the stable, default layout ──
    Timer {
        id: phase2
        interval: 150; running: false; repeat: false
        onTriggered: {
            var total = page.mainRows.length + page.extensionRows.length;
            ok(total >= 14, "at least 14 shelves total, got " + total);

            // both main and extension delegates are LazyPosterShelf hosts
            var m0 = page.mainShelfAt(0);
            var e0 = page.extShelfAt(0);
            ok(m0 !== null && m0.reservedHeight !== undefined && m0.railLoaded !== undefined,
               "main delegate is a LazyPosterShelf host");
            ok(e0 !== null && e0.reservedHeight !== undefined && e0.railLoaded !== undefined,
               "extension delegate is a LazyPosterShelf host");

            // top of page: some shelves live, but far fewer than the total
            page.viewportTop = 0;
            var liveTop = page.liveShelfCount;
            ok(liveTop > 0, "some shelves are live at the top, got " + liveTop);
            ok(liveTop < total, "live shelves are bounded below total at the top (" + liveTop + " < " + total + ")");

            var hBefore = page.implicitHeight;

            // move to the bottom: the live SET changes without ballooning to every row
            page.viewportTop = page.implicitHeight;   // scroll far down
            var liveBottom = page.liveShelfCount;
            ok(liveBottom > 0, "some shelves are live at the bottom, got " + liveBottom);
            ok(liveBottom < total, "live shelves stay bounded below total at the bottom (" + liveBottom + " < " + total + ")");

            // reserved geometry is stable — vertical layout never changed with residency
            ok(Math.abs(page.implicitHeight - hBefore) < 1,
               "implicitHeight unchanged by moving the viewport (" + hBefore + " -> " + page.implicitHeight + ")");

            // Customize mode must NOT eagerly instantiate every rail
            page.viewportTop = 0;
            page.editMode = true;
            ok(page.liveShelfCount < total, "Customize mode does not make every rail live, got " + page.liveShelfCount);
            page.editMode = false;

            // structure order: extension heading before extension shelves, genre mosaic last
            ok(page.hasExtensionSection === true, "extension heading present");
            ok(page.genreMosaicY > page.extensionHeadingY, "genre mosaic sits after the extension heading");
            var lastMainShelf = page.mainShelfAt(page.mainRows.length - 1);
            ok(lastMainShelf !== null && page.extensionHeadingY > lastMainShelf.mapToItem(page, 0, 0).y,
               "extension heading sits after the last main shelf");

            if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
            else console.log("THEATRE_PAGE_OK");
            Qt.exit(h.fails.length);
        }
    }
}
