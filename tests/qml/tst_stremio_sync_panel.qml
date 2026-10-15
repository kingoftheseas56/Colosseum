import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "StremioSyncPanel"
    when: windowShown

    QtObject {
        id: fakeState
        property string status: "notConnected"
        property int pendingCount: 0
        property double lastSuccessAt: 0
        property string accountDisplayName: ""
        property string lastResultSummary: ""
        property bool linkedAccount: false
        property int connectCalls: 0
        property int switchCalls: 0
        property int disconnectCalls: 0
        function connectAccount() { connectCalls++; return true }
        function switchAccount() { switchCalls++; return true }
        function disconnectCurrentProfile() { disconnectCalls++; return true }
    }
    QtObject {
        id: fakeActions
        property int syncCalls: 0
        function syncStremioNow() { syncCalls++; return true }
    }
    Window {
        id: window
        width: 900; height: 700
        visible: true
        Colosseum.StremioSyncPanel {
            id: panel
            anchors.fill: parent
            shown: true
            syncState: fakeState
            actions: fakeActions
        }
    }

    function findChild(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], name)
            if (found) return found
        }
        return null
    }
    function focusableFace(root) {
        if (!root) return null
        if (root.focusPolicy !== undefined && root.focusPolicy !== Qt.NoFocus) return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = focusableFace(children[i])
            if (found) return found
        }
        return null
    }
    function trigger(name) {
        var item = findChild(panel, name)
        verify(item !== null)
        item.triggered()
    }
    function init() {
        fakeState.status = "notConnected"
        fakeState.linkedAccount = false
        fakeState.accountDisplayName = ""
        fakeState.lastResultSummary = ""
        fakeState.connectCalls = 0
        fakeState.switchCalls = 0
        fakeState.disconnectCalls = 0
        fakeActions.syncCalls = 0
        panel.confirmation = ""
        panel.shown = true
        wait(0)
    }
    function test_connect_reconnect_and_sync_actions() {
        trigger("stremioSyncPrimaryAction")
        compare(fakeState.connectCalls, 1)
        fakeState.status = "reconnectRequired"
        fakeState.linkedAccount = true
        trigger("stremioSyncPrimaryAction")
        compare(fakeState.connectCalls, 2)
        fakeState.status = "synced"
        trigger("stremioSyncPrimaryAction")
        compare(fakeActions.syncCalls, 1)
    }
    function test_switch_and_disconnect_require_confirmation() {
        fakeState.status = "synced"
        fakeState.linkedAccount = true
        trigger("stremioSwitchAccountAction")
        compare(panel.confirmation, "switch")
        trigger("stremioSyncPrimaryAction")
        compare(fakeState.switchCalls, 1)
        trigger("stremioDisconnectAction")
        compare(panel.confirmation, "disconnect")
        trigger("stremioSyncPrimaryAction")
        compare(fakeState.disconnectCalls, 1)
    }
    function test_result_is_presented_without_provider_secrets() {
        fakeState.status = "synced"
        fakeState.accountDisplayName = "Harbor User"
        fakeState.lastResultSummary = "Sync complete · 4 library items · 2 addons"
        var result = findChild(panel, "stremioSyncResult")
        verify(result !== null)
        compare(result.visible, true)
        compare(result.text, fakeState.lastResultSummary)
        verify(result.text.indexOf("authKey") < 0)
    }
    function test_paused_profile_has_no_dead_primary_action() {
        fakeState.status = "paused"
        var primary = findChild(panel, "stremioSyncPrimaryAction")
        verify(primary !== null)
        compare(primary.enabled, false)
    }
}
