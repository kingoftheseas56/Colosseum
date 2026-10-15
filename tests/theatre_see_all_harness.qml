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
    property string mode: "ok"          // ok | error | missing

    function serve(job) {
        var items = [];
        for (var i = 0; i < 40; i++)
            items.push({ id: "tt" + (job.offset + i), type: "movie",
                         title: "Title " + (job.offset + i), rating: "8." + (i % 10) });
        job.done({ generation: job.gen, items: items, hasMore: job.offset < 400, error: "" });
    }
    function flush() {
        var p = h.pendingJobs; h.pendingJobs = [];
        for (var i = 0; i < p.length; i++) h.serve(p[i]);
    }
    function fakeLoader(pin, offset, limit, options, done) {
        h.loaderCalls.push({ offset: offset, limit: limit, gen: options.generation, key: pin ? pin.rowKey : "" });
        if (h.mode === "missing") { done({ generation: options.generation, items: [], hasMore: false, missing: true, extName: "Netflix" }); return; }
        if (h.mode === "error")   { done({ generation: options.generation, items: [], hasMore: false, error: "Network error" }); return; }
        var job = { done: done, gen: options.generation, offset: offset, pin: pin };
        if (h.autoRespond) h.serve(job); else h.pendingJobs.push(job);
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
