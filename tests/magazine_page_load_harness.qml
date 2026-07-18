// Headless load + offline-truth harness for MagazineUniversePage (the Editorial Archive) —
// catches the Loader-loaded page's QML errors, then asserts the page BORN OFFLINE stands
// whole and honest: the four volumes present in fixed order (empty, never vanished), the
// curated era fallbacks and sourced notes reachable, the champions fallback riding the
// curated queries, the manga-only verb surface, and nothing invented (no counts, no index).
// Verdict = exit code.
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

        // the four volumes stand before ANY network answer — empty, in the approved order
        check(p.eras.length === 4, "all four volumes must stand offline, got " + p.eras.length)
        check(p.eras[0].era === "The Founding Years" && p.eras[1].era === "The Golden Age"
              && p.eras[2].era === "The Big Three Era" && p.eras[3].era === "The New Generation",
              "the volumes must keep the approved fixed order")
        check(p.eras[0].items.length === 0, "an offline volume holds nothing — never invented entries")

        // the curated fallback carries the offline page
        check(p.fallbackFor("founding").length >= 3, "founding fallback flagships must be curated")
        check(p.fallbackFor("golden").length >= 4, "golden fallback flagships must be curated")
        check(p.fallbackFor("bigthree").length >= 6, "big-three fallback flagships must be curated")
        check(p.fallbackFor("newgen").length >= 5, "new-generation fallback flagships must be curated")
        check(p.eraNoteFor("founding").length > 0, "each volume must carry its sourced note")
        check(p.eraNoteFor("newgen").length > 0, "each volume must carry its sourced note (newgen)")
        check((p.uni.heroLine || "").length > 0, "the sourced hero line must be present")
        check((p.uni.milestones || []).length === 3, "the print record must carry its three milestones")

        // offline honesty: nothing live, nothing claimed
        check(p.summary === null, "no summary may exist before Jikan answers")
        check(p.champions.length === 0, "no champions may be ranked offline")
        check(p.registryTotal === 0, "no registry total may be invented offline")
        check(p.archive.length === 0, "no archive entries may be invented offline")
        check(p.ixItems.length === 0, "the registry index stays empty offline")
        check((p.uni.readQueries || []).length === 10, "the curated ten stand in for the champions offline")

        // verbs: manga only
        check(typeof p.seriesRequested !== "undefined", "the page must own the manga door")
        check(typeof p.watchRequested === "undefined", "the magazine has no watch verb")

        // spanLine speaks print truthfully
        check(p.spanLine({ fromYear: 1997, publishing: true }) === "since 1997", "spanLine publishing")
        check(p.spanLine({ fromYear: 1984, toYear: 1995, publishing: false }) === "1984–1995", "spanLine finished")
        check(p.spanLine({ fromYear: 0 }) === "serialized in Jump", "spanLine undated")

        console.log(ok ? "PASS" : "FAIL")
        Qt.exit(ok ? 0 : 1)
    }
}
