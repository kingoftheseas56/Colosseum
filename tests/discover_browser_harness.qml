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

    // ── fake adapter C — two types, NEVER completes; counts fetches. Proves the stranded-wall
    //    regression: a type left mid-first-fetch must re-issue a page on return, not sit empty. ──
    QtObject {
        id: strandFake
        property int fetchCount: 0
        property string lastType: ""
        function types() { return [{key:"manga", label:"Manga"},{key:"comics", label:"Comics"}] }
        function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Src", attribution:"Src"}] }
        function filters(t, c) { return [] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) { return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:"", filterKey:""} }
        function fetchPage(s, cursor, gen, done) { fetchCount++; lastType = s.type }   // never completes
    }

    // ── fake adapter D — two types, always answers EMPTY + EXHAUSTED; counts fetches. Proves the
    //    fix does NOT double-fetch a legitimately settled-empty catalogue on return. ──
    QtObject {
        id: exhaustFake
        property int fetchCount: 0
        function types() { return [{key:"a", label:"A"},{key:"b", label:"B"}] }
        function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Src", attribution:"Src"}] }
        function filters(t, c) { return [] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) { return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:"", filterKey:""} }
        function fetchPage(s, cursor, gen, done) { fetchCount++; done(gen, {items:[], nextCursor:null, exhausted:true, freshness:"live", warning:""}) }
    }

    // ── fake adapter E — the catalogue list NEVER changes (so refresh() would early-return),
    //    but the DATA behind it does (a "preference flip" stand-in). Counts fetches and answers
    //    a mutable item so a genuine reload is distinguishable from a no-op. ──
    QtObject {
        id: reloadFake
        property int fetchCount: 0
        property string answerId: "v1"
        function types() { return [{key:"manga", label:"Manga"}] }
        function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Src", attribution:"Src"}] }
        function filters(t, c) { return [] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) { return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:"", filterKey:""} }
        function fetchPage(s, cursor, gen, done) {
            fetchCount++
            done(gen, {items:[{id:reloadFake.answerId,type:"manga",title:reloadFake.answerId,cover:"",year:0,rating:0,format:"",publisher:"",availability:true,explicit:false,raw:{}}],
                       nextCursor:null, exhausted:true, freshness:"live", warning:""})
        }
    }

    // ── fake adapter F — a MUTABLE catalogue list (a built-in + a removable "extension") and a
    //    NEVER-auto-completing fetch. Models "extension removal during paging" (Task 9, Part B):
    //    a request in flight for an extension catalogue, followed by that extension disappearing
    //    (mirrors BiblioDiscoverPage.onExtensionsChanged -> browser.refresh()) BEFORE its reply
    //    lands. ──
    QtObject {
        id: extRemoveFake
        property bool ext1Present: true
        property int capturedGen: -1
        property var capturedDone: null
        function types() { return [{key:"book", label:"Books"}] }
        function catalogs(t) {
            var out = [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Biblio", attribution:"Biblio"}]
            if (ext1Present) out.push({key:"ext1", title:"Ext One", sourceKind:"extension", section:"Ext", attribution:"Ext"})
            return out
        }
        function filters(t, c) { return [] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) { return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:"", filterKey:""} }
        function fetchPage(s, cursor, gen, done) { capturedGen = gen; capturedDone = done }   // never auto-completes
    }

    // ── fake adapter G — an already-populated wall with a deferred next page. Proves that
    // deactivation cancels that page and reactivation resumes it even though existing items
    // remain visible (the subtle partial-page lifecycle regression).
    QtObject {
        id: pageCancelFake
        property int fetchCount: 0
        property int cancelCount: 0
        property int capturedGen: -1
        property var capturedDone: null
        function types() { return [{key:"book", label:"Books"}] }
        function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Src", attribution:"Src"}] }
        function filters(t, c) { return [] }
        function defaultCatalog(t) { return "popular" }
        function resolvePin(p) { return {missing:false, type:p.type, catalogKey:p.catalogId, filterGroup:"", filterKey:""} }
        function fetchPage(s, cursor, gen, done) {
            fetchCount++
            if (fetchCount === 1) {
                done(gen, {items:[{id:"seed",type:"book",title:"Seed",cover:"",year:0,rating:0,format:"",publisher:"",availability:true,explicit:false,raw:{}}],
                           nextCursor:1, exhausted:false, freshness:"bundled", warning:""})
                return function() {}
            }
            capturedGen = gen; capturedDone = done
            return function() { cancelCount++ }
        }
    }

    UI.DiscoverBrowser { id: browser;  width: 1200; height: 700; adapter: fake;         fallbackType: "manga" }
    UI.DiscoverBrowser { id: browser2; width: 1200; height: 700; adapter: deferredFake; fallbackType: "manga" }
    UI.DiscoverBrowser { id: browser3; width: 1200; height: 700; adapter: strandFake;   fallbackType: "manga" }
    UI.DiscoverBrowser { id: browser4; width: 1200; height: 700; adapter: exhaustFake;  fallbackType: "a" }
    UI.DiscoverBrowser { id: browser5; width: 1200; height: 700; adapter: reloadFake;   fallbackType: "manga" }
    UI.DiscoverBrowser { id: browser6; width: 1200; height: 700; adapter: extRemoveFake; fallbackType: "book" }
    UI.DiscoverBrowser { id: browser8; width: 1200; height: 700; adapter: pageCancelFake; fallbackType: "book" }
    // wide gallery-profile instance for the fixedGalleryWidth geometry proof (2026-08-06) —
    // 1920 is wide enough that the default (stretch-to-fill) behavior visibly exceeds the token.
    UI.DiscoverBrowser { id: browser7; width: 1920; height: 700; adapter: fake; fallbackType: "manga"; posterVisualProfile: "gallery" }

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
            // store side of scroll restore (the restore itself is eyes-on: GridView contentY is
            // not deterministic offscreen with tiny content — see the plan's restore note).
            ok(browser.typeStates["manga"] !== undefined && browser.typeStates["manga"].contentY !== undefined,
               "per-type state captured scroll (contentY) for restore");
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

            // ── stranded-wall regression (NEGATIVE CONTROL): a type left mid-first-fetch, returned
            //    to WITHOUT that fetch ever completing, must re-issue its page — not sit empty. This
            //    fails on the pre-fix restoreTypeState (no re-fetch), so it guards the regression. ──
            ok(strandFake.fetchCount === 1 && strandFake.lastType === "manga",
               "browser3 issued the first (never-completing) fetch for manga");
            browser3.selectType("comics");     // manga saved as {items:[], exhausted:false} (fence dropped its reply)
            ok(strandFake.fetchCount === 2 && strandFake.lastType === "comics", "fresh type fetched on switch");
            browser3.selectType("manga");      // return to the stranded type
            ok(strandFake.fetchCount === 3 && strandFake.lastType === "manga",
               "returning to a type stranded mid-fetch re-issued its fetch (fetchCount): " + strandFake.fetchCount);
            ok(browser3.loading === true && browser3.exhausted === false,
               "the returned type is fetching again, not stranded empty/settled");

            // ── retained-wall cancellation regression: an in-flight next page is canceled when
            // the world hides, then resumed on reactivation even though the first page remains.
            ok(pageCancelFake.fetchCount === 1 && browser8.items.length === 1 && !browser8.exhausted,
               "browser8 has a settled first page with more data available");
            browser8.requestPage();
            ok(pageCancelFake.fetchCount === 2 && browser8.loading === true && browser8.items.length === 1,
               "browser8 started a deferred next page without dropping existing rows");
            browser8.active = false;
            ok(pageCancelFake.cancelCount === 1 && browser8.loading === false,
               "browser8 canceled the hidden next page");
            browser8.active = true;
            ok(pageCancelFake.fetchCount === 3 && browser8.loading === true && browser8.items.length === 1,
               "browser8 re-issued the canceled next page on reactivation");
            browser8.selectType("comics");
            ok(pageCancelFake.fetchCount === 4 && browser8.currentType === "comics",
               "browser8 switching types fences the active next-page request");
            browser8.selectType("book");
            ok(pageCancelFake.fetchCount === 5 && browser8.currentType === "book" && browser8.loading === true,
               "browser8 restores a type with existing rows and resumes its canceled next page");

            // ── the fix must NOT double-fetch a legitimately settled (empty + exhausted) catalogue ──
            ok(exhaustFake.fetchCount === 1, "browser4 fetched its first type (A) on init: " + exhaustFake.fetchCount);
            browser4.selectType("b");          // fresh B -> one fetch, answers empty + exhausted
            ok(exhaustFake.fetchCount === 2, "fresh type B fetched on switch: " + exhaustFake.fetchCount);
            browser4.selectType("a");          // return to A, which saved {items:[], exhausted:true}
            ok(exhaustFake.fetchCount === 2, "exhausted-empty catalogue NOT re-fetched on return: " + exhaustFake.fetchCount);
            ok(browser4.loading === false && browser4.exhausted === true, "settled-empty return stays settled");

            // ── poster visual profile: classic by default; an injected gallery value is accepted ──
            // (that the value reaches the card delegate is proven statically in
            //  tests/catalogue_polish_scope_test.mjs, which asserts the delegate wiring exists.)
            ok(browser.posterVisualProfile === "classic", "DiscoverBrowser defaults to the classic poster profile");
            browser.posterVisualProfile = "gallery";
            ok(browser.posterVisualProfile === "gallery", "posterVisualProfile is settable to gallery");
            // Task 9: the gallery profile is presentation-only — flipping it must not disturb the
            // shell's loaded data (the Discover/Tankoban wrappers set it with no data/filter change).
            ok(browser.items.length === 1, "gallery profile leaves the shell's loaded data intact");
            browser.posterVisualProfile = "classic";

            // ── reloadCurrent(): a genuine re-fetch of the SAME catalogue/filter, unlike refresh()
            //    (which early-returns when the catalogue is still present in the adapter's list).
            //    This is the seam a preference flip (e.g. Biblio's Explicit Content toggle) uses to
            //    force real new data without resetting the user's catalogue/filter selection. ──
            ok(browser5.items.length === 1 && browser5.items[0].id === "v1", "reloadCurrent setup: initial fetch landed");
            ok(reloadFake.fetchCount === 1, "reloadCurrent setup: exactly one fetch so far");
            // refresh() must stay a no-op here (catalogue unchanged) — the regression this whole
            // function exists to fix: refresh() alone never re-fetches unchanged-catalogue data.
            browser5.refresh();
            ok(reloadFake.fetchCount === 1, "refresh() alone does NOT re-fetch an unchanged catalogue (unchanged behavior)");
            ok(browser5.items[0].id === "v1", "refresh() alone leaves stale data in place (the bug this task fixes)");
            // now the adapter's answer changes (stand-in for a preference flip) and reloadCurrent()
            // must force a real re-fetch that lands the NEW data.
            reloadFake.answerId = "v2";
            var hasReloadCurrent = typeof browser5.reloadCurrent === "function";
            ok(hasReloadCurrent, "DiscoverBrowser exposes a public reloadCurrent()");
            if (hasReloadCurrent) {
                browser5.reloadCurrent();
                ok(reloadFake.fetchCount === 2, "reloadCurrent() issued a genuine new fetch: " + reloadFake.fetchCount);
                ok(browser5.items.length === 1 && browser5.items[0].id === "v2",
                   "reloadCurrent() landed the NEW data, got " + JSON.stringify(browser5.items));
                ok(browser5.currentCatalogKey === "popular", "reloadCurrent() preserves the current catalogue selection");
                ok(browser5.exhausted === true, "reloadCurrent() resets paging state (a short page re-exhausts cleanly)");
            }

            // ── extension removal during paging (Task 9, Part B): a request in flight for an
            //    extension catalogue, followed by that extension's removal BEFORE its reply lands,
            //    must fall back safely and never let the stale reply corrupt the wall. ──
            browser6.selectCatalog("ext1");
            var gExt = extRemoveFake.capturedGen, dExt = extRemoveFake.capturedDone;
            ok(browser6.currentCatalogKey === "ext1" && browser6.loading === true,
               "extension-removal setup: the extension catalogue fetch is in flight");
            extRemoveFake.ext1Present = false;      // the extension is uninstalled mid-fetch
            browser6.refresh();                      // mirrors onExtensionsChanged -> browser.refresh()
            ok(browser6.currentCatalogKey === "popular",
               "extension removal: refresh() falls back to the built-in default, got " + browser6.currentCatalogKey);
            ok(browser6.items.length === 0, "extension removal: the fallback wall starts clean (no stale extension data)");
            dExt(gExt, {items:[{id:"stale-ext",type:"book",title:"STALE EXT",cover:"",year:0,rating:0,format:"",publisher:"",availability:true,explicit:false,raw:{}}],
                        nextCursor:null, exhausted:true, freshness:"", warning:""});
            ok(browser6.currentCatalogKey === "popular",
               "extension removal: the stale in-flight reply does not disturb the fallback catalogue");
            ok(browser6.items.length === 0,
               "extension removal: the fetchGen fence rejects the stale extension reply, no stale item leaks in, got "
               + JSON.stringify(browser6.items));

            // ── fixedGalleryWidth (2026-08-06): pin the gallery delegate to EXACTLY the approved
            //    posterWidth token instead of stretching it to consume residual column width — the
            //    root cause behind Biblio's oversized/blurry cards. Off by default (Theatre/Tankoban
            //    unaffected); a wrapper opts in per-world, mirroring showAuthorAtRest etc. ──
            ok(browser7.fixedGalleryWidth === false, "fixedGalleryWidth defaults off");
            ok(browser7._galleryDelegateWidthForTest !== 148,
               "default (off) still stretches past the 148px token at a wide width, got "
               + browser7._galleryDelegateWidthForTest);
            browser7.fixedGalleryWidth = true;
            ok(browser7._galleryDelegateWidthForTest === 148,
               "fixedGalleryWidth pins the delegate to EXACTLY the posterWidth token, got "
               + browser7._galleryDelegateWidthForTest);
            // residual width becomes centered outer margin, not lost and not extra columns:
            // columnCount must be identical on vs off (same host width feeds both formulas).
            browser7.fixedGalleryWidth = false;
            var colsOff = browser7._galleryColumnCountForTest;
            browser7.fixedGalleryWidth = true;
            ok(colsOff === browser7._galleryColumnCountForTest,
               "fixedGalleryWidth changes card width, not column count: " + colsOff + " vs " + browser7._galleryColumnCountForTest);
            // the classic profile must be entirely untouched by this flag, at any setting.
            browser7.posterVisualProfile = "classic";
            var classicOn = browser7._galleryDelegateWidthForTest;
            browser7.fixedGalleryWidth = false;
            ok(classicOn === browser7._galleryDelegateWidthForTest,
               "fixedGalleryWidth has zero effect on the classic profile: " + classicOn + " vs " + browser7._galleryDelegateWidthForTest);
            browser7.posterVisualProfile = "gallery";   // restore

            if (fails.length) console.log("DISCOVER_BROWSER FAILS:\n  " + fails.join("\n  "));
            else console.log("DISCOVER_BROWSER_OK");
            Qt.exit(fails.length);
        }
    }
}
