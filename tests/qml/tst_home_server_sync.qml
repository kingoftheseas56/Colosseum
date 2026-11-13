import QtQuick
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "HomeServerSync"
    when: windowShown
    width: 640
    height: 800
    visible: true

    QtObject {
        id: fixture
        property var connections: []
        property var plexServers: []
        property bool available: true
        property bool busy: false
        property string status: ""
        property string plexCode: ""
        property string plexUrl: ""
        property bool httpConsentRequired: false
        property int revision: 0
        property int profileGeneration: 1
        property int submits: 0
        property int cancels: 0
        property int plexStarts: 0
        property var received: ({})
        property string expectedPassword: ""
        property string selectedId: ""
        property bool selectedHttp: false
        property string enabledId: ""
        property bool enabledValue: false
        property string removedId: ""
        property string refreshedId: ""
        signal changed()
        signal connectionAdded(string id)
        function isInsecureAddress(address) { return address.trim().toLowerCase().indexOf("http://") === 0 }
        function connectServer(provider, address, username, password, allowHttp) {
            submits++
            received = { provider: provider, address: address, username: username, hadPassword: password.length > 0,
                         passwordMatched: password === expectedPassword, allowHttp: allowHttp }
        }
        function beginPlex() { plexStarts++ }
        function selectPlexServer(id, allowHttp) { selectedId = id; selectedHttp = allowHttp }
        function cancelSignIn() { cancels++ }
        function setEnabled(id, value) { enabledId = id; enabledValue = value }
        function remove(id) { removedId = id }
        function refresh(id) { refreshedId = id }
    }
    Component { id: panelComponent; Colosseum.HomeServerConnectPanel { service: fixture } }
    Component { id: cardComponent; Colosseum.HomeServerSyncCard { width: 420; service: fixture } }

    function init() {
        fixture.connections = []
        fixture.plexServers = []
        fixture.available = true
        fixture.busy = false
        fixture.status = ""
        fixture.plexCode = ""
        fixture.plexUrl = ""
        fixture.httpConsentRequired = false
        fixture.profileGeneration = 1
        fixture.submits = 0
        fixture.cancels = 0
        fixture.plexStarts = 0
        fixture.received = ({})
        fixture.expectedPassword = ""
        fixture.selectedId = ""
        fixture.selectedHttp = false
        fixture.enabledId = ""
        fixture.removedId = ""
        fixture.refreshedId = ""
    }
    function openPanel() {
        let panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null)
        panel.open()
        tryCompare(panel, "opened", true)
        return panel
    }
    function field(panel, name) {
        let result = findChild(panel, name)
        verify(result !== null, name)
        return result
    }
    function credentials(panel, address) {
        field(panel, "homeServerAddress").text = address
        field(panel, "homeServerUsername").text = "viewer"
        field(panel, "homeServerPassword").text = "test-secret"
    }
    function test_emptyCardAndActions() {
        let card = createTemporaryObject(cardComponent, testCase)
        verify(card !== null)
        verify(findChild(card, "homeServerConnectButton").visible)
        let logos = findChild(card, "homeServerLogoTrio")
        for (let i = 0; i < 3; ++i) tryCompare(logos.itemAt(i), "status", Image.Ready)
        compare(findChild(card, "homeServerEnabled_server1"), null)
        fixture.connections = [{ id: "server1", name: "Living room", provider: "jellyfin", enabled: true, needsReconnect: false, refreshing: false, status: "Connected" }]
        let toggle = null
        tryVerify(function() { toggle = findChild(card, "homeServerEnabled_server1"); return toggle !== null })
        let refresh = findChild(card, "homeServerRefresh_server1")
        verify(refresh !== null)
        verify(refresh.enabled)
        mouseClick(refresh)
        compare(fixture.refreshedId, "server1")
        mouseClick(toggle)
        compare(fixture.enabledId, "server1")
        compare(fixture.enabledValue, false)
        mouseClick(findChild(card, "homeServerRemove_server1"))
        compare(fixture.removedId, "server1")
        mouseClick(findChild(card, "homeServerConnectButton"))
        compare(card.panelOpen, true)
        card.panelOpen = false
    }
    function test_refreshRequiresEnabledConnectedIdleServer() {
        let card = createTemporaryObject(cardComponent, testCase)
        verify(card !== null)
        fixture.connections = [{ id: "server1", name: "Living room", provider: "jellyfin", enabled: true, needsReconnect: false, refreshing: false }]
        let refresh = null
        tryVerify(function() { refresh = findChild(card, "homeServerRefresh_server1"); return refresh !== null && refresh.enabled })

        fixture.connections = [{ id: "server1", name: "Living room", provider: "jellyfin", enabled: false, needsReconnect: false, refreshing: false }]
        tryVerify(function() { refresh = findChild(card, "homeServerRefresh_server1"); return refresh !== null && !refresh.enabled })

        fixture.connections = [{ id: "server1", name: "Living room", provider: "jellyfin", enabled: true, needsReconnect: true, refreshing: false }]
        tryVerify(function() { refresh = findChild(card, "homeServerRefresh_server1"); return refresh !== null && !refresh.enabled })

        fixture.connections = [{ id: "server1", name: "Living room", provider: "jellyfin", enabled: true, needsReconnect: false, refreshing: true }]
        tryVerify(function() { refresh = findChild(card, "homeServerRefresh_server1"); return refresh !== null && !refresh.enabled })
    }
    function test_credentialsAndHttpConsent() {
        let panel = openPanel()
        credentials(panel, "http://server.local:8096")
        let submit = field(panel, "homeServerSubmit")
        compare(submit.enabled, false)
        panel.submit()
        compare(fixture.submits, 0)
        mouseClick(field(panel, "homeServerHttpConsent"))
        compare(submit.enabled, true)
        mouseClick(submit)
        compare(fixture.submits, 1)
        compare(fixture.received.provider, "jellyfin")
        compare(fixture.received.allowHttp, true)
        verify(fixture.received.hadPassword)
        compare(field(panel, "homeServerPassword").text, "")
        panel.close()
        tryCompare(panel, "visible", false)
        compare(fixture.cancels, 1)
    }
    function test_blankPasswordStillRequiresHttpConsent() {
        let panel = openPanel()
        field(panel, "homeServerAddress").text = "http://server.local:8096"
        field(panel, "homeServerUsername").text = "viewer"
        compare(field(panel, "homeServerPassword").text, "")
        compare(field(panel, "homeServerSubmit").enabled, false)
        panel.submit()
        compare(fixture.submits, 0)
        mouseClick(field(panel, "homeServerHttpConsent"))
        compare(field(panel, "homeServerSubmit").enabled, true)
        mouseClick(field(panel, "homeServerSubmit"))
        compare(fixture.submits, 1)
        compare(fixture.received.hadPassword, false)
        compare(fixture.received.allowHttp, true)
        compare(field(panel, "homeServerPassword").text, "")
        panel.close()
    }
    function test_passwordSingleLineSanitation_data() {
        return [
            { tag: "assigned_crlf", insert: false, raw: "\r\n  fake pass Ω\r\nword  \n", clean: "  fake pass Ωword  " },
            { tag: "inserted_crlf", insert: true, raw: "\r\n  fake pass Ω\r\nword  \n", clean: "  fake pass Ωword  " },
            { tag: "spaces_and_other_characters", insert: false, raw: "  fake\tpass Ω  ", clean: "  fake\tpass Ω  " }
        ]
    }
    function test_passwordSingleLineSanitation(data) {
        let panel = openPanel()
        field(panel, "homeServerAddress").text = "https://server.local"
        field(panel, "homeServerUsername").text = "viewer"
        fixture.expectedPassword = data.clean
        let password = field(panel, "homeServerPassword")
        if (data.insert) password.insert(0, data.raw)
        else password.text = data.raw
        compare(password.text, data.clean)
        mouseClick(field(panel, "homeServerSubmit"))
        compare(fixture.submits, 1)
        compare(fixture.received.passwordMatched, true)
        compare(password.text, "")
        panel.close()
    }
    function test_cardShowsUnavailableAndRemovalStatus() {
        let card = createTemporaryObject(cardComponent, testCase)
        verify(card !== null)
        fixture.available = false
        fixture.status = "Secure credential storage is unavailable."
        let status = findChild(card, "homeServerCardStatus")
        verify(status.visible)
        compare(status.text, fixture.status)
        compare(status.textFormat, Text.PlainText)
        compare(findChild(card, "homeServerConnectButton").enabled, false)
        fixture.available = true
        fixture.status = "Could not remove this connection."
        compare(status.text, fixture.status)
        fixture.status = ""
        compare(status.visible, false)
    }
    function test_providerAndPlex() {
        let panel = openPanel()
        credentials(panel, "https://server.local")
        let choice = field(panel, "homeServerProvider")
        choice.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(panel.provider, "emby")
        compare(field(panel, "homeServerPassword").text, "")
        keyClick(Qt.Key_Down)
        compare(panel.provider, "plex")
        compare(field(panel, "homeServerPassword").visible, false)
        verify(field(panel, "homeServerBeginPlex").visible)
        mouseClick(field(panel, "homeServerBeginPlex"))
        compare(fixture.plexStarts, 1)
        fixture.plexCode = "ABCD"
        compare(field(panel, "homeServerPlexCode").text, "ABCD")
        fixture.plexServers = [{ id: "plex1", name: "Family Plex", available: true }]
        fixture.httpConsentRequired = true
        let candidates = field(panel, "homeServerPlexCandidates")
        tryCompare(candidates, "count", 1)
        let pick = candidates.itemAt(0)
        verify(pick !== null)
        compare(pick.enabled, false)
        mouseClick(field(panel, "homeServerPlexHttpConsent"))
        compare(pick.enabled, true)
        mouseClick(pick)
        compare(fixture.selectedId, "plex1")
        compare(fixture.selectedHttp, true)
        panel.close()
    }
    function test_brandedTilesSelectAndPreserveSingleSelection() {
        let panel = openPanel()
        let tiles = field(panel, "homeServerProviderTiles")
        let jellyfin = tiles.itemAt(0)
        let emby = tiles.itemAt(1)
        let plex = tiles.itemAt(2)
        for (let i = 0; i < 3; ++i) {
            let provider = ["jellyfin", "emby", "plex"][i]
            tryCompare(findChild(tiles.itemAt(i), "homeServerProviderLogo_" + provider), "status", Image.Ready)
        }
        verify(jellyfin.checked)
        verify(jellyfin.height >= 44 && emby.height >= 44 && plex.height >= 44)
        mouseClick(jellyfin)
        verify(jellyfin.checked)
        jellyfin.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(panel.provider, "emby")
        verify(emby.activeFocus)
        keyClick(Qt.Key_Left)
        compare(panel.provider, "jellyfin")
        credentials(panel, "https://server.local")
        mouseClick(emby)
        compare(panel.provider, "emby")
        verify(emby.checked && !jellyfin.checked && !plex.checked)
        compare(field(panel, "homeServerPassword").text, "")
        mouseClick(plex)
        compare(panel.provider, "plex")
        verify(plex.checked && !emby.checked)
        compare(field(panel, "homeServerAddress").visible, false)
        compare(field(panel, "homeServerSubmit").visible, false)
        panel.close()
    }
    function test_statusPreservesFormProfileClearsIt() {
        let panel = openPanel()
        credentials(panel, "https://server.local")
        fixture.status = "Connecting"
        fixture.revision++
        fixture.changed()
        compare(field(panel, "homeServerPassword").text, "test-secret")
        verify(panel.visible)
        fixture.profileGeneration++
        fixture.changed()
        tryCompare(panel, "visible", false)
        compare(field(panel, "homeServerPassword").text, "")
    }
    function test_escapeCancelsAndClears() {
        let panel = openPanel()
        credentials(panel, "https://server.local")
        field(panel, "homeServerPassword").forceActiveFocus()
        keyClick(Qt.Key_Escape)
        tryCompare(panel, "visible", false)
        compare(field(panel, "homeServerPassword").text, "")
        compare(fixture.cancels, 1)
    }
    function test_backCancelsAndClears() {
        let panel = openPanel()
        credentials(panel, "https://server.local")
        field(panel, "homeServerPassword").forceActiveFocus()
        keyClick(Qt.Key_Back)
        tryCompare(panel, "visible", false)
        compare(field(panel, "homeServerPassword").text, "")
        compare(fixture.cancels, 1)
    }
    function test_serviceChangeCancelsOriginal() {
        let panel = openPanel()
        credentials(panel, "https://server.local")
        panel.service = null
        tryCompare(panel, "visible", false)
        compare(field(panel, "homeServerPassword").text, "")
        compare(fixture.cancels, 1)
    }
    function test_httpsDoesNotRequestHttpConsent() {
        let panel = openPanel()
        credentials(panel, "https://server.local")
        mouseClick(field(panel, "homeServerSubmit"))
        compare(fixture.submits, 1)
        compare(fixture.received.allowHttp, false)
        compare(field(panel, "homeServerPassword").text, "")
        mouseClick(field(panel, "homeServerCancel"))
        tryCompare(panel, "visible", false)
        compare(fixture.cancels, 1)
    }
    function test_smallViewport() {
        let testWindow = testCase.Window.window
        let oldWidth = testWindow.width
        let oldHeight = testWindow.height
        testWindow.width = 320
        testWindow.height = 480
        let panel = openPanel()
        credentials(panel, "http://server.local:8096")
        fixture.status = "The server could not complete sign-in.\nPlease try again."
        tryVerify(function() { return panel.width <= testWindow.width })
        tryVerify(function() { return panel.height <= testWindow.height })
        let cancel = field(panel, "homeServerCancel")
        let submit = field(panel, "homeServerSubmit")
        verify(cancel.height >= 44 && submit.height >= 44)
        verify(cancel.mapToItem(panel.contentItem, 0, cancel.height).y <= panel.contentItem.height)
        verify(submit.mapToItem(panel.contentItem, 0, submit.height).y <= panel.contentItem.height)
        let status = field(panel, "homeServerStatus")
        let statusY = status.mapToItem(panel.contentItem, 0, 0).y
        verify(statusY >= 0)
        verify(status.mapToItem(panel.contentItem, 0, status.height).y <= cancel.mapToItem(panel.contentItem, 0, 0).y)
        let scroll = field(panel, "homeServerFormScroll")
        tryVerify(function() { return scroll.contentHeight > scroll.availableHeight })
        scroll.contentItem.contentY = scroll.contentItem.contentHeight - scroll.contentItem.height
        let consent = field(panel, "homeServerHttpConsent")
        tryVerify(function() { return consent.mapToItem(scroll, 0, 0).y >= -1 })
        verify(consent.mapToItem(scroll, 0, consent.height).y <= scroll.height + 1)
        mouseClick(consent)
        compare(consent.checked, true)
        compare(status.mapToItem(panel.contentItem, 0, 0).y, statusY)
        panel.close()
        testWindow.width = oldWidth
        testWindow.height = oldHeight
    }
    function test_httpFailureFitsDesktopAndStaysInDialog() {
        let testWindow = testCase.Window.window
        let oldWidth = testWindow.width
        let oldHeight = testWindow.height
        testWindow.width = 1280
        testWindow.height = 720
        let card = createTemporaryObject(cardComponent, testCase)
        try {
            mouseClick(findChild(card, "homeServerConnectButton"))
            let loader = findChild(card, "homeServerPanelLoader")
            tryCompare(loader, "status", Loader.Ready)
            let panel = loader.item
            tryCompare(panel, "opened", true)
            credentials(panel, "http://server.local:8096")
            let consent = field(panel, "homeServerHttpConsent")
            mouseClick(consent)
            mouseClick(field(panel, "homeServerSubmit"))
            compare(fixture.submits, 1)
            fixture.status = "<b>The server could not complete sign-in.</b>\nPlease try again or check server access."
            fixture.changed()
            let scroll = field(panel, "homeServerFormScroll")
            tryVerify(function() { return scroll.contentHeight <= scroll.availableHeight + 1 })
            compare(scroll.contentItem.contentY, 0)
            for (let name of ["homeServerAddress", "homeServerUsername", "homeServerPassword", "homeServerHttpConsent"]) {
                let item = field(panel, name)
                verify(item.mapToItem(scroll, 0, 0).y >= -1, name + " top is visible")
                verify(item.mapToItem(scroll, 0, item.height).y <= scroll.height + 1, name + " bottom is visible")
            }
            let status = field(panel, "homeServerStatus")
            compare(status.parent, panel.contentItem)
            compare(status.text, fixture.status)
            compare(status.textFormat, Text.PlainText)
            verify(status.mapToItem(panel.contentItem, 0, 0).y >= 0)
            let cancel = field(panel, "homeServerCancel")
            let submit = field(panel, "homeServerSubmit")
            verify(status.mapToItem(panel.contentItem, 0, status.height).y <= cancel.mapToItem(panel.contentItem, 0, 0).y)
            verify(cancel.mapToItem(panel.contentItem, 0, cancel.height).y <= panel.contentItem.height + 1)
            verify(submit.mapToItem(panel.contentItem, 0, submit.height).y <= panel.contentItem.height + 1)
            compare(findChild(card, "homeServerCardStatus").visible, false)
            panel.close()
            tryCompare(card, "panelOpen", false)
            compare(findChild(card, "homeServerCardStatus").visible, true)
        } finally {
            card.panelOpen = false
            testWindow.width = oldWidth
            testWindow.height = oldHeight
        }
    }
}
