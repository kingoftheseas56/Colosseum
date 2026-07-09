// Headless behavioral harness for XoxoApi.js parsers, fed by captured fixtures.
// Verdict rides the EXIT CODE — Qt.exit(0) pass, non-zero fail (console may not flush;
// an uncaught onCompleted throw HANGS qml.exe, so everything is try/catch → Qt.exit).
// Fixtures arrive via xoxo_fixtures.gen.js — a .pragma-library file the PowerShell test
// GENERATES from the real captured .html (qml.exe's file:// XHR returns empty offscreen,
// so runtime file reads are impossible here). Run this ONLY via tests/test_xoxo_api_p0.ps1.
import QtQuick

import "../qml/XoxoApi.js" as Xoxo
import "xoxo_fixtures.gen.js" as Fx

QtObject {
    id: t

    function fixture(name) {
        var text = Fx.get(name)
        if (!text || text.length < 1000) { console.log("FIXTURE EMPTY: " + name); Qt.exit(2) }
        return text
    }
    function ok(cond, msg) { if (!cond) { console.log("FAIL: " + msg); Qt.exit(1) } }

    Component.onCompleted: {
        try {
            var s = Xoxo.parseSeriesList(fixture("search.html"))
            t.ok(s.length >= 20, "search: expected >=20 series, got " + s.length)
            t.ok(s.map(function(i) { return i.id }).indexOf("xoxo:batman-1940") >= 0, "search: batman-1940 missing")
            t.ok(s.every(function(i) { return i.title.length > 0 }), "search: item without title")
            t.ok(s.some(function(i) { return i.cover.indexOf("http") === 0 }), "search: no covers found at all")

            var r1 = Xoxo.parseIssueList(fixture("series_p1.html"), "batman-1940")
            t.ok(r1.issues.length >= 40, "series p1: expected >=40 issues, got " + r1.issues.length)
            t.ok(r1.issues[0].issueId.indexOf("xoxo:batman-1940/") === 0, "series p1: bad issueId " + r1.issues[0].issueId)
            t.ok(r1.nextUrl.length > 0, "series p1: rel=next not found")

            var r2 = Xoxo.parseIssueList(fixture("series_p2.html"), "batman-1940")
            t.ok(r2.issues.length >= 40, "series p2: expected >=40 issues, got " + r2.issues.length)

            var urls = Xoxo.parsePages(fixture("issue_all.html"))
            t.ok(urls.length >= 60, "pages: expected >=60 urls, got " + urls.length)
            t.ok(urls[0].indexOf("http") === 0, "pages: url[0] not absolute: " + urls[0])
            t.ok(urls[0].indexOf("/1.jpg") > 0, "pages: url[0] should be page 1: " + urls[0])

            var g = Xoxo.parseSeriesList(fixture("genre.html"))
            t.ok(g.length >= 20, "genre: expected >=20 series, got " + g.length)

            console.log("XOXO API HARNESS PASS")
            Qt.exit(0)
        } catch (e) { console.log("THROW: " + e); Qt.exit(1) }
    }
}
