import QtQuick
import "../qml/ComicResolve.js" as Resolve

Item {
    Component.onCompleted: {
        var fails = [];
        function check(name, cond) { if (!cond) fails.push(name); }
        var saved = {};
        Resolve.store = { get: function(k) { return saved[k] || ""; }, set: function(k, v) { saved[k] = v; } };
        var searchHits = [];
        var searchCalls = 0;
        Resolve.searchFn = function(q, cb) { searchCalls += 1; cb(searchHits, { ok: true, blocked: false }); };

        // 1) exact title+year match attaches
        searchHits = [{ id: "daredevil-2019|1125", title: "Daredevil (2019)", cover: "" }];
        var r1 = null;
        Resolve.resolve({ id: "locg:150065", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r1 = res; });
        check("match: attached", r1 && r1.attached === true && r1.sourceId === "daredevil-2019|1125");
        // 2) mapping persisted — second resolve must NOT re-search
        var r2 = null;
        Resolve.resolve({ id: "locg:150065", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r2 = res; });
        check("persist: cached attach", r2.attached === true && searchCalls === 1);
        // 4) same title, wrong decade -> refused (both sides have years, far apart; year gate +/-1)
        searchHits = [{ id: "daredevil-1964|900", title: "Daredevil (1964)", cover: "" }];
        var r4 = null;
        Resolve.resolve({ id: "locg:10", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r4 = res; });
        check("yeargate: refused", r4.attached === false);
        // 5) normalization: punctuation/case must not break the match
        searchHits = [{ id: "ms-marvel-2014|1300", title: "Ms. Marvel (2014)", cover: "" }];
        var r5 = null;
        Resolve.resolve({ id: "locg:11", title: "MS MARVEL (2014)", startYear: 2014 }, function(res) { r5 = res; });
        check("normalize: matched", r5.attached === true);

        // 3a) THE JLU BUG: LOCG has a startYear, the search hit has NO year -> a clean title match MUST attach
        searchHits = [{ id: "justice-league-unlimited|1400", title: "Justice League Unlimited", cover: "" }];
        var rJlu = null;
        Resolve.resolve({ id: "locg:jlu", title: "Justice League Unlimited", startYear: 2025 }, function(res) { rJlu = res; });
        check("jlu: yearless hit attaches", rJlu && rJlu.attached === true && rJlu.sourceId === "justice-league-unlimited|1400");

        // 3b) MISS IS SESSION-ONLY: a miss then a matching hit for the SAME id -> still miss in-session
        //     (session cache holds; but the injected store is NEVER written for that key -> no persistent poison)
        searchHits = [];
        var rMiss1 = null;
        Resolve.resolve({ id: "locg:miss", title: "Ghost Series (1988)", startYear: 1988 }, function(res) { rMiss1 = res; });
        check("miss: honest no-match", rMiss1 && rMiss1.attached === false && !rMiss1.sourceId);
        var callsAfterMiss = searchCalls;
        var storeWrittenForMiss = (typeof saved["map/locg:miss"] !== "undefined");
        searchHits = [{ id: "ghost-series|1500", title: "Ghost Series", cover: "" }];   // catalog "grew" — but session miss holds
        var rMiss2 = null;
        Resolve.resolve({ id: "locg:miss", title: "Ghost Series (1988)", startYear: 1988 }, function(res) { rMiss2 = res; });
        check("miss: session cache holds (no re-search)", rMiss2 && rMiss2.attached === false && searchCalls === callsAfterMiss);
        check("miss: never persisted to store", !storeWrittenForMiss && typeof saved["map/locg:miss"] === "undefined");

        // 3c) BRACKET-STRIP: search hit "Batman '66 [I]" vs LOCG "Batman '66" -> attaches
        searchHits = [{ id: "batman-66|1600", title: "Batman '66 [I]", cover: "" }];
        var rBrk = null;
        Resolve.resolve({ id: "locg:b66", title: "Batman '66", startYear: 2013 }, function(res) { rBrk = res; });
        check("bracket: strip [I] attaches", rBrk && rBrk.attached === true && rBrk.sourceId === "batman-66|1600");

        // 3d) EXACT-YEAR DISAMBIGUATION: two dated hits, LOCG 2019 -> attaches to the 2019 one
        searchHits = [{ id: "daredevil-2019|1125", title: "Daredevil (2019)", cover: "" },
                      { id: "daredevil-1998|700", title: "Daredevil (1998)", cover: "" }];
        var rDis = null;
        Resolve.resolve({ id: "locg:dd-dis", title: "Daredevil (2019)", startYear: 2019 }, function(res) { rDis = res; });
        check("disambig: exact-year wins", rDis && rDis.attached === true && rDis.sourceId === "daredevil-2019|1125");

        // 6) a blocked source (cooldown) must NOT attach, NOT persist, and NOT poison _miss:
        //    a later un-blocked resolve of the SAME id MUST re-search and attach.
        var savedBefore = JSON.stringify(saved);
        Resolve.searchFn = function(q, cb) { cb([], { ok: false, blocked: true }); };
        var r6 = null;
        Resolve.resolve({ id: "locg:12", title: "New Thing", startYear: 2025 }, function(res) { r6 = res; });
        check("blocked: not attached", r6.attached === false);
        check("blocked: not persisted", JSON.stringify(saved) === savedBefore);
        var callsBeforeUnblock = searchCalls;
        searchHits = [{ id: "new-thing|1700", title: "New Thing", cover: "" }];
        Resolve.searchFn = function(q, cb) { searchCalls += 1; cb(searchHits, { ok: true, blocked: false }); };
        var r6b = null;
        Resolve.resolve({ id: "locg:12", title: "New Thing", startYear: 2025 }, function(res) { r6b = res; });
        check("blocked: un-block re-searches + attaches", r6b && r6b.attached === true && searchCalls === callsBeforeUnblock + 1 && r6b.sourceId === "new-thing|1700");

        // ===== matchIssues: LOCG issue rows ↔ GC posts =====
        function gcPost(id, name, coll, date) { return { id: id, name: name, url: "u", sizeMB: 10, date: date || "2024-01-01", collection: !!coll }; }
        function locgIss(id, title) { return { id: id, title: title }; }

        // 7a) basic: numbered post matches numbered row; collection splits out
        var m1 = Resolve.matchIssues(
            [locgIss("locg:1", "Saga #43"), locgIss("locg:2", "Saga #44")],
            [gcPost("p1", "Saga #43 (2017)"), gcPost("p2", "Saga Vol. 1 (TPB)", true)]);
        check("mi: #43 matched", m1.byIssue["locg:1"] && m1.byIssue["locg:1"].id === "p1");
        check("mi: #44 unmatched", !m1.byIssue["locg:2"]);
        check("mi: collection split", m1.collections.length === 1 && m1.collections[0].id === "p2");

        // 7b) zero-padding: "#01" ≡ "#1"
        var m2 = Resolve.matchIssues([locgIss("locg:3", "Daredevil #1")], [gcPost("p3", "Daredevil #01 (2019)")]);
        check("mi: #01 == #1", m2.byIssue["locg:3"] && m2.byIssue["locg:3"].id === "p3");

        // 7c) decimals exact: "#43.1" never matches "#43"
        var m3 = Resolve.matchIssues([locgIss("locg:4", "Batman #43")], [gcPost("p4", "Batman #43.1")]);
        check("mi: decimal refused", !m3.byIssue["locg:4"]);

        // 7d) base mismatch: an Annual never matches a plain numbered row
        var m4 = Resolve.matchIssues([locgIss("locg:5", "Saga #1")], [gcPost("p5", "Saga Annual #1")]);
        check("mi: annual excluded", !m4.byIssue["locg:5"]);

        // 7e) duplicate posts for the SAME issue → newest wins (same comic, not a wrong-attach risk)
        var m5 = Resolve.matchIssues([locgIss("locg:6", "Saga #43")],
            [gcPost("p6", "Saga #43", false, "2022-05-01"), gcPost("p7", "Saga #43", false, "2024-06-01")]);
        check("mi: newest duplicate wins", m5.byIssue["locg:6"] && m5.byIssue["locg:6"].id === "p7");

        // 7f) no #N in the post name → never matched into byIssue
        var m6 = Resolve.matchIssues([locgIss("locg:7", "Saga #43")], [gcPost("p8", "Saga Complete Run")]);
        check("mi: unnumbered post skipped", !m6.byIssue["locg:7"]);

        if (fails.length) { console.error("FAILS: " + fails.join(" | ")); Qt.exit(1); return; }
        Qt.exit(0);
    }
}
