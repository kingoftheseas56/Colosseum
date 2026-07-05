// THROWAWAY verification harness — real MangaSeries page over the MangaDex volume feed.
// Loads the actual series page for a title and reports the shelf/table state after resolve.
//   native/build-msvc/colosseum.exe qml/_volpagecheck.qml
import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1280; height: 720
    visible: true
    color: "#05060a"
    title: "volpagecheck"

    Item { id: wall; anchors.fill: parent }

    MangaSeries {
        id: page
        anchors.fill: parent
        backdrop: wall
        seriesTitle: "Bleach"
    }

    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ticks++
            if (!page.loading || ticks >= 25) {
                console.log("[volpagecheck] loading:", page.loading,
                            "volumes:", page.volumes.length,
                            "shelfTiles:", page.shelfVolumes.length,
                            "groupedOptions:", page.volGroups.options.length,
                            "shownVol:", JSON.stringify(page.shownVol),
                            "visibleChapters:", page.visibleChapters.length)
                var withCovers = page.volumes.filter(function(v) { return v.cover && v.cover.length }).length
                console.log("[volpagecheck] covers on", withCovers, "of", page.volumes.length, "volumes")
                console.log("[volpagecheck] art — banner:", page.banner.length > 0,
                            "synopsis:", page.synopsis.length > 0,
                            "genres:", page.genres.length, "score:", page.score, "year:", page.year)
                var artOk = page.banner.length > 0 && page.synopsis.length > 0 && page.genres.length > 0
                console.log((page.volumes.length > 0 && withCovers > 0
                             && page.visibleChapters.length > 0 && artOk)
                            ? "[volpagecheck] PAGE GREEN" : "[volpagecheck] PAGE RED")
                running = false
            }
        }
    }
}
