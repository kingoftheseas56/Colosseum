import QtQuick
import QtQuick.Controls
import QtTest
import "../../../qml" as Colosseum

TestCase {
    id: testCase
    name: "HomeServerCard"
    width: 880
    height: 740
    visible: true
    when: windowShown

    QtObject {
        id: fakeController
        property bool available: true
        property bool busy: false
        property bool refreshing: false
        property string refreshingId: ""
        property int refreshCalls: 0
        property bool hasError: false
        property string notice: ""
        property string profileId: "local"
        property var connections: []
        property string lastPassword: ""
        signal profileChanged()
        signal statusChanged()

        function connectServer(kind, name, address, user, password) {
            lastPassword = password
            connections = [{id: "one", name: name || "Home",
                            kind: kind === "jellyfin" ? "Jellyfin" : "Emby",
                            address: address, enabled: true}]
            notice = "Connected to your server."
            statusChanged()
            return true
        }
        function cancelConnect() { busy = false; statusChanged() }
        function cancelRefresh() { refreshing = false; refreshingId = ""; statusChanged() }
        function refreshConnection(id) { ++refreshCalls; return true }
        function setConnectionEnabled(id, enabled) {
            var changed = connections.slice()
            changed[0] = {id: id, name: changed[0].name,
                          kind: changed[0].kind, address: changed[0].address,
                          enabled: enabled}
            connections = changed
            return true
        }
        function removeConnection(id) {
            connections = []
            return true
        }
    }

    Colosseum.HomeServerConnectionsCard {
        id: card
        controller: fakeController
        width: 840
        height: implicitHeight
        anchors.centerIn: parent
    }

    function init() {
        fakeController.lastPassword = ""
        fakeController.notice = ""
        fakeController.connections = []
        fakeController.available = true
        fakeController.busy = false
        fakeController.refreshing = false
        fakeController.refreshingId = ""
        fakeController.refreshCalls = 0
        fakeController.statusChanged()
        card.expanded = false
        card.clearInputs()
    }

    function test_empty_state_and_profile_switch() {
        verify(findChild(card, "trackerHomeServerAddButton") !== null)
        var add = findChild(card, "trackerHomeServerAddButton")
        mouseClick(add)
        compare(card.expanded, true)
        findChild(card, "trackerHomeServerPassword").text = "sensitive"
        fakeController.profileId = "other"
        fakeController.profileChanged()
        compare(card.expanded, false)
        compare(findChild(card, "trackerHomeServerPassword").text, "")
    }

    function test_refresh_button_and_cancel() {
        fakeController.connections = [{
            id: "one", name: "Home", kind: "Jellyfin",
            address: "https://media.example", enabled: true,
            indexedCount: 4, matchedCount: 3, lastRefreshMs: 1800000000000
        }]
        var refresh = findChild(card, "trackerHomeServerRefreshButton")
        verify(refresh !== null)
        verify(refresh.enabled)
        mouseClick(refresh)
        compare(fakeController.refreshCalls, 1)
        fakeController.refreshing = true
        fakeController.refreshingId = "one"
        fakeController.statusChanged()
        compare(refresh.text, "Cancel refresh")
        mouseClick(refresh)
        compare(fakeController.refreshing, false)
        compare(fakeController.refreshCalls, 1)
    }

    function test_toggle_and_confirmed_removal() {
        fakeController.connections = [{
            id: "one", name: "Home", kind: "Jellyfin",
            address: "https://media.example", enabled: true
        }]
        var toggle = findChild(card, "trackerHomeServerEnabledSwitch")
        verify(toggle !== null)
        compare(toggle.checked, true)
        mouseClick(toggle)
        compare(fakeController.connections[0].enabled, false)

        var remove = findChild(card, "trackerHomeServerRemoveButton")
        verify(remove !== null)
        mouseClick(remove)
        compare(fakeController.connections.length, 1)
        var dialog = findChild(card, "trackerHomeServerRemoveDialog")
        verify(dialog !== null)
        verify(dialog.visible)
        dialog.accept()
        compare(fakeController.connections.length, 0)
    }

    function test_signin_clears_password_and_adds_row() {
        var add = findChild(card, "trackerHomeServerAddButton")
        mouseClick(add)
        findChild(card, "trackerHomeServerAddress").text = "https://media.example"
        findChild(card, "trackerHomeServerUsername").text = "alice"
        findChild(card, "trackerHomeServerPassword").text = "pw123"
        var submit = findChild(card, "trackerHomeServerConnectButton")
        verify(submit.enabled)
        mouseClick(submit)
        compare(fakeController.lastPassword, "pw123")
        compare(findChild(card, "trackerHomeServerPassword").text, "")
        compare(fakeController.connections.length, 1)
        compare(card.expanded, false)
    }
}
