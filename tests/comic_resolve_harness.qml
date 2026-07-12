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

        // ===== 8) download-failure triage (JLU #1 2024 chase) =====
        // JLU #1's only comicfiles mirror (fs2) sits behind a CF managed challenge, its MEGA
        // mirror is mega.nz (no direct-HTTP), pixeldrain is dropped → NO usable source. The
        // downloader emits a stable "no-source" reason for that terminal case; every other
        // reason (network blip, disk, extraction) stays retryable. A terminal reason must NOT
        // surface as "tap to retry" — that retry can never win.
        try {
            check("triage: no-source w/ detail is terminal",
                  Resolve.failureIsTerminal("no-source | all mirrors unavailable (blocked or offline)") === true);
            check("triage: bare no-source is terminal", Resolve.failureIsTerminal("no-source") === true);
            check("triage: no-link post is terminal",
                  Resolve.failureIsTerminal("no-source | no direct download link on this release") === true);
            check("triage: transient HTTP/CF error stays retryable",
                  Resolve.failureIsTerminal("HTTP error: Forbidden (status 403)") === false);
            check("triage: disk space stays retryable",
                  Resolve.failureIsTerminal("insufficient disk space for download + extract") === false);
            check("triage: empty reason not terminal", Resolve.failureIsTerminal("") === false);
            check("triage: cancelled not terminal", Resolve.failureIsTerminal("cancelled by user") === false);
        } catch (e) { fails.push("triage: threw " + e); }

        // ===== 9) slug-first resolve (top-10 empty-series bug, 2026-07-12) =====
        // WP's tags?search is token-OR + count-ordered: for popular titles the exact tag
        // (Absolute Batman, count 27) is flooded out of the returned 20 by giants (Batman,
        // 1417) -> exact-title filter finds nothing -> NO series attached -> zero downloads
        // on every issue. Fix: try the EXACT slug (derived from the LOCG title) first via an
        // injected slugFn; the search lane stays as fallback. Live-proven 7/7 top titles.
        var slugHit = null;
        var slugCalls = 0;
        Resolve.slugFn = function(slug, cb) { slugCalls += 1; cb(slugHit ? (slugHit.forSlug === slug ? slugHit : null) : null); };

        // 9a) THE BUG: search floods (no exact title in hits), slug lane finds the tag -> attaches
        searchHits = [{ id: "batman|24", title: "Batman", cover: "" },
                      { id: "batman-beyond|30", title: "Batman Beyond", cover: "" }];
        slugHit = { forSlug: "absolute-batman", id: "absolute-batman|14355", title: "Absolute Batman" };
        var callsBeforeSlug = searchCalls;
        var r9a = null;
        Resolve.resolve({ id: "locg:ab", title: "Absolute Batman", startYear: 2024 }, function(res) { r9a = res; });
        check("slug: flooded search bypassed, exact slug attaches",
              r9a && r9a.attached === true && r9a.sourceId === "absolute-batman|14355");
        check("slug: search lane not consulted on slug hit", searchCalls === callsBeforeSlug);

        // 9b) slug attach persists like any attach — second resolve is cached, no calls
        var slugCallsAfter = slugCalls;
        var r9b = null;
        Resolve.resolve({ id: "locg:ab", title: "Absolute Batman", startYear: 2024 }, function(res) { r9b = res; });
        check("slug: attach persisted", r9b && r9b.attached === true && slugCalls === slugCallsAfter && searchCalls === callsBeforeSlug);

        // 9c) slug MISS falls back to the search lane (existing exact-match behavior intact)
        slugHit = null;
        searchHits = [{ id: "new-thing-2|1800", title: "New Thing 2", cover: "" }];
        var r9c = null;
        Resolve.resolve({ id: "locg:nt2", title: "New Thing 2", startYear: 2025 }, function(res) { r9c = res; });
        check("slug: miss falls back to search attach", r9c && r9c.attached === true && r9c.sourceId === "new-thing-2|1800");

        // 9d) slug hit with a CLASHING year is rejected (wrong volume) -> falls to search -> honest miss
        slugHit = { forSlug: "daredevil", id: "daredevil|55", title: "Daredevil (1964)" };
        searchHits = [];
        var r9d = null;
        Resolve.resolve({ id: "locg:dd9", title: "Daredevil", startYear: 2019 }, function(res) { r9d = res; });
        check("slug: clashing-year hit refused", r9d && r9d.attached === false);
        check("slug: refused hit never persisted", typeof saved["map/locg:dd9"] === "undefined");

        // 9e) slug derivation: punctuation/apostrophes/year suffixes -> GetComics slug shape
        slugHit = { forSlug: "ms-marvel", id: "ms-marvel|1300", title: "Ms. Marvel" };
        var r9e = null;
        Resolve.resolve({ id: "locg:mm9", title: "Ms. Marvel (2014)", startYear: 2014 }, function(res) { r9e = res; });
        check("slug: derived from normalized title", r9e && r9e.attached === true && r9e.sourceId === "ms-marvel|1300");

        if (fails.length) { console.error("FAILS: " + fails.join(" | ")); Qt.exit(1); return; }
        Qt.exit(0);
    }
}
