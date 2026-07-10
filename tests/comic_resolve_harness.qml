import QtQuick
import "../qml/ComicResolve.js" as Resolve

Item {
    Component.onCompleted: {
        var fails = [];
        function check(name, cond) { if (!cond) fails.push(name); }
        var saved = {};
        Resolve.store = { get: function(k) { return saved[k] || ""; }, set: function(k, v) { saved[k] = v; } };
        var xoxoHits = [];
        var xoxoCalls = 0;
        Resolve.searchFn = function(q, cb) { xoxoCalls += 1; cb(xoxoHits, { ok: true, blocked: false }); };

        // 1) exact title+year match attaches
        xoxoHits = [{ id: "xoxo:daredevil-2019", title: "Daredevil (2019)", cover: "" }];
        var r1 = null;
        Resolve.resolve({ id: "locg:150065", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r1 = res; });
        check("match: attached", r1 && r1.attached === true && r1.sourceId === "xoxo:daredevil-2019");
        // 2) mapping persisted — second resolve must NOT re-search
        var r2 = null;
        Resolve.resolve({ id: "locg:150065", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r2 = res; });
        check("persist: cached attach", r2.attached === true && xoxoCalls === 1);
        // 4) same title, wrong decade -> refused (both sides have years, far apart; year gate +/-1)
        xoxoHits = [{ id: "xoxo:daredevil-1964", title: "Daredevil (1964)", cover: "" }];
        var r4 = null;
        Resolve.resolve({ id: "locg:10", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r4 = res; });
        check("yeargate: refused", r4.attached === false);
        // 5) normalization: punctuation/case must not break the match
        xoxoHits = [{ id: "xoxo:ms-marvel-2014", title: "Ms. Marvel (2014)", cover: "" }];
        var r5 = null;
        Resolve.resolve({ id: "locg:11", title: "MS MARVEL (2014)", startYear: 2014 }, function(res) { r5 = res; });
        check("normalize: matched", r5.attached === true);

        // 3a) THE JLU BUG: LOCG has a startYear, xoxo hit has NO year -> a clean title match MUST attach
        xoxoHits = [{ id: "xoxo:justice-league-unlimited", title: "Justice League Unlimited", cover: "" }];
        var rJlu = null;
        Resolve.resolve({ id: "locg:jlu", title: "Justice League Unlimited", startYear: 2025 }, function(res) { rJlu = res; });
        check("jlu: yearless xoxo hit attaches", rJlu && rJlu.attached === true && rJlu.sourceId === "xoxo:justice-league-unlimited");

        // 3b) MISS IS SESSION-ONLY: a miss then a matching hit for the SAME id -> still miss in-session
        //     (session cache holds; but the injected store is NEVER written for that key -> no persistent poison)
        xoxoHits = [];
        var rMiss1 = null;
        Resolve.resolve({ id: "locg:miss", title: "Ghost Series (1988)", startYear: 1988 }, function(res) { rMiss1 = res; });
        check("miss: honest no-match", rMiss1 && rMiss1.attached === false && !rMiss1.sourceId);
        var callsAfterMiss = xoxoCalls;
        var storeWrittenForMiss = (typeof saved["map/locg:miss"] !== "undefined");
        xoxoHits = [{ id: "xoxo:ghost-series", title: "Ghost Series", cover: "" }];   // catalog "grew" — but session miss holds
        var rMiss2 = null;
        Resolve.resolve({ id: "locg:miss", title: "Ghost Series (1988)", startYear: 1988 }, function(res) { rMiss2 = res; });
        check("miss: session cache holds (no re-search)", rMiss2 && rMiss2.attached === false && xoxoCalls === callsAfterMiss);
        check("miss: never persisted to store", !storeWrittenForMiss && typeof saved["map/locg:miss"] === "undefined");

        // 3c) BRACKET-STRIP: xoxo "Batman '66 [I]" vs LOCG "Batman '66" -> attaches
        xoxoHits = [{ id: "xoxo:batman-66", title: "Batman '66 [I]", cover: "" }];
        var rBrk = null;
        Resolve.resolve({ id: "locg:b66", title: "Batman '66", startYear: 2013 }, function(res) { rBrk = res; });
        check("bracket: strip [I] attaches", rBrk && rBrk.attached === true && rBrk.sourceId === "xoxo:batman-66");

        // 3d) EXACT-YEAR DISAMBIGUATION: two dated hits, LOCG 2019 -> attaches to the 2019 one
        xoxoHits = [{ id: "xoxo:daredevil-2019", title: "Daredevil (2019)", cover: "" },
                    { id: "xoxo:daredevil-1998", title: "Daredevil (1998)", cover: "" }];
        var rDis = null;
        Resolve.resolve({ id: "locg:dd-dis", title: "Daredevil (2019)", startYear: 2019 }, function(res) { rDis = res; });
        check("disambig: exact-year wins", rDis && rDis.attached === true && rDis.sourceId === "xoxo:daredevil-2019");

        // 6) blocked xoxo (cooldown) must NOT attach, NOT persist, and NOT poison _miss:
        //    a later un-blocked resolve of the SAME id MUST re-search and attach.
        var savedBefore = JSON.stringify(saved);
        Resolve.searchFn = function(q, cb) { cb([], { ok: false, blocked: true }); };
        var r6 = null;
        Resolve.resolve({ id: "locg:12", title: "New Thing", startYear: 2025 }, function(res) { r6 = res; });
        check("blocked: not attached", r6.attached === false);
        check("blocked: not persisted", JSON.stringify(saved) === savedBefore);
        var callsBeforeUnblock = xoxoCalls;
        xoxoHits = [{ id: "xoxo:new-thing", title: "New Thing", cover: "" }];
        Resolve.searchFn = function(q, cb) { xoxoCalls += 1; cb(xoxoHits, { ok: true, blocked: false }); };
        var r6b = null;
        Resolve.resolve({ id: "locg:12", title: "New Thing", startYear: 2025 }, function(res) { r6b = res; });
        check("blocked: un-block re-searches + attaches", r6b && r6b.attached === true && xoxoCalls === callsBeforeUnblock + 1 && r6b.sourceId === "xoxo:new-thing");

        if (fails.length) { console.error("FAILS: " + fails.join(" | ")); Qt.exit(1); return; }
        Qt.exit(0);
    }
}
