import QtQuick
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "HomeServerVault"
    when: windowShown
    QtObject {
        id: service
        property int revision: 1
        property int profileGeneration: 1
        property var shelves: []
        property var connections: [{ id: "one", provider: "jellyfin" }, { id: "two", provider: "plex" }]
        property var titleData: ({})
        signal changed()
        signal profileChanged()
        function shelfTitles(id) { return titleData[id] || [] }
    }
    Window {
        id: host
        width: 1100
        height: 780
        visible: true
        Colosseum.VaultPage {
            id: vault
            anchors.fill: parent
            homeServerSourceService: service
        }
    }
    SignalSpy { id: routeSpy; target: vault; signalName: "viewWorldRequested" }
    SignalSpy { id: localSpy; target: vault; signalName: "openMediaRequested" }
    function title(id, name, kind) {
        return { id: id, title: name, kind: kind, year: "2024", coverUrl: "",
            identity: { world: "Theatre", identityId: kind === "series" ? "tt7654321" : "tt1234567",
                type: kind, title: name, coverUrl: "" } }
    }
    function init() {
        service.shelves = []
        service.profileGeneration = 1
        service.titleData = ({ one: [title("film", "<b>Movie</b>", "movie")],
                               two: [title("show", "Series", "series")] })
        vault.leaveServerShelf()
        vault.localViewRequested = false
        vault.hiddenViewActive = false
        routeSpy.clear()
        localSpy.clear()
    }
    function cleanup() { service.shelves = []; service.changed() }
    function connectServers() {
        service.shelves = [{ id: "one", name: "<b>Living room</b>", titleCount: 1 },
                           { id: "two", name: "Family Plex", titleCount: 1 }]
        service.revision++
        service.changed()
    }
    function shelf() {
        let loader = findChild(vault, "homeServerVaultShelfLoader")
        verify(loader !== null)
        tryVerify(function() { return loader.item !== null })
        return loader.item
    }
    function test_serverOnlyRendersRailAndReadOnlyShelf() {
        compare(vault.hasConfirmedStorage, false)
        connectServers()
        compare(vault.hasBrowseSurface, true)
        compare(vault.selectedServerId, "one")
        verify(findChild(vault, "vaultBrowseFace").visible)
        let rail = findChild(vault, "vaultBrowseRail")
        verify(rail.visible)
        compare(rail.roots.length, 0)
        compare(rail.servers.length, 2)
        let serverLabel = findChild(rail, "vaultBrowseRailServerName_one")
        compare(serverLabel.text, "<b>Living room</b>")
        compare(serverLabel.textFormat, Text.PlainText)
        let serverLogo = findChild(rail, "vaultBrowseRailServerIcon_one")
        verify(serverLogo !== null)
        tryCompare(serverLogo, "status", Image.Ready)
        verify(serverLogo.source.toString().endsWith("/servers/jellyfin.svg"))
        let view = shelf()
        compare(findChild(view, "homeServerVaultShelfName").text, "<b>Living room</b>")
        compare(findChild(view, "homeServerVaultShelfName").textFormat, Text.PlainText)
        let grid = findChild(view, "homeServerVaultTitles")
        tryVerify(function() { return grid.itemAtIndex(0) !== null })
        let card = grid.itemAtIndex(0)
        let label = findChild(card, "homeServerVaultTitleLabel_film")
        compare(label.text, "<b>Movie</b>")
        compare(label.textFormat, Text.PlainText)
        mouseClick(card)
        compare(routeSpy.count, 1)
        compare(routeSpy.signalArguments[0][0], service.titleData.one[0].identity)
        compare(localSpy.count, 0)
        compare(findChild(view, "vaultBrowseContextMenu"), null)
        compare(findChild(view, "vaultBrowseContextIdentify"), null)
        mouseClick(card, 5, 5, Qt.RightButton)
        compare(routeSpy.count, 1)
        compare(localSpy.count, 0)
    }
    function test_serverSwitchAndStaleRemovalCannotRouteOldTitle() {
        connectServers()
        let oldRow = service.titleData.one[0]
        let generation = service.profileGeneration
        let rail = findChild(vault, "vaultBrowseRail")
        mouseClick(findChild(rail, "vaultBrowseRailServer_two"))
        compare(vault.selectedServerId, "two")
        compare(vault.serverTitleRows[0].id, "show")
        vault.openServerTitle(oldRow, "one", generation)
        compare(routeSpy.count, 0)
        vault.openServerTitle(service.titleData.two[0], "two", generation)
        compare(routeSpy.count, 1)
        compare(routeSpy.signalArguments[0][0].type, "series")
        service.shelves = [{ id: "one", name: "Living room", titleCount: 1 }]
        service.changed()
        compare(vault.selectedServerId, "one")
        compare(vault.serverTitleRows[0].id, "film")
        vault.openServerTitle(service.titleData.two[0], "two", generation)
        compare(routeSpy.count, 1)
        service.shelves = []
        service.changed()
        compare(vault.serverViewActive, false)
        compare(vault.serverTitleRows.length, 0)
        compare(vault.hasBrowseSurface, false)
        vault.openServerTitle(oldRow, "one", generation)
        compare(routeSpy.count, 1)
    }
    function test_localRootAndHiddenReturnToLocalView() {
        connectServers()
        vault.selectRoot("C:/LocalMovies", "Local movies")
        compare(vault.serverViewActive, false)
        compare(vault.currentBrowsePath, "C:/LocalMovies")
        compare(vault.serverTitleRows.length, 0)
        service.changed()
        compare(vault.serverViewActive, false)
        vault.selectServer("two")
        vault.openHidden()
        compare(vault.serverViewActive, false)
        compare(vault.hiddenViewActive, true)
        service.changed()
        compare(vault.serverViewActive, false)
    }
    function test_profileChangeAndDisconnectedStateClearShelf() {
        connectServers()
        let oldRow = vault.serverTitleRows[0]
        let oldGeneration = service.profileGeneration
        service.profileGeneration++
        service.shelves = []
        service.profileChanged()
        service.changed()
        compare(vault.serverViewActive, false)
        compare(vault.serverTitleRows.length, 0)
        compare(findChild(vault, "vaultBrowseRailServer_one"), null)
        compare(findChild(vault, "homeServerVaultShelfLoader").item, null)
        vault.openServerTitle(oldRow, "one", oldGeneration)
        compare(routeSpy.count, 0)
        compare(vault.hasBrowseSurface, false)
    }
    function test_connectedEmptyShelfHasNoFakeTitles() {
        service.titleData = ({ empty: [] })
        service.shelves = [{ id: "empty", name: "Empty server", titleCount: 0 }]
        service.changed()
        compare(vault.selectedServerId, "empty")
        let view = shelf()
        compare(findChild(view, "homeServerVaultTitles").count, 0)
        verify(findChild(view, "homeServerVaultShelfEmpty").visible)
        compare(routeSpy.count, 0)
        compare(localSpy.count, 0)
    }
    function test_cachedTitleRemovalRejectsStaleCardAndRoutesCurrentProjection() {
        connectServers()
        let original = service.titleData.one[0]
        let stale = Object.assign({}, original, { identity: { world: "Theatre", identityId: "wrong" } })
        vault.openServerTitle(stale, "one", service.profileGeneration)
        compare(routeSpy.count, 1)
        compare(routeSpy.signalArguments[0][0], original.identity)
        service.titleData = ({ one: [], two: service.titleData.two })
        service.revision++
        service.changed()
        compare(vault.serverTitleRows.length, 0)
        vault.openServerTitle(original, "one", service.profileGeneration)
        compare(routeSpy.count, 1)
    }
}
