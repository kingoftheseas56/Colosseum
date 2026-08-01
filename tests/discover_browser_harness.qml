// Offscreen shared-shell contract proof for DiscoverBrowser — the WORLD-NEUTRAL Discover
// shell (Task 3). Drives the shell through a FAKE adapter (no world API, no Extensions),
// exercising every contract the Theatre wrapper and the future Tankoban wrapper rely on.
// House rule: NEVER throw (hangs offscreen) — collect fails, print the unique OK marker
// DISCOVER_BROWSER_OK only when clean, single Qt.exit(fails.length). A Timer lets any
// async fetchPage callbacks resolve before the assertions run.
import QtQuick
import "../qml" as UI

Item {
    id: root

    // ── fake adapter A — auto-completing; feeds most assertions. Exactly the shared
    //    contract: types/catalogs/filters/defaultCatalog/resolvePin/fetchPage. A filtered
    //    ask answers EMPTY (so we can prove the filtered-empty state + its clear action);
    //    any unfiltered ask answers with ONE coverless normalized card. ──
    QtObject {
        id: fake
        function types() { return [{key:"manga", label:"Manga"},{key:"comics", label:"Comics"}] }
        function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Tankoban", attribution:"Tankoban built-in catalogue"}] }
        function filters(t, c) { return [{group:"Genres", options:[{key:"action",label:"Action"}]}] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) {
            if (p && p.type === "__gone__")   // the missing-catalogue path
                return {missing:true, type:"manga", catalogKey:"", filterGroup:"", filterKey:"", missingName:"Ghost Source"}
            return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:p.filterGroup||"", filterKey:p.filterKey||""}
        }
        function fetchPage(s, cursor, gen, done) {
            if (s.filterKey && s.filterKey.length)
                done(gen, {items:[], nextCursor:null, exhausted:true, freshness:"live", warning:""})
            else
                done(gen, {items:[{id:"1",type:s.type,title:"One",cover:"",year:2001,rating:8,format:"Manga",publisher:"",availability:true,explicit:false,raw:{}}],
                           nextCursor:null, exhausted:true, freshness:"bundled", warning:""})
        }
    }

    // ── fake adapter B — DEFERRED; never auto-completes. It stashes (gen, done) so the
    //    test can fire a STALE generation (must be rejected) and then a FRESH one carrying
    //    the offline-warning sentinel (must land + raise showOfflineNotice). ──
    QtObject {
        id: deferredFake
        property int capturedGen: -1
        property var capturedDone: null
        function types() { return [{key:"manga", label:"Manga"}] }
        function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Tankoban", attribution:"Tankoban built-in catalogue"}] }
        function filters(t, c) { return [] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) { return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:"", filterKey:""} }
        function fetchPage(s, cursor, gen, done) { capturedGen = gen; capturedDone = done }
    }

    UI.DiscoverBrowser { id: browser;  width: 1200; height: 700; adapter: fake;         fallbackType: "manga" }
    UI.DiscoverBrowser { id: browser2; width: 1200; height: 700; adapter: deferredFake; fallbackType: "manga" }

    // item-activation observer (assertion: fires ONCE, with the normalized item)
    property int openCount: 0
    property var openedItem: null
    Connections {
        target: browser
        function onItemOpenRequested(item) { root.openCount++; root.openedItem = item }
    }

    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            // ── non-vacuity self-check: prove ok() actually catches a wrong expectation ──
            var selfCheck = [];
            (function(){ if (browser.currentType !== "THIS_IS_DELIBERATELY_WRONG") selfCheck.push("caught"); })();
            ok(selfCheck.length === 1, "non-vacuity: ok() detects a wrong expectation");

            // ── default type + catalogue (init picked types()[0] and defaultCatalog) ──
            ok(browser.currentType === "manga", "default type = types()[0]: " + browser.currentType);
            ok(browser.currentCatalogKey === "popular", "default catalogue = defaultCatalog(): " + browser.currentCatalogKey);
            ok(browser.currentCatalog && browser.currentCatalog.title === "Popular", "currentCatalog descriptor resolved");
            ok(browser.loading === false, "not loading after the first (exhausted) page");
            ok(browser.freshness === "bundled", "page freshness surfaced: " + browser.freshness);
            ok(browser.showOfflineNotice === false, "no offline notice for a live/bundled page");

            // ── coverless-card construction (normalized shape carried through) ──
            ok(browser.items.length === 1, "one normalized card landed: " + browser.items.length);
            ok(browser.items.length === 1 && browser.items[0].cover === "", "coverless card kept its empty cover");
            ok(browser.items.length === 1 && browser.items[0].id === "1" && browser.items[0].title === "One",
               "normalized id/title carried");

            // ── one active filter (single selection, not N pickers) ──
            ok(browser.filterHasOptions === true, "the catalogue offers one filter group");
            ok(Array.isArray(browser.filterMenuModel) && browser.filterMenuModel.length === 2,
               "single group menu = All + one option (no header): " + browser.filterMenuModel.length);

            // ── item activation: fires once, with the normalized item ──
            root.openCount = 0; root.openedItem = null;
            browser.activateIndex(0);
            browser.activateIndex(9);   // out of range → no extra fire
            ok(root.openCount === 1, "activateIndex fired itemOpenRequested exactly once: " + root.openCount);
            ok(root.openedItem && root.openedItem.id === "1" && root.openedItem.raw !== undefined,
               "activation carried the normalized item");

            // ── filtered-empty state + clear action (via the picker's encoded key) ──
            // the single filter picker hands back one encoded option key (group + SEP + key);
            // prove that round-trips back into one active filter selection.
            var encoded = browser.filterMenuModel[1].key;
            ok(encoded === "Genres" + browser._filterSep + "action", "menu option key encodes group+key: " + encoded);
            browser._applyFilterKey(encoded);
            ok(browser.filterKey === "action" && browser.filterGroup === "Genres", "picker key decoded to one active filter");
            ok(browser.filterSelection && browser.filterSelection.key === "action", "filterSelection reflects the pick");
            ok(browser.items.length === 0, "filtered catalogue answered empty");
            ok(browser.emptyMessage === browser.textFilterEmpty, "filtered-empty message: " + browser.emptyMessage);
            browser.clearFilter();
            ok(browser.filterKey === "" && browser.items.length === 1, "clear filter restores the unfiltered wall");

            // ── per-type session state restoration ──
            browser.setFilter("Genres", "action");            // manga now filtered
            browser.selectType("comics");
            ok(browser.currentType === "comics" && browser.filterKey === "",
               "switching type opens fresh (no filter bleed)");
            browser.selectType("manga");
            ok(browser.currentType === "manga" && browser.filterGroup === "Genres" && browser.filterKey === "action",
               "per-type state restored on return");
            browser.clearFilter();                            // reset for the pin steps

            // ── pin application (found) ──
            browser.applyPin({type:"comics", catalogId:"popular"});
            ok(browser.currentType === "comics" && browser.currentCatalogKey === "popular", "found pin applied");

            // ── missing-catalogue fallback: same type's built-in default, filter cleared, one notice ──
            browser.setFilter("Genres", "action");            // an invalid filter that must clear
            browser.applyPin({type:"__gone__"});
            ok(browser.currentType === "manga", "missing pin falls back to the pin's type: " + browser.currentType);
            ok(browser.currentCatalogKey === "popular", "missing pin selects the built-in default catalogue");
            ok(browser.filterKey === "", "missing pin cleared the invalid filter");
            ok(browser.noticeText.length > 0, "missing pin exposes one explanatory notice: " + browser.noticeText);

            // ── stale-generation rejection + offline notice (deferred adapter) ──
            var g1 = deferredFake.capturedGen, d1 = deferredFake.capturedDone;
            ok(g1 >= 1 && !!d1, "deferred adapter captured the first fetch");
            browser2.applyPin({type:"manga", catalogId:"popular"});   // supersedes the first fetch
            var g2 = deferredFake.capturedGen, d2 = deferredFake.capturedDone;
            ok(g2 > g1, "the superseding fetch got a newer generation: " + g1 + " -> " + g2);
            d1(g1, {items:[{id:"stale",type:"manga",title:"STALE",cover:"",year:0,rating:0,format:"",publisher:"",availability:true,explicit:false,raw:{}}],
                    nextCursor:null, exhausted:true, freshness:"", warning:""});
            ok(browser2.items.length === 0, "stale-generation page rejected");
            d2(g2, {items:[{id:"fresh",type:"manga",title:"FRESH",cover:"",year:0,rating:0,format:"",publisher:"",availability:true,explicit:false,raw:{}}],
                    nextCursor:null, exhausted:true, freshness:"live", warning:"Showing offline catalogue"});
            ok(browser2.items.length === 1 && browser2.items[0].id === "fresh", "fresh-generation page accepted");
            ok(browser2.showOfflineNotice === true, "offline notice raised on the sentinel warning");

            if (fails.length) console.log("DISCOVER_BROWSER FAILS:\n  " + fails.join("\n  "));
            else console.log("DISCOVER_BROWSER_OK");
            Qt.exit(fails.length);
        }
    }
}
