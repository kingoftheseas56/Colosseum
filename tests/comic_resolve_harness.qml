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
        Resolve.xoxoSearchFn = function(q, cb) { xoxoCalls += 1; cb(xoxoHits, { ok: true, blocked: false }); };

        // 1) exact title+year match attaches
        xoxoHits = [{ id: "xoxo:daredevil-2019", title: "Daredevil (2019)", cover: "" }];
        var r1 = null;
        Resolve.resolve({ id: "locg:150065", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r1 = res; });
        check("match: attached", r1 && r1.attached === true && r1.xoxoId === "xoxo:daredevil-2019");
        // 2) mapping persisted — second resolve must NOT re-search
        var r2 = null;
        Resolve.resolve({ id: "locg:150065", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r2 = res; });
        check("persist: cached attach", r2.attached === true && xoxoCalls === 1);
        // 3) no hit -> honest no-match
        xoxoHits = [];
        var r3 = null;
        Resolve.resolve({ id: "locg:9", title: "Obscure Mini (1988)", startYear: 1988 }, function(res) { r3 = res; });
        check("nomatch: honest", r3.attached === false && !r3.xoxoId);
        // 4) same title, wrong decade -> refused (year gate +/-1)
        xoxoHits = [{ id: "xoxo:daredevil-1964", title: "Daredevil (1964)", cover: "" }];
        var r4 = null;
        Resolve.resolve({ id: "locg:10", title: "Daredevil (2019)", startYear: 2019 }, function(res) { r4 = res; });
        check("yeargate: refused", r4.attached === false);
        // 5) normalization: punctuation/case must not break the match
        xoxoHits = [{ id: "xoxo:ms-marvel-2014", title: "Ms. Marvel (2014)", cover: "" }];
        var r5 = null;
        Resolve.resolve({ id: "locg:11", title: "MS MARVEL (2014)", startYear: 2014 }, function(res) { r5 = res; });
        check("normalize: matched", r5.attached === true);
        // 6) blocked xoxo (cooldown) must NOT be persisted as no-match
        var savedBefore = JSON.stringify(saved);
        Resolve.xoxoSearchFn = function(q, cb) { cb([], { ok: false, blocked: true }); };
        var r6 = null;
        Resolve.resolve({ id: "locg:12", title: "New Thing (2025)", startYear: 2025 }, function(res) { r6 = res; });
        check("blocked: not attached", r6.attached === false);
        check("blocked: not persisted", JSON.stringify(saved) === savedBefore);

        if (fails.length) { console.error("FAILS: " + fails.join(" | ")); Qt.exit(1); return; }
        Qt.exit(0);
    }
}
