// Headless load + offline-truth harness for MagazineUniversePage (THE LONG RUN) — catches
// the Loader-loaded page's QML errors, then asserts the page BORN OFFLINE stands whole and
// honest: the run chart drawn from the verified AniList flagship pins, the roster riding
// the same pins, the manga-only verb surface, and nothing invented (no ranks, no totals,
// no registry entries). Verdict = exit code.
import QtQuick

Item {
    width: 1280; height: 720
    Component.onCompleted: {
        var c = Qt.createComponent("../qml/MagazineUniversePage.qml")
        if (c.status === Component.Error) { console.log("LOAD ERROR: " + c.errorString()); Qt.exit(2); return }
        var p = c.createObject(this, { universeName: "Weekly Shonen Jump", width: 1280, height: 720 })
        if (!p) { console.log("CREATE FAILED"); Qt.exit(3); return }

        var ok = true
        function check(cond, what) { if (!cond) { ok = false; console.log("FAIL: " + what) } }

        check(p.uni && p.uni.malMagazineId === 83, "the page must pin MAL magazine 83")
        check(p.magId === 83, "magId must ride the pin")

        // the verified flagship pins carry the offline page
        check(p.flagships.length >= 24, "the curated flagships must be present, got " + p.flagships.length)
        var covered = p.flagships.filter(function(f) { return f.cover.indexOf("s4.anilist.co") === 0
                                                              || f.cover.indexOf("https://s4.anilist.co") === 0 })
        check(covered.length === p.flagships.length, "every flagship must carry a baked AniList cover")
        var pinned = p.flagships.filter(function(f) { return f.anilistId > 0 })
        check(pinned.length === p.flagships.length, "every flagship must carry its AniList id pin")

        // the run chart is drawn before ANY network answer, from the pins alone
        check(p.chartRuns.runs.length >= 24, "the chart must draw the flagship runs offline")
        check(p.chartRuns.lanes >= 2, "the runs must pack into lanes")
        var op = p.chartRuns.runs.filter(function(r) { return r.title === "One Piece" })[0]
        check(!!op && op.publishing && op.endFor === p.nowYear,
              "One Piece must run to the chart's right edge")
        var db = p.chartRuns.runs.filter(function(r) { return r.title === "Dragon Ball" })[0]
        check(!!db && db.fromYear === 1984 && db.toYear === 1995,
              "Dragon Ball must ride its verified 1984–1995 run")

        // offline honesty: nothing live, nothing claimed
        check(p.summary === null, "no summary may exist before Jikan answers")
        check(p.champions.length === 0, "no ranking may exist offline")
        check(p.runningNow.length === 0, "no publishing lineup may be invented offline")
        check(p.registryTotal === 0, "no registry total may be invented offline")
        check(p.archive.length === 0, "no registry entries may be invented offline")
        check(p.ixItems.length === 0, "the registry wall stays empty offline")
        check((p.uni.milestones || []).length === 3, "the print milestones must be present")

        // verbs: manga only
        check(typeof p.seriesRequested !== "undefined", "the page must own the manga door")
        check(typeof p.watchRequested === "undefined", "the magazine has no watch verb")

        // spanLine speaks print truthfully
        check(p.spanLine({ fromYear: 1997, publishing: true }) === "1997–", "spanLine publishing")
        check(p.spanLine({ fromYear: 1984, toYear: 1995, publishing: false }) === "1984–1995", "spanLine finished")
        check(p.spanLine({ fromYear: 0 }) === "", "spanLine undated stays silent")

        console.log(ok ? "PASS" : "FAIL")
        Qt.exit(ok ? 0 : 1)
    }
}
