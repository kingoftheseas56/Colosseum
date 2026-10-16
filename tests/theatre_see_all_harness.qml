// Offscreen contract proof for TheatreSeeAllPage (Theatre Deep Catalogue, Task 7). A fake
// page loader is injected so paging, coalescing, stale-generation, retry, and the extension
// "missing" state are all observable without transport. NEVER throw offscreen: collect fails,
// print the OK marker only when clean, single Qt.exit(fails.length).
import QtQuick
import "../qml" as UI

Item {
    id: h
    width: 900; height: 640

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    property var loaderCalls: []
    property var pendingJobs: []
    property bool autoRespond: true
    property bool rawCursorOnNext: false
    property string mode: "ok"          // ok | error | missing

    function serve(job) {
        var base = typeof job.offset === "object"
                 ? (job.offset.sourceOffset || 0) + (job.offset.index || 0) : job.offset;
        var items = [];
        for (var i = 0; i < 40; i++)
            items.push({ id: "tt" + (base + i), type: "movie",
                         title: "Title " + (base + i), rating: "8." + (i % 10) });
        job.done({ generation: job.gen, items: items, hasMore: base < 400, error: "",
                   nextCursor: h.rawCursorOnNext
                     ? ({sourceOffset: base + 40, index: 0}) : base + 40 });
    }
    function flush() {
        var p = h.pendingJobs; h.pendingJobs = [];
        for (var i = 0; i < p.length; i++) h.serve(p[i]);
    }
    function fakeLoader(pin, offset, limit, options, done) {
        h.loaderCalls.push({ offset: offset, limit: limit, gen: options.generation, key: pin ? pin.rowKey : "" });
        if (h.mode === "missing") { done({ generation: options.generation, items: [], hasMore: false, missing: true, extName: "Netflix" }); return; }
        if (h.mode === "error")   { done({ generation: options.generation, items: [], hasMore: false, error: "Network error" }); return; }
        if (h.mode === "partial-first" || h.mode === "partial-next") {
            var base = typeof offset === "object"
                     ? (offset.sourceOffset || 0) + (offset.index || 0) : offset;
            var prefix = [];
            for (var j = 0; j < 20; j++)
                prefix.push({id: "tt" + (base + j), type: "movie", title: "Title " + (base + j)});
            done({generation: options.generation, items: prefix, hasMore: true,
                  failed: true, retryable: true, error: "HTTP_503",
                  failedFactsIds: ["tt" + (base + 20)]});
            return;
        }
        var job = { done: done, gen: options.generation, offset: offset, pin: pin };
        if (h.autoRespond) h.serve(job); else h.pendingJobs.push(job);
    }

    function findNamed(item, name) {
        if (item.objectName === name) return item;
        var children = item.children || [];
        for (var i = 0; i < children.length; i++) {
            var hit = findNamed(children[i], name);
            if (hit) return hit;
        }
        return null;
    }
    function retryAction(item) {
        if (item.accessibleName !== undefined && item.accessibleName === "Retry") return item;
        var children = item.children || [];
        for (var i = 0; i < children.length; i++) {
            var action = retryAction(children[i]);
            if (action) return action;
        }
        return null;
    }

    UI.TheatreSeeAllPage {
        id: page
        width: 900; height: 640
        pageLoader: h.fakeLoader
    }

    Timer { interval: 60; running: true; repeat: false; onTriggered: h.run() }

    function run() {
        try {
            // ── initial load: offset 0, limit 40, identity preserved ──
            page.pin = ({ pageKey: "movies", sourceKind: "house", rowKey: "top-rated", title: "Top Rated" });
            ok(h.loaderCalls.length === 1, "one initial load, got " + h.loaderCalls.length);
            ok(h.loaderCalls[0].offset === 0 && h.loaderCalls[0].limit === 40, "initial offset 0, limit 40");
            ok(page.items.length === 40, "first page has 40 items, got " + page.items.length);
            ok(page.items[0].id === "tt0", "item identity preserved through the page");
            ok(page.titleText === "Top Rated", "See-all shows the shelf title");

            // ── Theatre See-all renders the approved gallery poster profile ──
            ok(page.posterProfile === "gallery", "Theatre See-all selects the gallery profile, got '" + page.posterProfile + "'");

            // ── next page offset equals the loaded count ──
            page.requestMore();
            ok(h.loaderCalls[h.loaderCalls.length - 1].offset === 40, "next offset == loaded count (40)");
            ok(page.items.length === 80, "second page appended, got " + page.items.length);

            // ── duplicate end-of-grid requests coalesce ──
            h.autoRespond = false;
            var before = h.loaderCalls.length;
            page.requestMore();                     // offset 80, deferred (in-flight)
            page.requestMore();                     // must coalesce — no second call
            ok(h.loaderCalls.length === before + 1, "duplicate end-of-grid requests coalesce");
            h.flush();
            ok(page.items.length === 120, "the coalesced page served exactly once");
            h.autoRespond = true;

            // ── older generation ignored after a pin change ──
            h.autoRespond = false;
            page.requestMore();                     // deferred, OLD generation
            var staleJob = h.pendingJobs.shift();
            page.pin = ({ pageKey: "movies", sourceKind: "house", rowKey: "hidden-gems", title: "Hidden Gems" });
            // the pin change reset + reloaded (deferred). Serve the STALE job first — must be ignored.
            h.serve(staleJob);
            ok(page.titleText === "Hidden Gems", "pin change switched the shelf");
            h.flush();                              // serve the fresh pin's first page
            ok(page.items.length === 40 && page.items[0].id === "tt0",
               "stale-generation response ignored; fresh pin page kept");
            h.autoRespond = true;

            // ── retry repeats the failed offset ──
            page.pin = ({ pageKey: "movies", sourceKind: "house", rowKey: "top-rated", title: "Top Rated" });
            ok(page.items.length === 40, "reset for retry scenario");
            h.mode = "error";
            page.requestMore();                     // offset 40 -> error
            ok(page.errorText.length > 0, "a failed page surfaces an error");
            ok(page.items.length === 40, "a failed page does not drop the loaded rows");
            h.mode = "ok";
            var retryBefore = h.loaderCalls[h.loaderCalls.length - 1].offset;
            page.retry();
            ok(h.loaderCalls[h.loaderCalls.length - 1].offset === retryBefore,
               "retry repeats the SAME failed offset (" + retryBefore + ")");
            ok(page.items.length === 80 && page.errorText === "", "retry recovers and clears the error");

            // ── actual Qt: first-page 20-card validated prefix must expose Retry ──
            h.mode = "partial-first";
            page.pin = ({ pageKey: "movies", sourceKind: "house", rowKey: "top-10", title: "Top" });
            ok(page.items.length === 20 && page.errorText === "HTTP_503",
               "partial first page displays 20 validated cards and exact error");
            var firstRetry = h.retryAction(page);
            ok(firstRetry !== null && firstRetry.visible && firstRetry.enabled && firstRetry.focusEnabled,
               "partial first-page Retry visible and keyboard reachable");
            var retryColumn = h.findNamed(page, "theatreSeeAllRetryColumn");
            var posterWall = h.findNamed(page, "theatreSeeAllPosterWall");
            ok(retryColumn && posterWall && posterWall.y >= retryColumn.y + retryColumn.height,
               "partial retry occupies its own vertical region, never overlays poster cards");
            h.mode = "ok";
            h.rawCursorOnNext = true;
            ok(firstRetry && firstRetry.activate(Qt.ShortcutFocusReason),
               "semantic Retry activation accepted for 20-card prefix");
            h.rawCursorOnNext = false;
            ok(page.items.length === 40 && page.errorText === "",
               "semantic retry completed and replaced partial prefix without duplicates");

            // ── failed later page with raw/ranked cursor preserves both prefixes ──
            h.mode = "partial-next";
            var rawCursor = page.nextCursor;
            ok(typeof rawCursor === "object" && rawCursor.sourceOffset === 40,
               "first full page publishes native-shaped raw cursor");
            page.requestMore();
            ok(page.items.length === 60 && page.items[40].id === "tt40",
               "later error preserves old 40 plus its 20 validated provisional cards");
            ok(page.errorText === "HTTP_503", "later partial error blocks automatic load-more");
            var laterRetry = h.retryAction(page);
            ok(laterRetry && laterRetry.visible && laterRetry.enabled,
               "later-page Retry remains keyboard reachable with 60 cards");
            ok(retryColumn && posterWall && posterWall.y >= retryColumn.y + retryColumn.height,
               "later-page Retry does not obscure the 60 validated cards");
            var attempted = h.loaderCalls.length;
            page.requestMore();
            ok(h.loaderCalls.length === attempted, "error does not silently load another page");
            page.retry(); // a second failed attempt must not duplicate provisional cards
            ok(page.items.length === 60 && page.items[59].id === "tt59",
               "repeated failure does not append provisional prefix twice");
            h.mode = "ok";
            ok(laterRetry && laterRetry.activate(Qt.ShortcutFocusReason),
               "keyboard-semantic action retries same failed raw cursor");
            var lastCall = h.loaderCalls[h.loaderCalls.length - 1];
            ok(lastCall.offset && lastCall.offset.sourceOffset === rawCursor.sourceOffset
               && lastCall.offset.index === rawCursor.index,
               "retry retained exact raw/ranked cursor, not visible count");
            var keys = {}; for (var ai = 0; ai < page.items.length; ai++) keys[page.items[ai].id] = true;
            ok(page.items.length === 80 && Object.keys(keys).length === 80 && !page.errorText,
               "later-page recovery replaces attempted prefix, no skipped or duplicate cards");

            // ── extension missing state names the provider ──
            h.mode = "missing";
            page.pin = ({ pageKey: "movies", sourceKind: "extension", rowKey: "ext:x", title: "Netflix", extName: "Netflix" });
            ok(page.missing === true, "removed extension shows a missing state");
            ok(page.providerName === "Netflix", "missing state names the provider, got '" + page.providerName + "'");
            ok(page.items.length === 0, "missing state shows no stale items");
            h.mode = "ok";

            finish();
        } catch (e) {
            h.fails.push("ASSERT EXCEPTION: " + e);
            finish();
        }
    }

    function finish() {
        if (h.fails.length) console.log("FAILS:\n  " + h.fails.join("\n  "));
        else console.log("THEATRE_SEE_ALL_OK");
        Qt.exit(h.fails.length);
    }
}
