// Headless behavioral harness for the pure EpisodeBrowser.js store (Feature 8).
// Verdict rides the EXIT CODE — Qt.exit(0) pass, non-zero fail — because console output
// is not guaranteed to flush and an uncaught onCompleted throw HANGS qml.exe.
import QtQuick
import "../qml/EpisodeBrowser.js" as EB

QtObject {
    Component.onCompleted: {
        try {
            runChecks()
            Qt.exit(0)
        } catch (e) {
            console.log("HARNESS FAIL: " + e.message)
            Qt.exit(2)
        }
    }

    function runChecks() {
        // --- seriesRootId: tt ids take the first part; provider ids keep two parts ---
        if (EB.seriesRootId("tt0944947:3:4") !== "tt0944947")
            throw new Error("tt episode id must root to the tt id")
        if (EB.seriesRootId("kitsu:7442:2:5") !== "kitsu:7442")
            throw new Error("kitsu episode id must root to provider:id")
        if (EB.seriesRootId("tt0944947") !== "tt0944947")
            throw new Error("a bare series id roots to itself")
        if (EB.seriesRootId("") !== "")
            throw new Error("empty id roots to empty")

        // --- seasonOf: the season number inside an episode id ---
        if (EB.seasonOf("tt0944947:3:4") !== 3)
            throw new Error("seasonOf must read the season slot")
        if (EB.seasonOf("kitsu:7442:2:5") !== 2)
            throw new Error("seasonOf must handle provider-prefixed ids")
        if (EB.seasonOf("tt0944947") !== -1)
            throw new Error("no season slot -> -1")

        // --- showTitleFrom: strip the ' - SxEy' suffix the queue titles carry ---
        if (EB.showTitleFrom("Attack on Titan - S3E4") !== "Attack on Titan")
            throw new Error("showTitleFrom must strip the S/E suffix")
        if (EB.showTitleFrom("Plain Movie Title") !== "Plain Movie Title")
            throw new Error("showTitleFrom must pass a plain title through")

        // --- seasonsFrom: ascending, Specials (0) pinned last, deduped ---
        var vids = [
            { season: 2, episode: 1 }, { season: 1, episode: 1 },
            { season: 0, episode: 1 }, { season: 2, episode: 2 }, { season: 3, episode: 1 }
        ]
        var seasons = EB.seasonsFrom(vids)
        if (JSON.stringify(seasons) !== "[1,2,3,0]")
            throw new Error("seasons must be ascending with S0 pinned last, got " + JSON.stringify(seasons))

        // --- episodesFor: filter + order + id fallback construction ---
        var vids2 = [
            { season: 1, episode: 2, name: "Two", id: "tt1:1:2" },
            { season: 2, episode: 1, name: "Other" },
            { season: 1, episode: 1, title: "One" }        // no id, no name -> title, built id
        ]
        var eps = EB.episodesFor(vids2, 1, "tt1")
        if (eps.length !== 2)
            throw new Error("episodesFor must filter to the season")
        if (eps[0].num !== 1 || eps[1].num !== 2)
            throw new Error("episodesFor must order by episode number")
        if (eps[0].id !== "tt1:1:1")
            throw new Error("missing video id must be built as series:season:episode, got " + eps[0].id)
        if (eps[1].id !== "tt1:1:2")
            throw new Error("existing video id must be kept")
        if (eps[0].title !== "One" || eps[1].title !== "Two")
            throw new Error("row title must prefer name/title fields")

        // --- queueFrom: rows -> playable queue targets (shallowEpisodeTarget shape) ---
        var q = EB.queueFrom(eps, "My Show", "http://art/backdrop.jpg")
        if (q.length !== 2 || q[0].type !== "series")
            throw new Error("queueFrom must produce series targets")
        if (q[0].id !== "tt1:1:1" || q[0].season !== 1 || q[0].episode !== 1)
            throw new Error("queueFrom must carry id/season/episode")
        if (q[1].title !== "My Show - S1E2")
            throw new Error("queueFrom titles must be 'Show - SxEy', got " + q[1].title)
        if (q[0].backdrop !== "http://art/backdrop.jpg")
            throw new Error("queueFrom must carry the backdrop")

        // --- floorRows: the traveling queue rendered without any fetch ---
        var floor = EB.floorRows([
            { id: "tt1:1:1", title: "My Show - S1E1", season: 1, episode: 1 },
            { id: "tt1:1:2", title: "My Show - S1E2", season: 1, episode: 2 }
        ])
        if (floor.length !== 2 || floor[0].num !== 1 || floor[0].id !== "tt1:1:1")
            throw new Error("floorRows must mirror the queue as rows")
        if (floor[1].queueIdx !== 1)
            throw new Error("floorRows must remember each row's queue index")

        // --- rowState: watched / now / inProgress / unwatched + the 0.90 line ---
        function st(rec, rowId, nowId) { return EB.rowState(rec, rowId, nowId) }
        if (st({}, "tt1:1:1", "tt1:1:9").state !== "unwatched")
            throw new Error("empty record -> unwatched")
        if (st({ progress: 0.45 }, "tt1:1:1", "tt1:1:9").state !== "inProgress")
            throw new Error("partial progress -> inProgress")
        if (Math.abs(st({ progress: 0.45 }, "tt1:1:1", "tt1:1:9").frac - 0.45) > 0.001)
            throw new Error("frac must ride through")
        if (st({ progress: 0.95 }, "tt1:1:1", "tt1:1:9").state !== "watched")
            throw new Error(">=0.90 -> watched (ProgressStore's line)")
        if (st({ watched: true, progress: 0.5 }, "tt1:1:1", "tt1:1:9").state !== "watched")
            throw new Error("explicit watched flag wins")
        if (st({ progress: 0.95 }, "tt1:1:1", "tt1:1:1").state !== "now")
            throw new Error("the playing row is 'now' regardless of progress")

        // --- sourceRowState ---
        if (EB.sourceRowState(2, 2, false) !== "now")
            throw new Error("current index -> now")
        if (EB.sourceRowState(1, 2, true) !== "dead")
            throw new Error("dead check -> dead")
        if (EB.sourceRowState(0, 2, false) !== "playable")
            throw new Error("otherwise -> playable")

        // --- pickContinuityRow: same-torrent rows win episode jumps (season packs) ---
        var packRows = [
            { infoHash: "AAA111", fileIdx: 0, quality: "2160p" },
            { infoHash: "bbb222", fileIdx: 4, quality: "1080p" },   // the pack we're playing
            { infoHash: "ccc333", fileIdx: 0, quality: "1080p" }
        ]
        // same hash (case-insensitive), different file -> pack row wins over rank
        var pick = EB.pickContinuityRow(packRows, "BBB222", 3)
        if (!pick || pick.infoHash !== "bbb222" || pick.fileIdx !== 4)
            throw new Error("pack row (same hash, different file) must win over rank")
        // no same-hash row -> rank-best fallback (today's behavior)
        pick = EB.pickContinuityRow(packRows, "zzz999", 0)
        if (!pick || pick.infoHash !== "AAA111")
            throw new Error("no pack match must fall back to rows[0]")
        // same hash AND same fileIdx = the same file = wrong episode -> excluded
        pick = EB.pickContinuityRow(packRows, "bbb222", 4)
        if (!pick || pick.infoHash !== "AAA111")
            throw new Error("same-file row must be excluded from continuity")
        // url: streams never match — same url for a different episode is the same video
        var urlRows = [
            { infoHash: "ddd444", fileIdx: 0 },
            { infoHash: "url:http://x/ep.mp4", fileIdx: 0 }
        ]
        pick = EB.pickContinuityRow(urlRows, "url:http://x/ep.mp4", 0)
        if (!pick || pick.infoHash !== "ddd444")
            throw new Error("url: hashes must never continuity-match")
        // empty / missing inputs stay safe
        if (EB.pickContinuityRow([], "aaa", 0) !== null)
            throw new Error("empty rows -> null")
        if (EB.pickContinuityRow(null, "aaa", 0) !== null)
            throw new Error("null rows -> null")
        pick = EB.pickContinuityRow(packRows, "", 0)
        if (!pick || pick.infoHash !== "AAA111")
            throw new Error("no current hash -> rank-best")
    }
}
