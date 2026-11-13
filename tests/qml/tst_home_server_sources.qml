import QtQuick
import QtTest
import "../../qml"

Item {
    width: 1280
    height: 900
    QtObject {
        id: homeService
        property int revision: 1
        property int profileGeneration: 1
        property bool enabled: true
        property var lastIdentity: ({})
        property int requests: 0
        property string matchedId: "tt1234567"
        property var cached: [{ homeServer: true, infoHash: "home-server:server1:ItemA:VersionA",
            canonicalId: "tt1234567", connectionId: "server1", itemId: "ItemA", versionId: "VersionA",
            streamKind: "Home server", addonId: "home-server:server1", addonName: "Your server · Living room",
            sourceName: "Your server · Living room", release: "My movie", quality: "1080p", fileIdx: 0,
            seeders: -1, tags: [] }]
        signal changed()
        signal profileChanged()
        function sources(identity) {
            lastIdentity = identity
            requests++
            return enabled && identity.id === matchedId ? cached : []
        }
    }
    QtObject { id: extensions; function installed() { return [] } }
    Window {
        id: hostWindow
        width: 1280
        height: 900
        visible: true
        property bool androidHost: false
        property bool desktopPresentation: false
        property int theatreSourcesDockInset: 0
        SourcesSheet {
            id: sheet
            homeServerSourceService: homeService
            nuvioSourceService: null
            extensionsService: extensions
        }
    }
    SignalSpy { id: playSpy; target: sheet; signalName: "playRequested" }
    SignalSpy { id: downloadSpy; target: sheet; signalName: "downloadRequested" }
    TestCase {
        name: "HomeServerSources"
        when: windowShown
        function init() {
            homeService.enabled = true
            homeService.profileGeneration = 1
            homeService.matchedId = "tt1234567"
            homeService.requests = 0
            sheet.rows = []
            sheet.nuvioRows = []
            playSpy.clear()
            downloadSpy.clear()
        }
        function cleanup() { sheet.hide(); hostWindow.androidHost = false; hostWindow.width = 1280 }
        function directRow(name, key, nuvio) {
            return { infoHash: "url:https://video.example.org/" + key + ".m3u8", fileIdx: 0,
                streamKind: "Direct", addonName: name, sourceName: name, release: "Movie",
                quality: "720p", seeders: -1, tags: [], nuvio: nuvio }
        }
        function test_matchesPrependAndProviderFilterPreservesOthers() {
            sheet.show("movie", "tt1234567", "Movie", { tmdbId: 456, imdbId: "tt1234567" })
            sheet.rows = [directRow("Addon one", "one", false), directRow("Addon two", "two", false)]
            sheet.nuvioRows = [directRow("Nuvio plugin", "three", true)]
            compare(homeService.lastIdentity.id, "tt1234567")
            compare(homeService.lastIdentity.type, "movie")
            compare(homeService.lastIdentity.tmdbId, 456)
            compare(sheet.baseRows().map(row => row.addonName),
                    ["Your server · Living room", "Addon one", "Addon two", "Nuvio plugin"])
            compare(sheet.providerOptions.map(option => [option.label, option.count]),
                    [["All", 4], ["Your server · Living room", 1], ["Nuvio", 1], ["Addon one", 1], ["Addon two", 1]])
            sheet.activateExtOption("home-server:server1")
            compare(sheet.visibleRows.length, 1)
            compare(sheet.visibleRows[0].infoHash, homeService.cached[0].infoHash)
            sheet.qualityFilter = "720p"
            compare(sheet.visibleRows.length, 0)
            sheet.qualityFilter = "all"
            sheet.activateExtOption("all")
            sheet.activateSource(0)
            compare(playSpy.count, 1)
            compare(playSpy.signalArguments[0][0], "home-server:server1:ItemA:VersionA")
            compare(playSpy.signalArguments[0][6].length, 4)
            compare(playSpy.signalArguments[0][7].tmdbId, 456)
        }
        function test_copyDownloadAndContextAreBlockedOnBothLayouts() {
            sheet.show("movie", "tt1234567", "Movie")
            sheet.copySource(0)
            sheet.downloadSource(0)
            compare(downloadSpy.count, 0)
            compare(sheet.titleQueued, false)
            sheet.openSourceContext(0)
            compare(sheet.sourceContextOptions(), ["Play"])
            sheet.activateSourceContext(1)
            compare(playSpy.count, 0)
            sheet.openSourceContext(0)
            sheet.activateSourceContext(2)
            compare(downloadSpy.count, 0)
            let list = findChild(sheet, "sourcesSheetList")
            tryVerify(function() { return list.itemAtIndex(0) !== null })
            let row = list.itemAtIndex(0)
            compare(findChild(row, "sourceCopyWide_0").visible, false)
            compare(findChild(row, "sourceDownloadWide_0").visible, false)
            hostWindow.width = 400
            hostWindow.androidHost = true
            tryCompare(sheet, "phoneLayout", true)
            compare(findChild(row, "sourceCopy_0").visible, false)
            compare(findChild(row, "sourceDownload_0").visible, false)
            verify(findChild(row, "sourcePlay_0").visible)
        }
        function test_disabledOrUnmatchedHasNoProviderOption() {
            homeService.enabled = false
            sheet.show("movie", "tt1234567", "Movie")
            compare(sheet.baseRows(), [])
            compare(sheet.providerOptions.map(option => option.label), ["All"])
            homeService.enabled = true
            sheet.show("movie", "tt9999999", "Other movie")
            compare(sheet.homeServerRows, [])
            compare(sheet.providerOptions.length, 1)
        }
        function test_homeMetadataIsLiteralOnDesktopPhoneAndProviderMenu() {
            let original = homeService.cached
            try {
                homeService.cached = [Object.assign({}, original[0], {
                    addonName: "Your server · <b>Server</b>", sourceName: "Your server · <b>Server</b>",
                    release: "<b>Movie title</b>"
                })]
                sheet.show("movie", "tt1234567", "Movie")
                sheet.rows = [directRow("Ordinary addon", "ordinary", false)]
                let list = findChild(sheet, "sourcesSheetList")
                tryVerify(function() { return list.itemAtIndex(0) && list.itemAtIndex(1) })
                let row = list.itemAtIndex(0)
                let provider = findChild(row, "sourceProviderWide_0")
                let title = findChild(row, "sourceReleaseWide_0")
                compare(provider.text, "Your server · <b>Server</b>")
                compare(provider.textFormat, Text.PlainText)
                compare(title.text, "<b>Movie title</b>")
                compare(title.textFormat, Text.PlainText)
                compare(findChild(list.itemAtIndex(1), "sourceProviderWide_1").textFormat, Text.AutoText)
                sheet.openExtMenu(null)
                tryVerify(function() { return findChild(sheet, "extPickLabel_1") !== null })
                let option = findChild(sheet, "extPickLabel_1")
                compare(option.text, "Your server · <b>Server</b>")
                compare(option.textFormat, Text.PlainText)
                sheet.activateExtOption("home-server:server1")
                compare(findChild(sheet, "extPickerLabel").text, "Your server · <b>Server</b>")
                compare(findChild(sheet, "extPickerLabel").textFormat, Text.PlainText)
                hostWindow.width = 400
                hostWindow.androidHost = true
                tryCompare(sheet, "phoneLayout", true)
                compare(findChild(row, "sourceProviderPhone_0").textFormat, Text.PlainText)
                compare(findChild(row, "sourceProviderPhone_0").text, "Your server · <b>Server</b>")
                compare(findChild(row, "sourceReleasePhone_0").textFormat, Text.PlainText)
                compare(findChild(row, "sourceReleasePhone_0").text, "<b>Movie title</b>")
            } finally {
                homeService.cached = original
            }
        }
        function test_cachedUpdatesAndProfileFence() {
            sheet.show("movie", "tt1234567", "Movie")
            compare(sheet.homeServerRows.length, 1)
            homeService.enabled = false
            homeService.revision++
            homeService.changed()
            compare(sheet.homeServerRows.length, 0)
            homeService.enabled = true
            homeService.changed()
            compare(sheet.homeServerRows.length, 1)
            homeService.profileGeneration++
            homeService.profileChanged()
            homeService.changed()
            compare(sheet.homeServerRows.length, 0)
            compare(sheet.providerOptions.length, 1)
            sheet.show("movie", "tt1234567", "Movie")
            compare(sheet.homeServerRows.length, 1)
            sheet.hide()
            homeService.changed()
            compare(sheet.homeServerRows.length, 0)
        }
        function test_onlyPlayModeQueriesAndIdentityChangesRefresh() {
            sheet.show("movie", "tt1234567", "Movie", {}, "download")
            compare(homeService.requests, 0)
            compare(sheet.homeServerRows.length, 0)
            sheet.show("series", "tt1234567:2:3", "Episode", { season: 2, episode: 3, tvdbId: "999" })
            compare(homeService.lastIdentity.season, 2)
            compare(homeService.lastIdentity.episode, 3)
            compare(homeService.lastIdentity.tvdbId, "999")
            sheet.subType = "movie"
            sheet.subId = "tt1234567"
            compare(sheet.homeServerRows.length, 1)
            sheet.subId = "tt9999999"
            compare(sheet.homeServerRows.length, 0)
        }
    }
}
