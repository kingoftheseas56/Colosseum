// Non-vacuous rules + persistence proof for BiblioExploreRules.js and
// BiblioExplorePreferences.qml (Biblio Discover/Explore plan, Task 6). Mirrors the Theatre row
// preferences harness pattern: a temp INI file under StandardPaths.TempLocation, sequential
// Timer-staged phases (construct -> write baseline -> destroy -> reconstruct -> verify
// persistence survived), then in-memory pure-rule assertions run synchronously against
// BiblioExploreRules.js. NEVER throw offscreen: collect fails, one Qt.exit at the end.
import QtQuick
import QtCore
import "../qml" as UI
import "../qml/BiblioExploreRules.js" as Rules
import "../qml/BiblioDiscoverApi.js" as BiblioDiscoverApi

Item {
    id: harness
    property string iniUrl: StandardPaths.writableLocation(StandardPaths.TempLocation)
                            + "/colosseum_biblio_explore_test.ini"
    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    // ---------------------------------------------------------------------
    // Task 7 — BiblioBookRail + BiblioExplorePage. Injected fakes stand in for the native
    // BiblioCatalog/Extensions context properties and DiscoverApi's network-backed loadPage —
    // this harness never touches a real native object or the network. `tempIni(name)` mirrors
    // the file's own iniUrl convention, one isolated temp INI per page instance so parallel
    // BiblioExplorePreferences instances never collide.
    // ---------------------------------------------------------------------
    function tempIni(name) {
        return StandardPaths.writableLocation(StandardPaths.TempLocation)
             + "/colosseum_biblio_explore_" + name + "_test.ini";
    }

    function bookRow(id, title, author, cover, ratingAvg) {
        return { canonicalId: id, title: title, author: author, originalLanguage: "english",
                 canonicalFirstPublished: "2020-01-01", publisher: "Test House", coverUrl: cover,
                 rating: { average: ratingAvg, count: 42 }, score: 1, rank: 1 };
    }

    function makeFakeCatalog(opts) {
        opts = opts || {};
        // Slice 5 (spec 2026-08-15): requestEnrichment recording stub — the page's See-All
        // lazy-enrich trigger fires it for the two Open Library rails; the log lets the
        // harness assert the exact key and call count. Dropping the property from a returned
        // fake simulates a pre-update/older catalogSource for the no-method guard proof.
        var enrichmentLog = [];
        return {
            ready: opts.ready !== undefined ? opts.ready : true,
            revision: opts.revision !== undefined ? opts.revision : 1,
            exploreRows: function(limit, includeExplicit) {
                return [
                    { catalogId: "popular", items: opts.popularItems || [] },
                    { catalogId: "top-rated", items: opts.topRatedItems || [] },
                    { catalogId: "new-releases", items: opts.newReleasesItems || [] },
                    { catalogId: "trending", items: opts.trendingItems || [] },
                    { catalogId: "most-read", items: opts.mostReadItems || [] },
                    { catalogId: "classics", items: opts.classicsItems || [] }
                ];
            },
            discoverPage: function(catalogId, axis, key, includeExplicit, offset, limit) {
                return { items: opts.top10Items || [] };
            },
            mosaic: function(facetKey, limit, includeExplicit) {
                var map = opts.mosaicByFacet || {};
                return map[facetKey] || [];
            },
            requestEnrichment: function(id) { enrichmentLog.push(id); },
            enrichmentLog: enrichmentLog
        };
    }

    function makeFakeExtensionsSource(manifestList) {
        return { installed: function() { return manifestList || []; } };
    }

    function oneBookExtension(id, title) {
        return [{
            id: id, enabled: true, core: false,
            transportUrl: "https://" + id + ".example/manifest.json",
            manifest: { name: title, catalogs: [ { id: "popular", type: "book", name: title } ] }
        }];
    }

    function syncFetcher(metas) {
        return function(catalog, selections, skip, done) { done(metas); };
    }

    // a fetcher that counts real invocations — Part A proves a preference flip triggers a
    // GENUINE re-fetch, not just an internal bookkeeping bump.
    function countingFetcher(metas, counterHolder) {
        return function(catalog, selections, skip, done) {
            counterHolder.calls = counterHolder.calls + 1;
            done(metas);
        };
    }

    // a delayed fetch (dynamic Timer) — proves a slow extension row resolves on its OWN
    // schedule without blocking (or being blocked by) the synchronous house/top-10 data.
    function delayedFetcher(metas, delayMs) {
        return function(catalog, selections, skip, done) {
            var t = Qt.createQmlObject(
                'import QtQuick; Timer { interval: ' + delayMs + '; running: true; repeat: false }',
                harness, "biblioExploreDelayTimer");
            t.triggered.connect(function() { done(metas); t.destroy(); });
        };
    }

    // collects the page's real BiblioBookRail instances by walking the visual tree — each
    // rendered displayRow owns exactly one, so emitting seeAllActivated() on a found rail
    // drives the PAGE's own onSeeAllActivated handler (no synthetic re-implementation).
    function collectRails(item, out) {
        if (!item) return out;
        var kids = item.children || [];
        for (var i = 0; i < kids.length; i++) {
            if (String(kids[i]).indexOf("BiblioBookRail") !== -1) out.push(kids[i]);
            harness.collectRails(kids[i], out);
        }
        return out;
    }
    function railByTitle(rails, title) {
        for (var i = 0; i < rails.length; i++) if (rails[i].title === title) return rails[i];
        return null;
    }

    readonly property var mosaicFixture: ({
        "literary-fiction": [{ title: "Fic A", author: "Author F", coverUrl: "fic-a.png", rating: { average: 4, count: 1 } }],
        "nonfiction": [{ title: "Non A", author: "Author N", coverUrl: "non-a.png", rating: { average: 4, count: 1 } }],
        "young-adult": [{ title: "YA A", author: "Author Y", coverUrl: "ya-a.png", rating: { average: 4, count: 1 } }]
    })

    Component { id: railComp; UI.BiblioBookRail {} }
    Component { id: pageComp; UI.BiblioExplorePage {} }
    property var pendingSlowPage: null

    function runBookRailChecks() {
        var items = [{ id: "r1", title: "Rail Book", author: "Rail Author", cover: "r1.png",
                        rating: "4.3", source: "Apple Books · Open Library" }];
        var rail = railComp.createObject(harness, { items: items, title: "Test Shelf" });
        harness.ok(rail !== null, "BiblioBookRail constructs");
        if (!rail) return;

        harness.ok(rail.items[0].author === "Rail Author", "author is carried on the rail's item data");
        harness.ok(rail.ratingVisibleAt(0) === false, "rest: rating hidden with no hover and no keyboard focus");
        harness.ok(rail.sourceVisibleAt(0) === false, "rest: source hidden with no hover and no keyboard focus");

        rail.testHoveredIndex = 0;
        harness.ok(rail.ratingVisibleAt(0) === true, "pointer hover reveals rating");
        harness.ok(rail.sourceVisibleAt(0) === true, "pointer hover reveals source");
        rail.testHoveredIndex = -1;
        harness.ok(rail.ratingVisibleAt(0) === false, "rating hides again once hover ends");

        rail.keyboardMode = true; rail.currentIndex = 0;
        harness.ok(rail.ratingVisibleAt(0) === true, "keyboard focus ALSO reveals rating (Biblio: hover OR focus)");
        harness.ok(rail.sourceVisibleAt(0) === true, "keyboard focus ALSO reveals source");
        rail.keyboardMode = false; rail.currentIndex = -1;
        harness.ok(rail.ratingVisibleAt(0) === false, "rating hides again once focus moves away");

        // ranked mode (Top 10): numeral badge only — never a competing rating glyph, even revealed.
        rail.ranked = true;
        rail.testHoveredIndex = 0;
        harness.ok(rail.ratingVisibleAt(0) === false, "ranked mode suppresses the rating glyph even on reveal");
        harness.ok(rail.sourceVisibleAt(0) === true, "ranked mode still allows the source label on reveal");
        rail.testHoveredIndex = -1;
        rail.ranked = false;

        var seeAllCount = 0;
        rail.seeAllActivated.connect(function() { seeAllCount++; });
        rail.seeAllActivated();
        harness.ok(seeAllCount === 1, "See All is wired to seeAllActivated()");

        var activated = null;
        rail.itemActivated.connect(function(it) { activated = it; });
        rail.itemActivated(items[0]);
        harness.ok(activated === items[0], "a card activation forwards the book item via itemActivated()");

        // an empty, non-loading rail collapses entirely; a loading rail still reserves its shelf.
        rail.items = [];
        harness.ok(rail.visible === false, "an empty, non-loading rail collapses entirely (no placeholder shelf)");
        rail.loading = true;
        harness.ok(rail.visible === true, "a loading rail still renders (its own independent loading state)");

        rail.destroy();
    }

    function runExplorePageChecks(doneCb) {
        var cat = harness.makeFakeCatalog({
            popularItems: [harness.bookRow("pop1", "Popular One", "Author P", "p1.png", 4.5)],
            topRatedItems: [harness.bookRow("tr1", "Top Rated One", "Author T", "t1.png", 4.8)],
            newReleasesItems: [harness.bookRow("nr1", "New One", "Author N", "n1.png", 4.1)],
            trendingItems: [harness.bookRow("tn1", "Trending One", "Author G", "g1.png", 4.2)],
            mostReadItems: [harness.bookRow("mr1", "Most Read One", "Author M", "m1.png", 4.3)],
            classicsItems: [harness.bookRow("cl1", "Classic One", "Author C", "c1.png", 4.6)],
            top10Items: [harness.bookRow("top1", "Top10 One", "Author X", "x1.png", 4.9)],
            mosaicByFacet: harness.mosaicFixture
        });
        var extList = harness.oneBookExtension("libgen", "LibGen Mirror");
        var ext = harness.makeFakeExtensionsSource(extList);
        var extMetas = [{ id: "e1", title: "Ext Book One", author: "Ext Author", cover: "e1.png", imdbRating: "4.2" }];

        var page1 = pageComp.createObject(harness, {
            catalogSource: cat, extensionsSource: ext, pageFetcher: harness.syncFetcher(extMetas),
            preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("page1") })
        });
        harness.ok(page1 !== null, "BiblioExplorePage constructs with injected fakes");
        if (page1) {
            page1._prefs.reset();

            var rows = page1.displayRows;
            harness.ok(rows.length === 8, "default displayRows: top-10 + 1 extension + 6 house rails, got " + rows.length);
            harness.ok(rows[0] && rows[0].key === "top-10" && rows[0].kind === "top10" && rows[0].ranked === true,
                       "Top 10 renders first, in ranked mode");
            harness.ok(rows[1] && rows[1].kind === "extension",
                       "extension preview row renders second (right after Top 10, before the house rails)");
            var houseOrder = [];
            for (var hi = 2; hi < rows.length; hi++) houseOrder.push(rows[hi] ? rows[hi].key : null);
            harness.ok(JSON.stringify(houseOrder) === JSON.stringify(
                           ["popular", "top-rated", "new-releases", "trending", "most-read", "classics"]),
                       "the six house rails render in fixed order, got " + JSON.stringify(houseOrder));

            var top10Pin = rows[0].pin;
            harness.ok(top10Pin.type === "book" && top10Pin.catalogId === "popular" && top10Pin.sourceKind === "builtin"
                       && top10Pin.filterGroup === "" && top10Pin.filterKey === "",
                       "Top 10's See-All pin resolves to the Popular catalogue, got " + JSON.stringify(top10Pin));
            var extPin = rows[1].pin;
            harness.ok(extPin.type === "book" && extPin.sourceKind === "extension" && !!extPin.extensionId
                       && !!extPin.transportUrl && extPin.extCatalogId === "popular" && extPin.addonName === "LibGen Mirror",
                       "the extension row's See-All pin carries its extension identity, got " + JSON.stringify(extPin));

            // Regression (independent review, 2026-08-05): the pin's catalogId must be the SAME
            // composite key (transportUrl+"|book|"+catalogId) BiblioDiscoverApi.resolvePin()
            // matches a pin against. Round-trip the REAL extPin through the REAL resolvePin() —
            // exactly what happens when a user taps See All and lands on the Discover page — and
            // prove it resolves to the extension's OWN catalogue key, never falling back to the
            // "popular" built-in the old empty catalogId silently triggered.
            var resolvedExtPin = BiblioDiscoverApi.resolvePin(extPin,
                { extensions: extList, biblioCatalog: null, showExplicit: false });
            harness.ok(resolvedExtPin.missing === false && resolvedExtPin.catalogKey === extPin.catalogId
                       && resolvedExtPin.catalogKey !== "popular" && resolvedExtPin.catalogKey.indexOf("|book|") !== -1,
                       "the extension row's See-All pin resolves through the REAL BiblioDiscoverApi.resolvePin() "
                       + "to its own extension catalogue, not the Popular fallback, got " + JSON.stringify(resolvedExtPin));
            var housePin = rows[2].pin;
            harness.ok(housePin.type === "book" && housePin.catalogId === "popular" && housePin.sourceKind === "builtin",
                       "a house rail's See-All pin carries its catalogId, got " + JSON.stringify(housePin));

            // ── Slice 5 (spec 2026-08-15) per-rail attribution: the two Open Library rails
            //    carry the plain "Open Library" label, the Apple-seeded four keep the combined
            //    house label; Top 10 (discoverPage("popular")) keeps the default label too. ──
            var popItem = (page1.houseRowsMap["popular"] || [])[0];
            harness.ok(popItem && popItem.source === "Apple Books · Open Library",
                       "a popular house item keeps the combined house attribution, got "
                       + JSON.stringify(popItem && popItem.source));
            var mrItem = (page1.houseRowsMap["most-read"] || [])[0];
            harness.ok(mrItem && mrItem.source === "Open Library",
                       "a most-read house item carries the Open Library attribution, got "
                       + JSON.stringify(mrItem && mrItem.source));
            var clItem = (page1.houseRowsMap["classics"] || [])[0];
            harness.ok(clItem && clItem.source === "Open Library",
                       "a classics house item carries the Open Library attribution, got "
                       + JSON.stringify(clItem && clItem.source));
            var top10SrcItem = (page1.top10Items || [])[0];
            harness.ok(top10SrcItem && top10SrcItem.source === "Apple Books · Open Library",
                       "a Top 10 item keeps the default house attribution, got "
                       + JSON.stringify(top10SrcItem && top10SrcItem.source));

            // ── Slice 5 See-All lazy-enrich trigger: activating See-All on the two Open
            //    Library rails fires catalogSource.requestEnrichment exactly once with that
            //    rail's key; the four Apple-seeded rails never fire it. Emitted on the REAL
            //    delegate BiblioBookRail (found by title through the visual tree) so the
            //    PAGE's own onSeeAllActivated handler is what runs. ──
            var rails = harness.collectRails(page1, []);
            harness.ok(rails.length === rows.length,
                       "every displayRow renders exactly one BiblioBookRail, got " + rails.length);
            var pinCount = 0;
            page1.discoverPinRequested.connect(function() { pinCount++; });
            var mrRail = harness.railByTitle(rails, "Most Read");
            harness.ok(mrRail !== null, "the most-read rail is instantiated under its house title");
            if (mrRail) {
                mrRail.seeAllActivated();
                harness.ok(cat.enrichmentLog.length === 1 && cat.enrichmentLog[0] === "most-read",
                           "See-All on most-read fires requestEnrichment('most-read') exactly once, got "
                           + JSON.stringify(cat.enrichmentLog));
            }
            var clRail = harness.railByTitle(rails, "Classics");
            if (clRail) {
                clRail.seeAllActivated();
                harness.ok(cat.enrichmentLog.length === 2 && cat.enrichmentLog[1] === "classics",
                           "See-All on classics fires requestEnrichment('classics') exactly once, got "
                           + JSON.stringify(cat.enrichmentLog));
            }
            var popRail = harness.railByTitle(rails, "Popular");
            if (popRail) {
                popRail.seeAllActivated();
                harness.ok(cat.enrichmentLog.length === 2,
                           "See-All on popular (an Apple-seeded rail) fires NO enrichment, got "
                           + JSON.stringify(cat.enrichmentLog));
            }
            var expectedPins = (mrRail ? 1 : 0) + (clRail ? 1 : 0) + (popRail ? 1 : 0);
            harness.ok(pinCount === expectedPins,
                       "each See-All activation still routes the discover pin first, got " + pinCount);

            // the guard: a catalogSource WITHOUT requestEnrichment (an older native build or a
            // pre-update fake) makes the same See-All activation a silent no-op, never a
            // TypeError — the offscreen harness must never throw.
            var bareCat = harness.makeFakeCatalog({ mosaicByFacet: harness.mosaicFixture });
            delete bareCat.requestEnrichment;
            var barePage = pageComp.createObject(harness, {
                catalogSource: bareCat,
                extensionsSource: harness.makeFakeExtensionsSource([]),
                preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("page1bare") })
            });
            if (barePage) {
                var bareRails = harness.collectRails(barePage, []);
                var bareMrRail = harness.railByTitle(bareRails, "Most Read");
                var guardThrew = false;
                try { if (bareMrRail) bareMrRail.seeAllActivated(); }
                catch (e) { guardThrew = true; }
                harness.ok(guardThrew === false,
                           "See-All with a catalogSource lacking requestEnrichment never throws");
                barePage.destroy();
            }

            harness.ok(page1.mosaicSpecs.length === 3, "exactly three fixed mosaics");
            var mosKeys = page1.mosaicSpecs.map(function(s) { return s.key; });
            harness.ok(mosKeys.indexOf("mosaic-fiction") !== -1 && mosKeys.indexOf("mosaic-nonfiction") !== -1
                       && mosKeys.indexOf("mosaic-audience") !== -1, "mosaics are Fiction / Nonfiction / Audience");
            harness.ok((page1.mosaicItemsByKey["mosaic-fiction"] || []).length > 0, "fiction mosaic has items");
            harness.ok((page1.mosaicItemsByKey["mosaic-nonfiction"] || []).length > 0, "nonfiction mosaic has items");
            harness.ok((page1.mosaicItemsByKey["mosaic-audience"] || []).length > 0, "audience mosaic has items");
            for (var mi = 0; mi < rows.length; mi++)
                harness.ok(rows[mi].kind !== "mosaic", "mosaics never appear inside the customizable displayRows list");

            var fictionPin = page1.mosaicPin(page1.mosaicSpecs[0]);
            harness.ok(fictionPin.type === "book" && fictionPin.filterGroup === "genre"
                       && fictionPin.filterKey === "literary-fiction" && fictionPin.sourceKind === "builtin",
                       "Fiction mosaic tile pin filters Discover to genre/literary-fiction, got " + JSON.stringify(fictionPin));
            var nonfictionPin = page1.mosaicPin(page1.mosaicSpecs[1]);
            harness.ok(nonfictionPin.filterGroup === "genre" && nonfictionPin.filterKey === "nonfiction",
                       "Nonfiction mosaic tile pin filters Discover to genre/nonfiction");
            var audiencePin = page1.mosaicPin(page1.mosaicSpecs[2]);
            harness.ok(audiencePin.filterGroup === "audience" && audiencePin.filterKey === "young-adult",
                       "Audience mosaic tile pin filters Discover to audience/young-adult");

            // reordering never touches the fixed mosaics — they are simply outside the
            // customization system, not merely "last by convention".
            page1._prefs.move("trending", 0);
            harness.ok(page1.mosaicSpecs.length === 3 && (page1.mosaicItemsByKey["mosaic-fiction"] || []).length > 0,
                       "row reordering never affects the fixed mosaics");
            page1._prefs.reset();

            var popItem = (page1.houseRowsMap["popular"] || [])[0];
            harness.ok(popItem && popItem.author === "Author P", "a house item carries author at rest");
            harness.ok(popItem && popItem.blurb === undefined && popItem.description === undefined,
                       "no generated row/item blurb field anywhere on a normalized house item");
            var extKey1 = Object.keys(page1.catalogByExtKey)[0];
            var extItems1 = page1._extRowItems(extKey1);
            harness.ok(extItems1.length === 1 && extItems1[0].author === "Ext Author",
                       "an extension item carries author at rest, got " + JSON.stringify(extItems1));
            harness.ok(extItems1[0].blurb === undefined && extItems1[0].description === undefined,
                       "no blurb field on a normalized extension item");

            // drag handles appear ONLY in edit mode.
            page1.editMode = false;
            harness.ok(page1.editControlsVisibleAt(0) === false, "drag/move/hide controls are hidden outside edit mode");
            page1.editMode = true;
            harness.ok(page1.editControlsVisibleAt(0) === true, "drag/move/hide controls appear in edit mode");
            page1.editMode = false;

            // hide/show: a hidden row disappears in normal browsing, resurfaces (marked) in edit mode.
            page1._prefs.setVisible("popular", false);
            var keysNoEdit = page1.displayRows.map(function(r) { return r.key; });
            harness.ok(keysNoEdit.indexOf("popular") === -1, "a hidden row never renders outside edit mode");
            page1.editMode = true;
            var popularEdit = page1.displayRows.filter(function(r) { return r.key === "popular"; })[0];
            harness.ok(popularEdit !== undefined && popularEdit.hidden === true,
                       "a hidden row is present (marked hidden) in edit mode so it can be re-shown");
            page1._prefs.setVisible("popular", true);
            page1.editMode = false;

            // keyboard Move Up/Move Down is the accessible equivalent of drag — SAME move() call.
            var firstKey = page1.customizableRowKeys[0];
            page1.moveRowBy(firstKey, 1);
            harness.ok(page1._prefs.order.indexOf(firstKey) === 1,
                       "Move Down calls BiblioExplorePreferences.move() and persists the new position, got "
                       + JSON.stringify(page1._prefs.order));
            page1._prefs.reset();

            // pointer drag: a TEMPORARY visible order, committed through the SAME move() only on release.
            var dragKey = page1.customizableRowKeys[0];
            var orderBeforeDrag = page1._prefs.order.slice();
            page1.beginDrag(dragKey);
            harness.ok(page1.draggingKey === dragKey, "beginDrag() marks the row as dragging");
            page1.updateDragDelta(dragKey, page1.dragRowStride * 2);
            harness.ok(JSON.stringify(page1._prefs.order) === JSON.stringify(orderBeforeDrag),
                       "an in-progress drag never writes BiblioExplorePreferences (temporary visible order only)");
            harness.ok(page1.dragKeys[0] !== dragKey, "the drag's temporary order already reflects the pending move");
            var dragTargetIndex = page1.dragKeys.indexOf(dragKey);
            page1.endDrag(dragKey);
            harness.ok(page1.draggingKey === "" && page1.dragKeys === null, "endDrag() clears the temporary drag state");
            harness.ok(page1._prefs.order.indexOf(dragKey) === dragTargetIndex,
                       "releasing the drag commits the SAME final position through move(), got "
                       + JSON.stringify(page1._prefs.order));
            page1._prefs.reset();
        }

        // an extension row whose fetch answers empty collapses entirely — no placeholder shelf.
        var page2 = pageComp.createObject(harness, {
            catalogSource: harness.makeFakeCatalog({ mosaicByFacet: harness.mosaicFixture }),
            extensionsSource: harness.makeFakeExtensionsSource(harness.oneBookExtension("empty-ext", "Empty Source")),
            pageFetcher: harness.syncFetcher([]),
            preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("page2") })
        });
        harness.ok(page2 !== null, "second page instance constructs (empty-extension scenario)");
        if (page2) {
            var keys2 = page2.displayRows.map(function(r) { return r.key; });
            var hasExt2 = false;
            for (var k2 = 0; k2 < keys2.length; k2++) if (Rules.isExtensionKey(keys2[k2])) hasExt2 = true;
            harness.ok(!hasExt2, "an extension row whose fetch returns empty collapses entirely");
        }

        // ExplicitContentPolicy gates an extension item; the native includeExplicit param
        // already gates house/mosaic rows server-side, so only the extension path needs the
        // JS-side check here.
        var explicitMeta = [
            { id: "e-ok", title: "Fine Book", author: "A", cover: "ok.png" },
            { id: "e-bad", title: "Explicit Book", author: "B", cover: "bad.png", explicit: true }
        ];
        var page3 = pageComp.createObject(harness, {
            catalogSource: harness.makeFakeCatalog({ mosaicByFacet: harness.mosaicFixture }),
            extensionsSource: harness.makeFakeExtensionsSource(harness.oneBookExtension("gate-ext", "Gate Source")),
            pageFetcher: harness.syncFetcher(explicitMeta),
            showExplicit: false,
            preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("page3") })
        });
        if (page3) {
            var extKey3 = Object.keys(page3.catalogByExtKey)[0];
            var items3 = page3._extRowItems(extKey3);
            harness.ok(items3.length === 1 && items3[0].id === "e-ok",
                       "ExplicitContentPolicy gates an explicit extension item when showExplicit is false, got "
                       + JSON.stringify(items3));
        }
        var page4 = pageComp.createObject(harness, {
            catalogSource: harness.makeFakeCatalog({ mosaicByFacet: harness.mosaicFixture }),
            extensionsSource: harness.makeFakeExtensionsSource(harness.oneBookExtension("gate-ext2", "Gate Source 2")),
            pageFetcher: harness.syncFetcher(explicitMeta),
            showExplicit: true,
            preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("page4") })
        });
        if (page4) {
            var extKey4 = Object.keys(page4.catalogByExtKey)[0];
            var items4 = page4._extRowItems(extKey4);
            harness.ok(items4.length === 2, "showExplicit true reveals the gated item too, got " + items4.length);
        }

        // ── Part A regression: a LIVE showExplicit flip must actually re-fetch and re-render
        //    extension rows. Before this fix there was no onShowExplicitChanged handler at all in
        //    BiblioExplorePage.qml, so extensionRowData kept whatever the FIRST (construction-time)
        //    fetch's callback captured — a stale explicit (or stale hidden) item could persist on
        //    screen forever after a live preference flip, unlike the house rails' declarative
        //    bindings on page.showExplicit (which already react correctly). ──
        var flipCounter = { calls: 0 };
        var flipPage = pageComp.createObject(harness, {
            catalogSource: harness.makeFakeCatalog({ mosaicByFacet: harness.mosaicFixture }),
            extensionsSource: harness.makeFakeExtensionsSource(harness.oneBookExtension("flip-ext", "Flip Source")),
            pageFetcher: harness.countingFetcher(explicitMeta, flipCounter),
            showExplicit: false,
            preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("flip") })
        });
        if (flipPage) {
            var flipExtKey = Object.keys(flipPage.catalogByExtKey)[0];
            var callsBeforeFlip = flipCounter.calls;
            harness.ok(flipPage._extRowItems(flipExtKey).length === 1,
                       "flip setup: the explicit item is gated while showExplicit=false, got "
                       + JSON.stringify(flipPage._extRowItems(flipExtKey)));
            flipPage.showExplicit = true;
            harness.ok(flipCounter.calls > callsBeforeFlip,
                       "showExplicit flip: a REAL new extension fetch happened (calls " + callsBeforeFlip
                       + " -> " + flipCounter.calls + ")");
            var itemsAfterFlip = flipPage._extRowItems(flipExtKey);
            harness.ok(itemsAfterFlip.length === 2,
                       "showExplicit flip: the extension row actually re-renders with the now-visible item, got "
                       + JSON.stringify(itemsAfterFlip));
            flipPage.showExplicit = false;
            var itemsRestored = flipPage._extRowItems(flipExtKey);
            harness.ok(itemsRestored.length === 1,
                       "showExplicit flip back off: the extension row actually drops the explicit item again, got "
                       + JSON.stringify(itemsRestored));
        }

        // independent per-shelf loading: house rails render immediately while a SLOW extension
        // fetch is still pending, and the slow row resolves later without blocking anything else.
        var slowPage = pageComp.createObject(harness, {
            catalogSource: harness.makeFakeCatalog({
                popularItems: [harness.bookRow("ip1", "Independent Popular", "Auth", "i1.png", 4.0)],
                topRatedItems: [harness.bookRow("ip2", "Independent TopRated", "Auth", "i2.png", 4.0)],
                newReleasesItems: [harness.bookRow("ip3", "Independent New", "Auth", "i3.png", 4.0)],
                trendingItems: [harness.bookRow("ip4", "Independent Trending", "Auth", "i4.png", 4.0)],
                top10Items: [harness.bookRow("ip5", "Independent Top10", "Auth", "i5.png", 4.0)],
                mosaicByFacet: harness.mosaicFixture
            }),
            extensionsSource: harness.makeFakeExtensionsSource(harness.oneBookExtension("slow-ext", "Slow Source")),
            pageFetcher: harness.delayedFetcher([{ id: "slow1", title: "Slow Book", author: "Slow Author", cover: "slow.png" }], 250),
            preferences: prefsComp.createObject(harness, { settingsLocation: harness.tempIni("page5") })
        });
        harness.pendingSlowPage = slowPage;
        if (slowPage) {
            var keys5 = slowPage.displayRows.map(function(r) { return r.key; });
            harness.ok(keys5.indexOf("popular") !== -1 && keys5.indexOf("top-rated") !== -1
                       && keys5.indexOf("new-releases") !== -1 && keys5.indexOf("trending") !== -1,
                       "house rails render immediately even while an extension fetch is still pending");
            var extKey5 = Object.keys(slowPage.catalogByExtKey)[0];
            harness.ok(slowPage._extRowStatus(extKey5) === "loading",
                       "a slow extension row starts in its OWN independent loading state");
        }

        doneCb();
    }

    function verifySlowExtensionResolved() {
        var slowPage = harness.pendingSlowPage;
        if (slowPage) {
            var extKey5 = Object.keys(slowPage.catalogByExtKey)[0];
            harness.ok(slowPage._extRowStatus(extKey5) === "ok",
                       "the slow extension row resolves independently once its own fetch completes");
            var keys5b = slowPage.displayRows.map(function(r) { return r.key; });
            var hasExt5 = false;
            for (var i5 = 0; i5 < keys5b.length; i5++) if (Rules.isExtensionKey(keys5b[i5])) hasExt5 = true;
            harness.ok(hasExt5, "the resolved slow extension row now renders in its place");
        }
    }

    // ---------------------------------------------------------------------
    // Pure rules — no QML instance required, run first and synchronously.
    // ---------------------------------------------------------------------
    function runRuleChecks() {
        // exact default order, no extensions installed
        var bare = Rules.defaultRows([]);
        ok(JSON.stringify(bare) === JSON.stringify(
               ["top-10", "popular", "top-rated", "new-releases", "trending", "most-read", "classics"]),
           "default order with zero extensions, got " + JSON.stringify(bare));

        // empty extension-section collapse: no "ext:" keys present at all, not a present-but-empty marker
        var hasExtKey = false;
        for (var i = 0; i < bare.length; i++) if (Rules.isExtensionKey(bare[i])) hasExtKey = true;
        ok(!hasExtKey, "zero enabled extensions means no ext: keys at all (collapsed, not present-but-empty)");

        // extension keys land between top-10 and the house rails, derived from stable id
        var withExt = Rules.defaultRows([{ id: "com.example.libgen", title: "LibGen Mirror" },
                                          { id: "com.example.annas", title: "Anna's Archive" }]);
        ok(JSON.stringify(withExt) === JSON.stringify([
            "top-10", "ext:com.example.libgen", "ext:com.example.annas",
            "popular", "top-rated", "new-releases", "trending", "most-read", "classics"
        ]), "extension keys ordered between top-10 and house rails, got " + JSON.stringify(withExt));

        // stable extension keys: derived from id, NOT the display title
        var byTitleA = Rules.defaultRows([{ id: "com.example.libgen", title: "LibGen Mirror" }]);
        var byTitleB = Rules.defaultRows([{ id: "com.example.libgen", title: "Renamed Completely" }]);
        ok(JSON.stringify(byTitleA) === JSON.stringify(byTitleB),
           "extension key is stable across a title/name change (id-derived only)");
        ok(byTitleA[1] === "ext:com.example.libgen", "extension key format is ext:<id>");

        // plain string ids are also accepted (no title at all)
        var plainIds = Rules.defaultRows(["com.example.libgen"]);
        ok(plainIds[1] === "ext:com.example.libgen", "plain string extension id is accepted");

        // duplicate stable id collapses to one key, not repeated
        var dup = Rules.defaultRows([{ id: "com.example.libgen" }, { id: "com.example.libgen" }]);
        ok(dup.length === 8, "duplicate extension id collapses to a single key, got " + JSON.stringify(dup));

        // mosaics are never part of the row inventory at all
        var mosaicish = ["fiction", "nonfiction", "audience", "mosaic-fiction", "mosaic-nonfiction", "mosaic-audience"];
        var rowsAll = Rules.defaultRows([{ id: "x" }]);
        for (var m = 0; m < mosaicish.length; m++)
            ok(rowsAll.indexOf(mosaicish[m]) === -1, "mosaic key '" + mosaicish[m] + "' never appears in defaultRows()");

        // ── applyCustomization ──
        var rows = Rules.defaultRows([{ id: "ext-a" }, { id: "ext-b" }]);
        // rows = [top-10, ext:ext-a, ext:ext-b, popular, top-rated, new-releases, trending, most-read, classics]

        // no customization -> identity order, nothing hidden
        var plain = Rules.applyCustomization(rows, { order: [], hidden: [] }, false);
        var plainKeys = plain.map(function(r) { return r.key; });
        ok(JSON.stringify(plainKeys) === JSON.stringify(rows), "no customization preserves default order");
        ok(plain.every(function(r) { return r.hidden === false; }), "nothing hidden by default");

        // drag-equivalent move: saved order reorders the effective list
        var reordered = Rules.applyCustomization(rows, { order: ["trending", "top-10"], hidden: [] }, false);
        var reorderedKeys = reordered.map(function(r) { return r.key; });
        ok(reorderedKeys[0] === "trending" && reorderedKeys[1] === "top-10",
           "saved order takes precedence (drag-equivalent move), got " + JSON.stringify(reorderedKeys));
        ok(reorderedKeys.length === rows.length, "reordering keeps every available row, none dropped");

        // new-extension append: a row not present in the saved order is appended, not dropped.
        // Also the Slice 5 six-shelf flip case: a save from the OLD four-key-house world keeps
        // its own relative order at the front while most-read/classics — unknown to that save —
        // are appended safely behind it, never dropped and never resurrected out of order.
        var savedBeforeExt = ["trending", "top-10", "popular", "top-rated", "new-releases"]; // missing both ext keys
        var appended = Rules.applyCustomization(rows, { order: savedBeforeExt, hidden: [] }, false);
        var appendedKeys = appended.map(function(r) { return r.key; });
        ok(appendedKeys.indexOf("ext:ext-a") !== -1 && appendedKeys.indexOf("ext:ext-b") !== -1,
           "rows missing from saved order are appended safely, got " + JSON.stringify(appendedKeys));
        ok(appendedKeys.length === rows.length, "append does not drop or duplicate any row");
        ok(JSON.stringify(appendedKeys.slice(0, savedBeforeExt.length)) === JSON.stringify(savedBeforeExt),
           "an OLD four-key-house saved order keeps its relative order at the front, got "
           + JSON.stringify(appendedKeys));
        ok(appendedKeys.indexOf("most-read") !== -1 && appendedKeys.indexOf("classics") !== -1,
           "the two new house keys are appended for an old four-key save, never dropped");

        // removed-key ignore: a saved order entry for a row no longer available is silently dropped
        var shrunkRows = Rules.defaultRows([{ id: "ext-a" }]); // ext-b no longer installed
        var withStaleEntry = Rules.applyCustomization(shrunkRows,
            { order: ["top-10", "ext:ext-a", "ext:ext-b", "popular"], hidden: [] }, false);
        var staleKeys = withStaleEntry.map(function(r) { return r.key; });
        ok(staleKeys.indexOf("ext:ext-b") === -1, "a removed row's saved order entry is dropped, not resurrected");
        ok(staleKeys.length === shrunkRows.length, "dropped entry does not leave a gap or duplicate");

        // hide/show: a hidden row is omitted from normal (non-edit) browsing
        var withHidden = Rules.applyCustomization(rows, { order: [], hidden: ["popular"] }, false);
        var hiddenKeysNormal = withHidden.map(function(r) { return r.key; });
        ok(hiddenKeysNormal.indexOf("popular") === -1, "a hidden row is omitted in normal (non-edit) mode");

        // ...but still surfaced (marked hidden:true) in edit mode so it can be toggled back on
        var withHiddenEdit = Rules.applyCustomization(rows, { order: [], hidden: ["popular"] }, true);
        var popularEntry = withHiddenEdit.filter(function(r) { return r.key === "popular"; })[0];
        ok(popularEntry !== undefined && popularEntry.hidden === true,
           "a hidden row is present-but-marked-hidden in edit mode");
        ok(withHiddenEdit.length === rows.length, "edit mode surfaces every row, hidden or not");

        // applyCustomization never mutates its inputs
        var inputRows = rows.slice();
        var inputCustom = { order: ["top-10"], hidden: ["popular"] };
        var inputOrderCopy = inputCustom.order.slice();
        var inputHiddenCopy = inputCustom.hidden.slice();
        Rules.applyCustomization(inputRows, inputCustom, false);
        ok(JSON.stringify(inputRows) === JSON.stringify(rows), "applyCustomization does not mutate the rows array");
        ok(JSON.stringify(inputCustom.order) === JSON.stringify(inputOrderCopy) &&
           JSON.stringify(inputCustom.hidden) === JSON.stringify(inputHiddenCopy),
           "applyCustomization does not mutate the customization object");
    }

    // ---------------------------------------------------------------------
    // QSettings persistence — sequential Timer-staged phases.
    // ---------------------------------------------------------------------
    Component { id: prefsComp; UI.BiblioExplorePreferences {} }
    property var instA: null
    property var instB: null
    property var instC: null

    // Phase 0 — establish an empty baseline (overwrites any stale value from a prior run).
    Timer {
        interval: 20; running: true; repeat: false
        onTriggered: {
            runRuleChecks();

            harness.instA = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            harness.ok(harness.instA !== null, "instance A constructs");
            harness.ok(harness.instA && String(harness.instA.settingsLocation).indexOf("colosseum_biblio_explore_test.ini") !== -1,
                       "settingsLocation routes to the temp INI");
            if (harness.instA) harness.instA.reset();
            flushA.start();
        }
    }
    Timer { id: flushA; interval: 300; repeat: false
        onTriggered: { if (harness.instA) harness.instA.destroy(); phase1.start(); } }

    // Phase 1 — a fresh instance reads the empty baseline, then we drive real mutations.
    Timer { id: phase1; interval: 300; repeat: false
        onTriggered: {
            harness.instB = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            var b = harness.instB;
            harness.ok(b && JSON.stringify(b.order) === JSON.stringify([]), "baseline: order reads empty after reload");
            harness.ok(b && JSON.stringify(b.hidden) === JSON.stringify([]), "baseline: hidden reads empty after reload");

            // changed() fires ONLY on a real mutation.
            var fired = 0;
            b.changed.connect(function() { fired++; });

            // move() on an unknown key with a no-op destination (append position == requested
            // index) should not persist an order the reload can't otherwise reproduce; a real
            // move must both mutate `order` and fire changed().
            b.move("top-10", 0);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["top-10"]) && fired === 1,
                       "move() on a fresh key inserts it and fires changed(), got " + JSON.stringify(b.order));

            b.move("popular", 0);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["popular", "top-10"]) && fired === 2,
                       "move(key, toIndex) reorders to an ABSOLUTE index, got " + JSON.stringify(b.order));

            // boundary no-op: moving the first key to index 0 (its current position) is silent
            b.move("popular", 0);
            harness.ok(fired === 2, "a boundary/no-op move stays silent (no changed())");

            // out-of-range index is clamped rather than throwing or corrupting order
            b.move("top-10", 999);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["popular", "top-10"]) && fired === 2,
                       "move() clamps an out-of-range index instead of throwing, got " + JSON.stringify(b.order));
            b.move("popular", 5);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["top-10", "popular"]) && fired === 3,
                       "move() clamps to the end and still fires changed() for a real move, got " + JSON.stringify(b.order));

            // setVisible toggles hidden membership and is idempotent (no double-fire on repeat).
            // `popular` is left HIDDEN at the end of this phase (no re-show call) ON PURPOSE: the
            // Phase 2 reload proof needs a NON-empty `hidden` to survive reload, otherwise a real
            // regression in hiddenJson persistence would go uncaught (an empty array "survives"
            // reload either way, vacuously).
            b.setVisible("popular", false);
            harness.ok(b.hidden.indexOf("popular") !== -1 && fired === 4, "setVisible(key,false) hides a shown row");
            b.setVisible("popular", false);
            harness.ok(fired === 4, "setVisible with no actual change stays silent");

            flushB.start();
        }
    }
    Timer { id: flushB; interval: 300; repeat: false
        onTriggered: { if (harness.instB) harness.instB.destroy(); phase2.start(); } }

    // Phase 2 — a fresh instance must read the real state back (order + hidden survive reload).
    Timer { id: phase2; interval: 300; repeat: false
        onTriggered: {
            harness.instC = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            var c = harness.instC;
            harness.ok(c !== null, "instance C constructs");
            harness.ok(JSON.stringify(c.order) === JSON.stringify(["top-10", "popular"]),
                       "persistence: order survived destroy + reload, got " + JSON.stringify(c.order));
            harness.ok(JSON.stringify(c.hidden) === JSON.stringify(["popular"]),
                       "persistence: a NON-empty hidden set survived destroy + reload (popular stayed hidden), got "
                       + JSON.stringify(c.hidden));

            // only stable keys are ever persisted, never a display title. Structural, not
            // vacuous: a hyphenated-key JSON array never contains a literal space regardless of
            // content, so the old `raw.indexOf(" ") === -1` half of this check was always true —
            // assert every persisted entry actually IS one of the stable row keys instead.
            var raw = String(c.settingsStore ? c.settingsStore.orderJson : "");
            var persistedOrder = [];
            try { persistedOrder = JSON.parse(raw || "[]"); } catch (e) { persistedOrder = []; }
            var stableKeyPattern = /^(top-10|ext:|popular|top-rated|new-releases|trending|most-read|classics)/;
            var allStableKeys = persistedOrder.length > 0
                && persistedOrder.every(function(k) { return stableKeyPattern.test(k); });
            harness.ok(allStableKeys,
                       "persisted order carries ONLY stable row keys, no display-title text, got " + raw);

            // reset restores empty order/hidden and fires changed()
            var resetFired = false;
            c.changed.connect(function() { resetFired = true; });
            c.reset();
            harness.ok(resetFired, "reset() fires changed()");
            harness.ok(JSON.stringify(c.order) === JSON.stringify([]) && JSON.stringify(c.hidden) === JSON.stringify([]),
                       "reset() clears both order and hidden");
            if (harness.instC) harness.instC.destroy();

            // Task 7 — BiblioBookRail direct checks, then BiblioExplorePage (which itself spins
            // up a slow-extension scenario finished off by pagePhaseFinal below).
            harness.runBookRailChecks();
            harness.runExplorePageChecks(function() { pagePhaseFinal.start(); });
        }
    }

    // Phase 3 — after the slow (250ms) extension fetch in runExplorePageChecks has had time to
    // resolve, verify it landed independently and print the file's final OK marker.
    Timer {
        id: pagePhaseFinal
        interval: 400
        repeat: false
        onTriggered: {
            harness.verifySlowExtensionResolved();
            if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
            else console.log("BIBLIO_EXPLORE_PAGE_OK");
            Qt.exit(harness.fails.length);
        }
    }
}
