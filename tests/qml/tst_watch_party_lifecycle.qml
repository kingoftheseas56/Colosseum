// Watch Party Slice 7 — focused recovery/failure presentation states.
//
// Production WatchPartyPanel is driven through scalar presentation fakes only. No live room,
// account, network, or player owner participates. Exact state transitions are asserted directly;
// waits exist only to flush QML bindings, never to infer success from elapsed time.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "WatchPartyLifecyclePresentation"
    when: windowShown

    Window {
        id: testWindow
        width: 760
        height: 720
        visible: true
    }

    QtObject {
        id: controller

        property bool serviceConfigured: true
        property bool signedIn: true
        property string signedInUsername: "Host"
        property string phase: "active"
        property bool busy: false
        property string errorCategory: ""
        property string errorText: ""
        property string noticeText: ""
        property bool inRoom: true
        property string roomId: "room-test"
        property string localParticipantId: "local"
        property bool localIsHost: false
        property string controlMode: "host"
        property var roomSource: ({
            "kind": "torrent",
            "infoHash": "0123456789abcdef0123456789abcdef01234567",
            "fileIdx": 7
        })
        property bool localSourceReady: true
        property bool canInvite: false
        property bool canToggleControlMode: false
        property bool canEnd: false
        property bool canLeave: true
        property bool canChat: true
        property bool inviteBusy: false
        property bool hostGraceActive: false
        property var participants: []
        property var chatMessages: []
        property var reactions: []

        function setSharedControl(enabled) { return true }
        function inviteExactUsername(username) { return true }
        function removeParticipant(participantId) { return true }
        function sendChat(message) { return true }
        function sendReaction(reaction) { return true }
        function catchUp() { return true }
        function leaveParty() { return true }
        function endParty() { return true }
        function clearFeedback() {}
    }

    QtObject {
        id: syncController
        property bool catchUpAvailable: false
        property string syncStatus: "inSync"
    }

    Component {
        id: panelComponent
        Colosseum.WatchPartyPanel {}
    }

    property var panel: null

    function eligibleTorrent() {
        return {
            "eligible": true,
            "eligibility": "torrent",
            "descriptor": {
                "kind": "torrent",
                "infoHash": "0123456789abcdef0123456789abcdef01234567",
                "fileIdx": 7
            }
        }
    }

    function init() {
        controller.phase = "active"
        controller.errorCategory = ""
        controller.errorText = ""
        controller.noticeText = ""
        controller.inRoom = true
        controller.hostGraceActive = false

        panel = panelComponent.createObject(testWindow.contentItem, {
            "controller": controller,
            "syncController": syncController,
            "overlayParent": testWindow.contentItem,
            "panelOpen": true,
            "sourceInfo": eligibleTorrent(),
            "localSourceMatches": true
        })
        verify(panel !== null)
        wait(0)
    }

    function cleanup() {
        if (panel)
            panel.destroy()
        panel = null
    }

    function test_reconnect_banner_is_exact_and_disappears_on_recovery() {
        var banner = findChild(testWindow.contentItem, "watchPartyLifecycleBanner")
        verify(banner !== null)
        compare(banner.visible, false)

        controller.phase = "reconnecting"
        wait(0)
        compare(banner.visible, true)
        compare(banner.stateCategory, "reconnecting")

        controller.phase = "active"
        wait(0)
        compare(banner.visible, false)
        compare(banner.stateCategory, "")
    }

    function test_host_grace_has_distinct_recovery_state() {
        var banner = findChild(testWindow.contentItem, "watchPartyLifecycleBanner")
        verify(banner !== null)

        controller.hostGraceActive = true
        wait(0)
        compare(banner.visible, true)
        compare(banner.stateCategory, "hostGrace")

        controller.hostGraceActive = false
        wait(0)
        compare(banner.visible, false)
    }

    function test_typed_failure_text_is_presented_without_state_guessing() {
        controller.errorCategory = "roomFull"
        controller.errorText = "This Watch Party is full."
        wait(0)
        compare(controller.errorCategory, "roomFull")
        verify(findText(testWindow.contentItem, "This Watch Party is full.") !== null)

        controller.errorCategory = "roomNotFound"
        controller.errorText = "That Watch Party does not exist."
        wait(0)
        compare(controller.errorCategory, "roomNotFound")
        verify(findText(testWindow.contentItem, "That Watch Party does not exist.") !== null)

        controller.errorCategory = "roomEnded"
        controller.errorText = "That Watch Party has ended."
        wait(0)
        compare(controller.errorCategory, "roomEnded")
        verify(findText(testWindow.contentItem, "That Watch Party has ended.") !== null)

        controller.errorCategory = "protocolVersionMismatch"
        controller.errorText = "Watch Party protocol versions do not match."
        wait(0)
        compare(controller.errorCategory, "protocolVersionMismatch")
        verify(findText(testWindow.contentItem, "Watch Party protocol versions do not match.") !== null)
    }

    function findChild(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findChild(kids[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function findText(root, value) {
        if (!root)
            return null
        if (root.text !== undefined && String(root.text) === value)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findText(kids[i], value)
            if (found)
                return found
        }
        return null
    }
}
