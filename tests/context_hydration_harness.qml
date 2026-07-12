// Headless behavioral harness for the context-hydration pure logic (bare-door fix
// 2026-07-12: Continue-Watching / downloaded entries open the player with no episode
// queue and no source list, so prev/next-episode and change-stream buttons vanish).
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
        // --- isEpisodeId: only ids with numeric season+episode slots hydrate ---
        if (EB.isEpisodeId("tt0944947:2:3") !== true)
            throw new Error("tt episode id must be an episode id")
        if (EB.isEpisodeId("kitsu:7442:2:5") !== true)
            throw new Error("provider episode id must be an episode id")
        if (EB.isEpisodeId("tt0944947") !== false)
            throw new Error("a bare movie/series id is not an episode id")
        if (EB.isEpisodeId("") !== false)
            throw new Error("empty id is not an episode id")
        if (EB.isEpisodeId("iptv:chan:1:2") !== false)
            throw new Error("iptv ids must never hydrate")
        if (EB.isEpisodeId("url:http://host/file.mkv") !== false)
            throw new Error("url ids must never hydrate")
        if (EB.isEpisodeId("local:C:/x/file.mkv") !== false)
            throw new Error("local ids must never hydrate")
        if (EB.isEpisodeId("tt1:1:abc") !== false)
            throw new Error("non-numeric episode slot is not an episode id")

        // --- queueContextFromMeta: Cinemeta videos -> the playing episode's season queue ---
        var videos = [
            { season: 1, episode: 1, name: "One-One", id: "tt1:1:1" },
            { season: 2, episode: 2, name: "Two-Two", id: "tt1:2:2" },
            { season: 2, episode: 1, name: "Two-One", id: "tt1:2:1" },
            { season: 2, episode: 3, name: "Two-Three" },   // no id -> built as tt1:2:3
            { season: 0, episode: 1, name: "Special" }
        ]
        var ctx = EB.queueContextFromMeta(videos, "tt1:2:2", "My Show", "http://art/b.jpg", "2019")
        if (!ctx)
            throw new Error("queueContextFromMeta must build a context when the episode is in the meta")
        if (!ctx.episodeQueue || ctx.episodeQueue.length !== 3)
            throw new Error("queue must hold ONLY the playing season's episodes, got "
                            + (ctx.episodeQueue ? ctx.episodeQueue.length : "none"))
        if (ctx.episodeQueue[0].id !== "tt1:2:1" || ctx.episodeQueue[2].id !== "tt1:2:3")
            throw new Error("queue must be episode-ordered with built ids for missing ones")
        if (ctx.episodeIndex !== 1)
            throw new Error("episodeIndex must point at the playing episode, got " + ctx.episodeIndex)
        if (ctx.episodeQueue[1].title !== "My Show - S2E2")
            throw new Error("queue titles must be 'Show - SxEy', got " + ctx.episodeQueue[1].title)
        if (ctx.episodeQueue[0].backdrop !== "http://art/b.jpg")
            throw new Error("queue targets must carry the backdrop")
        if (ctx.year !== "2019")
            throw new Error("context must carry the year for the player title line")

        // the shape must feed PlayerPage.resolveAdjacentContext as-is: prev/next derivable
        if (ctx.episodeQueue[ctx.episodeIndex - 1].id !== "tt1:2:1")
            throw new Error("prev neighbor must be the previous season episode")
        if (ctx.episodeQueue[ctx.episodeIndex + 1].id !== "tt1:2:3")
            throw new Error("next neighbor must be the next season episode")

        // honest no-ops: unknown episode, non-episode id, empty meta
        if (EB.queueContextFromMeta(videos, "tt1:9:9", "My Show", "", "") !== null)
            throw new Error("an episode missing from the meta must NOT build a context")
        if (EB.queueContextFromMeta(videos, "tt1", "My Show", "", "") !== null)
            throw new Error("a non-episode id must NOT build a context")
        if (EB.queueContextFromMeta([], "tt1:2:2", "My Show", "", "") !== null)
            throw new Error("empty meta must NOT build a context")

        // --- mergeHydratedCandidates: fetched source list merged around the playing one ---
        var fetched = [
            { infoHash: "AAA", fileIdx: 0, title: "1080p" },
            { infoHash: "bbb", fileIdx: 2, title: "720p" },
            { infoHash: "ccc", fileIdx: 0, title: "480p" }
        ]
        // playing candidate present in the fetched list (case-insensitive hash, same fileIdx)
        var m = EB.mergeHydratedCandidates({ infoHash: "BBB", fileIdx: 2 }, fetched)
        if (!m || m.list.length !== 3 || m.index !== 1)
            throw new Error("a playing candidate found in the fetched list must keep its position")
        // playing candidate NOT in the list -> prepended, index 0
        m = EB.mergeHydratedCandidates({ infoHash: "zzz", fileIdx: 0, title: "mine" }, fetched)
        if (!m || m.list.length !== 4 || m.index !== 0 || m.list[0].infoHash !== "zzz")
            throw new Error("an unknown playing candidate must be prepended at index 0")
        // direct-url identity: url field matches a fetched row riding the url: hash prefix
        m = EB.mergeHydratedCandidates({ url: "http://host/v.mkv" },
                                       [ { infoHash: "url:http://host/v.mkv", fileIdx: 0 } ])
        if (!m || m.index !== 0 || m.list.length !== 1)
            throw new Error("a url candidate must match a fetched row with the url: hash prefix")
        // honest no-ops: nothing fetched, or a playing candidate with no identity
        if (EB.mergeHydratedCandidates({ infoHash: "aaa", fileIdx: 0 }, []) !== null)
            throw new Error("an empty fetched list must NOT hydrate")
        if (EB.mergeHydratedCandidates({}, fetched) !== null)
            throw new Error("a candidate with no identity must NOT hydrate (cannot merge safely)")
    }
}
