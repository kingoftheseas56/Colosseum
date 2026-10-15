// Headless behavioral harness for LocgApi.js parsers, fed by captured fixtures.
// Verdict rides the EXIT CODE — Qt.exit(0) pass, non-zero fail (console may not flush;
// an uncaught onCompleted throw HANGS qml.exe, so everything is try/catch → Qt.exit).
// Fixtures arrive via locg_fixtures.gen.js — a .pragma-library file the PowerShell test
// GENERATES from the real captured .json (qml.exe's file:// XHR returns empty offscreen,
// so runtime file reads are impossible here). Run this ONLY via tests/test_locg_api_p0.ps1.
import QtQuick

import "../qml/LocgApi.js" as Locg
import "locg_fixtures.gen.js" as Fx

QtObject {
    id: t

    function fixture(name) {
        var text = Fx.get(name)
        if (!text || text.length < 500) { console.log("FIXTURE EMPTY: " + name); Qt.exit(2) }
        return text
    }
    function ok(cond, msg, fails) { if (!cond) fails.push(msg) }

    Component.onCompleted: {
        var fails = []
        try {
            // --- search.json: series/search shape ---
            var searchJson = JSON.parse(fixture("search.json"))
            var s = Locg.parseSeriesList(searchJson.list)
            t.ok(s.length > 0, "search: expected >0 series, got " + s.length, fails)
            if (s.length > 0) {
                t.ok(s[0].id === "locg:148319", "search: item[0].id, got " + s[0].id, fails)
                t.ok(s[0].title === "Batman / Daredevil", "search: item[0].title, got " + s[0].title, fails)
                t.ok(s[0].publisher === "DC Comics", "search: item[0].publisher, got " + s[0].publisher, fails)
                t.ok(s[0].cover.indexOf("medium-6855190") >= 0, "search: item[0].cover, got " + s[0].cover, fails)
            }

            // --- releases.json: releases/issue shape ---
            var releasesJson = JSON.parse(fixture("releases.json"))
            var r = Locg.parseReleases(releasesJson.list)
            t.ok(r.length > 0, "releases: expected >0 items, got " + r.length, fails)
            if (r.length > 0) {
                t.ok(r[0].pulls === 60672, "releases: item[0].pulls, got " + r[0].pulls, fails)
                t.ok(r[0].rating === 98, "releases: item[0].rating, got " + r[0].rating, fails)
                t.ok(r[0].id === "locg:6863939", "releases: item[0].id, got " + r[0].id, fails)
                t.ok(r[0].title === "Absolute Batman #22", "releases: item[0].title, got " + r[0].title, fails)
                t.ok(r[0].publisher === "DC Comics", "releases: item[0].publisher, got " + r[0].publisher, fails)
            }

            // --- series.json: structured series-detail object ---
            var seriesJson = JSON.parse(fixture("series.json"))
            var d = Locg.parseSeriesDetail(seriesJson)
            t.ok(d.issues.length > 0, "series detail: expected >0 issues, got " + d.issues.length, fails)
            t.ok(typeof d.title === "string" && d.title.length > 0, "series detail: title missing, got " + d.title, fails)

            // --- popular.json: same series/search shape as search.json ---
            var popularJson = JSON.parse(fixture("popular.json"))
            var p = Locg.parseSeriesList(popularJson.list)
            t.ok(p.length > 0, "popular: expected >0 series, got " + p.length, fails)

            // --- garbage tolerance: never throw, always empty-shaped ---
            t.ok(Locg.parseSeriesList("<html>nope</html>").length === 0, "garbage: parseSeriesList should return []", fails)
            t.ok(Locg.parseReleases("<html>nope</html>").length === 0, "garbage: parseReleases should return []", fails)
            var gd = Locg.parseSeriesDetail("<html>nope</html>")
            t.ok(gd && gd.issues.length === 0, "garbage: parseSeriesDetail should return empty-shaped object", fails)

            // ── verbs against injected fake network (NO live calls in the suite, ever) ──
            Locg._testLog = [];
            var served = {};
            Locg.fetchFn = function(url, cb) { Locg._testLog.push(url); cb(served[url] !== undefined ? served[url] : null); };
            Locg.delayFn = function(ms, cb) { cb(); };
            var searchU = Locg.searchUrl("daredevil");
            served[searchU] = Fx.get("search.json");
            var got = null, gotMeta = null;
            Locg.searchSeries("daredevil", function(list, meta) { got = list; gotMeta = meta; });
            t.ok(got && got.length > 0, "verb: search parses", fails);
            t.ok(gotMeta && gotMeta.ok === true, "verb: meta ok", fails);
            var callsBefore = Locg._testLog.length;
            Locg.searchSeries("daredevil", function(list) { got = list; });
            t.ok(Locg._testLog.length === callsBefore && got.length > 0, "verb: cached (no refetch)", fails);
            served[Locg.searchUrl("broken")] = "<!DOCTYPE html>Just a moment...";
            var bMeta = null;
            Locg.searchSeries("broken", function(list, meta) {
                bMeta = meta;
                t.ok(list.length === 0, "verb: garbage empty", fails);
            });
            t.ok(bMeta && bMeta.ok === false && bMeta.blocked === true, "verb: garbage blocked meta", fails);
            served[Locg.releasesUrl()] = Fx.get("releases.json");
            var top = null;
            Locg.topInComics(function(list, meta) { top = list; });
            t.ok(top && top.length > 0 && top.length <= 10, "verb: topInComics max 10", fails);
            t.ok(top.length < 2 || top[0].pulls >= top[1].pulls, "verb: topInComics pulls-desc", fails);
            var stripOk = true, idOk = true;
            for (var ti = 0; ti < top.length; ti++) {
                if (/#\d/.test(top[ti].title)) stripOk = false;     // issue "#N" suffix must be stripped to series
                if (String(top[ti].id).indexOf("locg:t/") !== 0) idOk = false;
            }
            t.ok(stripOk, "verb: topInComics strips issue #N suffix", fails);
            t.ok(idOk, "verb: topInComics ids are locg:t/ aggregate keys", fails);

            // ── publisher axis ──
            var boxes = null;
            Locg.publisherBoxes(function(b) { boxes = b; });
            t.ok(boxes && boxes.length >= 5, "pub: boxes exist", fails);
            t.ok(boxes[0].id.length > 0 && boxes[0].label.length > 0 && boxes[0].kind === "publisher", "pub: box shape", fails);
            served[Locg.publisherUrl("2", 1)] = Fx.get("popular.json");   // same series/search shape
            var pi = null;
            Locg.publisherItems("2", 1, function(r, meta) { pi = r; });
            t.ok(pi && pi.items.length > 0, "pub: items parse", fails);
            t.ok(typeof pi.hasMore === "boolean", "pub: hasMore flag", fails);

            if (fails.length > 0) {
                console.error("FAILS: " + fails.join(" | "))
                Qt.exit(1)
            } else {
                console.log("LOCG API HARNESS PASS")
                Qt.exit(0)
            }
        } catch (e) {
            console.error("FAILS: THROW: " + e)
            Qt.exit(1)
        }
    }
}
