// Headless behavioral harness for AddonClient.isSeasonPack (the season checkout's
// full-season torrent filter, 2026-07-19). Driven by qml.exe; verdict rides the
// EXIT CODE — Qt.exit(0) pass, non-zero fail — because console output is not
// guaranteed to flush before exit and an uncaught onCompleted throw would HANG
// qml.exe rather than exit non-zero.
import QtQuick
import "../qml/AddonClient.js" as AddonClient

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

    function pack(release, season) {
        return AddonClient.isSeasonPack({ streamKind: "Torrent", release: release }, season)
    }

    function runChecks() {
        // --- packs: season claims, complete runs, episode ranges ---
        if (!pack("Show.S01.1080p.WEB-DL.x265", 1))
            throw new Error("bare S01 must read as a season pack")
        if (!pack("Show Season 1 Complete 1080p", 1))
            throw new Error("'Season 1 Complete' must read as a season pack")
        if (!pack("[Group] Anime Batch (001-1071) [1080p]", 1))
            throw new Error("an episode range must read as a season pack")
        if (!pack("Show S01E01-E26 1080p", 1))
            throw new Error("an SxxExx-Eyy range is a run, not one episode")
        if (!pack("Anime Complete Series Dual Audio", 3))
            throw new Error("'Complete' must read as a season pack in any season")

        // --- single episodes: never packs ---
        if (pack("Show.S01E05.1080p.WEB-DL", 1))
            throw new Error("S01E05 is one episode, not a pack")
        if (pack("Show 1x05 720p HDTV", 1))
            throw new Error("1x05 is one episode, not a pack")
        if (pack("[Feibanyama] One Piece EP0001 [IQIYI WebRip 2160p HEVC]", 1))
            throw new Error("EP0001 is one episode, not a pack")

        // --- wrong season: S02 pack must not answer a season-1 ask ---
        if (pack("Show.S02.1080p.WEB-DL", 1))
            throw new Error("an S02 pack must not pass for season 1")

        // --- direct-HTTP rows are one file by nature ---
        if (AddonClient.isSeasonPack({ streamKind: "Direct", release: "Show Season 1 Complete" }, 1))
            throw new Error("a direct url row can never be a season pack")

        // --- junk in, calm out ---
        if (AddonClient.isSeasonPack({ streamKind: "Torrent", release: "" }, 1)
                || AddonClient.isSeasonPack({ streamKind: "Torrent" }, 1))
            throw new Error("empty release must not read as a pack")
    }
}
