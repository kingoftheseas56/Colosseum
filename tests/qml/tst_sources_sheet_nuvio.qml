import QtQuick
import QtTest
import "../../qml"
import "../../qml/AddonClient.js" as AddonClient

Item {
    width: 1280
    height: 900

    QtObject {
        id: nuvio
        property int lastId: 0
        property var lastRequest: []
        property var cancelled: []
        signal streamsReady(int requestId, var streams)
        signal finished(int requestId)

        function resolve(mediaId, mediaType, season, episode) {
            lastRequest = [mediaId, mediaType, season, episode]
            return ++lastId
        }
        function cancel(requestId) { cancelled = cancelled.concat([requestId]) }
    }

    QtObject {
        id: stremio
        property var requests: []
        function installed() {
            return [{
                enabled: true, id: "stremio.test", transportUrl: "https://example.org",
                manifest: { name: "Stremio fixture", resources: ["stream"], types: ["movie", "series"] }
            }]
        }
        function load(extensions, type, mediaId, partial, done) {
            requests = requests.concat([{ extensions, type, mediaId, partial, done }])
        }
    }

    Window {
        id: hostWindow
        width: 1280
        height: 900
        visible: true
        property bool androidHost: false
        readonly property int theatreSourcesDockInset: taskbar.visible ? taskbar.closedSize + taskbar.bottomGap : 0

        SourcesSheet {
            id: sheet
            nuvioSourceService: nuvio
            extensionsService: stremio
            loadStremioStreams: stremio.load
        }
        Taskbar { id: taskbar }
        NuvioRepositoryCard {
            id: repository
            width: 400
            visible: false
            busy: false
            expanded: true
            repository: { return { id: "repo", name: "Fixture repo",
                                   plugins: [{ id: "plugin", name: "Fixture plugin", enabled: true }] } }
        }
    }

    SignalSpy { id: playSpy; target: sheet; signalName: "playRequested" }

    TestCase {
        name: "SourcesSheetNuvio"
        when: windowShown

        function init() {
            stremio.requests = []
            nuvio.lastRequest = []
            nuvio.cancelled = []
            playSpy.clear()
        }

        function cleanup() {
            sheet.hide()
            hostWindow.width = 1280
            hostWindow.androidHost = false
        }

        function stream(url) {
            return {
                url, name: "1080p", title: "Fixture release", pluginName: "Fixture plugin",
                pluginId: "plugin", repoId: "repo",
                headers: { Referer: "https://nuvio.example.org/" }
            }
        }

        function test_independentRepliesAndDirectPlayback() {
            var url = "https://video.example.org/master.m3u8"
            sheet.show("series", "tt12345:2:3", "Episode", { tmdbId: 456, season: 2, episode: 3 })
            compare(nuvio.lastRequest, ["456", "series", 2, 3])
            compare(stremio.requests[0].mediaId, "tt12345:2:3")
            compare(stremio.requests[0].type, "series")
            compare(stremio.requests[0].extensions[0].id, "stremio.test")
            compare(sheet.askedCount, 1)

            nuvio.streamsReady(nuvio.lastId, [stream(url)])
            compare(sheet.rowCount, 1)
            compare(sheet.loading, true)
            compare(sheet.nuvioPending, true)
            tryVerify(function() { return findChild(sheet, "sourceRow_0") !== null })
            var row = findChild(sheet, "sourceRow_0")
            compare(row.providerName, "Fixture plugin")
            var heading = findChild(row, "nuvioSourceHeading")
            var brand = findChild(heading, "nuvioSourceBrandChip")
            var direct = findChild(heading, "nuvioSourceDirectChip")
            compare(brand.text, "NUVIO")
            compare(direct.text, "DIRECT")
            tryCompare(direct, "y", brand.y)
            tryCompare(findChild(heading, "nuvioSourceQuality"), "y", direct.y)
            verify(!findChild(row, "sourceStreamType").visible)
            compare(sheet.visibleRows[0].headers.Referer, "https://nuvio.example.org/")
            sheet.openSourceContext(0)

            var stremioRow = AddonClient.parseStream({ url, title: "Same URL", behaviorHints: {
                proxyHeaders: { request: { Referer: "https://stremio.example.org/" } }
            } }, "Stremio fixture", 0, "stremio.test")
            stremio.requests[0].partial([stremioRow])
            compare(sheet.rowCount, 2)
            compare(sheet.visibleRows[0].addonName, "Stremio fixture")
            compare(sheet.loading, true)
            stremio.requests[0].done([stremioRow])
            compare(sheet.loading, false)
            compare(sheet.nuvioPending, true)

            sheet.activateSourceContext(0)
            compare(playSpy.count, 1)
            var args = playSpy.signalArguments[0]
            compare(args[0], "url:" + url)
            compare(args[4], "series")
            compare(args[5], "tt12345:2:3")
            compare(args[6][0].headers.Referer, "https://nuvio.example.org/")
            compare(args[7].tmdbId, 456)
            compare(args[7].episode, 3)

            nuvio.streamsReady(nuvio.lastId, [stream("https://video.example.org/second.m3u8")])
            nuvio.finished(nuvio.lastId)
            compare(sheet.rowCount, 3)
            compare(sheet.nuvioPending, false)
        }

        function test_badgesAndDockClearance() {
            sheet.show("movie", "tt1234567", "Movie")
            var batch = []
            for (var i = 0; i < 8; ++i)
                batch.push(stream("https://video.example.org/" + i + ".m3u8"))
            nuvio.streamsReady(nuvio.lastId, batch)
            tryCompare(sheet, "rowCount", 8)
            var list = findChild(sheet, "sourcesSheetList")
            var dock = findChild(taskbar, "taskbarDock")
            list.positionViewAtEnd()
            tryVerify(function() { return list.itemAtIndex(7) !== null })
            var row = list.itemAtIndex(7)
            verify(row.mapToItem(sheet, 0, row.height).y <= dock.mapToItem(sheet, 0, 0).y)

            hostWindow.width = 400
            hostWindow.androidHost = true
            tryCompare(sheet, "phoneLayout", true)
            list.positionViewAtBeginning()
            tryVerify(function() { return list.itemAtIndex(0) !== null })
            row = list.itemAtIndex(0)
            var heading = findChild(row, "nuvioSourceHeadingPhone")
            verify(heading.visible)
            compare(findChild(heading, "nuvioSourceBrandChip").text, "NUVIO")
            var direct = findChild(heading, "nuvioSourceDirectChip")
            var repoDirect = findChild(repository, "nuvioPluginDirectChip")
            compare(direct.text, "DIRECT")
            compare(repoDirect.text, direct.text)
            compare(repoDirect.color, direct.color)
            compare(repoDirect.border.color, direct.border.color)
            verify(!findChild(row, "sourceStreamTypePhone").visible)
        }

        function test_replacementAndClosedResults() {
            var batch = [stream("https://video.example.org/master.m3u8")]
            sheet.show("movie", "tt12345", "First")
            var firstId = nuvio.lastId
            sheet.show("series", "tt67890:0:4", "Second")
            var secondId = nuvio.lastId
            compare(nuvio.lastRequest, ["tt67890:0:4", "series", 0, 4])
            compare(nuvio.cancelled, [firstId])

            nuvio.streamsReady(firstId, batch)
            nuvio.finished(firstId)
            stremio.requests[0].done([])
            compare(sheet.nuvioPending, true)
            compare(sheet.loading, true)
            compare(sheet.rowCount, 0)

            sheet.hide()
            compare(nuvio.cancelled, [firstId, secondId])
            nuvio.streamsReady(secondId, batch)
            nuvio.finished(secondId)
            compare(sheet.rowCount, 0)
            compare(sheet.nuvioPending, false)
        }

        function test_downloadSeasonAndSeriesWithoutEpisode() {
            sheet.show("movie", "tt12345", "Film", {}, "download")
            sheet.show("series", "tt12345:1:3", "Season", { season: 1 }, "season")
            sheet.show("series", "tt12345", "Series")
            compare(nuvio.lastRequest, [])
            compare(stremio.requests.length, 3)
        }
    }
}
