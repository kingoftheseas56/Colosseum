import QtQuick
import "../qml/MangaChapterGrouping.js" as Grouping

Item {
    id: root

    function fail(message) {
        console.error("MANGA_CHAPTER_GROUPING_FAIL: " + message)
        Qt.exit(1)
    }

    function check(condition, message) {
        if (!condition) fail(message)
    }

    function chapter(n, suffix) {
        return {
            id: "ch-" + String(n) + (suffix || ""),
            seriesId: "wc-series",
            canonicalSeriesId: "mal:1",
            number: n,
            label: "Chapter " + String(n),
            rawOrder: n
        }
    }

    function rangeRecord(volumes, looseStatus, looseNumbers) {
        return {
            source: "arc1",
            canonicalSeriesId: "mal:1",
            canonicalVolumeCountKnown: true,
            matchesCanonicalVolumeCount: true,
            looseStatus: looseStatus || "verified_none",
            looseChapterNumbers: looseNumbers || [],
            volumes: volumes
        }
    }

    Component.onCompleted: {
        var chapters = []
        for (var i = 1; i <= 25; ++i) chapters.push(chapter(i))
        var generic = Grouping.group(chapters, ({}), "mal:1")
        check(generic.mode === "generic", "no range record must use generic mode")
        check(generic.groups.length === 3, "25 chapters must make 3 generic groups")
        check(generic.groups[0].windows[0].chapters.length === 10, "generic group 1 must contain 10")
        check(generic.groups[1].windows[0].chapters.length === 10, "generic group 2 must contain 10")
        check(generic.groups[2].windows[0].chapters.length === 5, "generic group 3 must contain 5")
        check(generic.groups[0].windows[0].chapters[0].id === "ch-1", "grouping must preserve source order")
        check(generic.groups[2].windows[0].chapters[4].id === "ch-25", "grouping must preserve final item")

        var exactRanges = rangeRecord([
            { number: "1", rangeState: "known", chapterStart: "1", chapterEnd: "12", rangeStatus: "verified" },
            { number: "2", rangeState: "known", chapterStart: "13", chapterEnd: "25", rangeStatus: "verified" }
        ])
        var exact = Grouping.group(chapters, exactRanges, "mal:1")
        check(exact.mode === "exact-volume", "eligible ranges must prefer exact volumes")
        check(exact.groups.length === 2, "exact ranges must make two volume groups")
        check(exact.groups[0].label === "Volume 1", "first exact group label")
        check(exact.groups[0].windows.length === 2, "12-chapter volume must have two windows")
        check(exact.groups[0].windows[0].chapters.length === 10, "large exact volume first window budget")
        check(exact.groups[0].windows[1].chapters.length === 2, "large exact volume remainder")

        var conflicted = rangeRecord([
            { number: "1", rangeState: "conflict", chapterStart: "1", chapterEnd: "12", rangeStatus: "conflict" }
        ])
        var fallback = Grouping.group(chapters, conflicted, "mal:1")
        check(fallback.mode === "generic", "conflicting ranges must fall back")
        check(fallback.reason === "range-conflict", "conflict fallback reason must be observable")

        var looseChapters = []
        for (var j = 1; j <= 22; ++j) looseChapters.push(chapter(j))
        var withLoose = Grouping.group(looseChapters, rangeRecord([
            { number: "1", rangeState: "known", chapterStart: "1", chapterEnd: "10", rangeStatus: "verified" },
            { number: "2", rangeState: "known", chapterStart: "11", chapterEnd: "20", rangeStatus: "verified" }
        ], "verified_cross_checked", ["21", "22"]), "mal:1")
        check(withLoose.mode === "exact-volume", "verified loose tail can coexist with exact volumes")
        check(withLoose.groups.length === 3, "verified loose tail must become an honest generic group")
        check(withLoose.groups[2].kind === "loose", "loose group must be marked")
        check(withLoose.groups[2].windows[0].chapters.length === 2, "loose tail count")

        var mismatch = Grouping.group(chapters, exactRanges, "mal:999")
        check(mismatch.mode === "generic", "identity mismatch must fall back")
        check(mismatch.reason === "identity-mismatch", "identity mismatch reason")

        console.log("MANGA_CHAPTER_GROUPING_OK")
        Qt.exit(0)
    }
}
