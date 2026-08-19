// Deterministic Watch Party visual harness. Test/reference only, never production UI.
import QtQuick 2.15
import "../../qml" as Colosseum

Item {
    id: harness
    objectName: "watchPartyVisualHarness"
    width: 1440
    height: 900

    property string visualState: "hostControl"
    property bool showPanel: true
    property bool localSourceMatches: true
    property var sourceInfo: eligibleTorrent()
    property alias watchPartyPanel: partyPanel

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

    function normalParticipants(localHost) {
        if (localHost) {
            return [
                { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
                  "host": true, "local": true, "connected": true, "ready": true, "syncStatus": "inSync" },
                { "participantId": "p-guest", "displayName": "Guest", "identityKind": "guest",
                  "host": false, "local": false, "connected": true, "ready": true, "syncStatus": "inSync" },
                { "participantId": "p-guest-2", "displayName": "Guest 2", "identityKind": "guest",
                  "host": false, "local": false, "connected": true, "ready": true, "syncStatus": "buffering" }
            ]
        }
        return [
            { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
              "host": true, "local": false, "connected": true, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-local", "displayName": "Guest", "identityKind": "guest",
              "host": false, "local": true, "connected": true, "ready": true, "syncStatus": "inSync" }
        ]
    }

    function resetFixture() {
        localSourceMatches = true
        sourceInfo = eligibleTorrent()
        fakeController.serviceConfigured = true
        fakeController.signedIn = true
        fakeController.signedInUsername = "Hemanth56"
        fakeController.phase = "active"
        fakeController.busy = false
        fakeController.errorCategory = ""
        fakeController.errorText = ""
        fakeController.noticeText = ""
        fakeController.inRoom = true
        fakeController.roomId = "WP-4N7K-2Q9M"
        fakeController.localParticipantId = "p-host"
        fakeController.localIsHost = true
        fakeController.controlMode = "host"
        fakeController.roomSource = sourceInfo.descriptor
        fakeController.localSourceReady = true
        fakeController.canInvite = true
        fakeController.canToggleControlMode = true
        fakeController.canEnd = true
        fakeController.canLeave = true
        fakeController.canChat = true
        fakeController.inviteBusy = false
        fakeController.hostGraceActive = false
        fakeController.participants = normalParticipants(true)
        fakeController.chatMessages = [
            { "displayName": "Hemanth56", "message": "Ready when you are." },
            { "displayName": "Guest", "message": "Yep." }
        ]
        fakeController.reactions = []
        fakeSync.catchUpAvailable = false
        fakeSync.syncStatus = "inSync"
    }

    function applyState(state) {
        resetFixture()
        visualState = state
        if (state === "hostShared") {
            fakeController.controlMode = "shared"
        } else if (state === "participant") {
            fakeController.localParticipantId = "p-local"
            fakeController.localIsHost = false
            fakeController.controlMode = "shared"
            fakeController.canInvite = false
            fakeController.canToggleControlMode = false
            fakeController.canEnd = false
            fakeController.participants = normalParticipants(false)
        } else if (state === "sourceNotReady") {
            applyState("participant")
            visualState = state
            localSourceMatches = false
            fakeController.localSourceReady = false
            fakeController.participants = [
                fakeController.participants[0],
                { "participantId": "p-local", "displayName": "Guest", "identityKind": "guest",
                  "host": false, "local": true, "connected": true, "ready": false, "syncStatus": "unknown" }
            ]
            fakeSync.syncStatus = "unknown"
        } else if (state === "catchUp") {
            applyState("participant")
            visualState = state
            fakeSync.catchUpAvailable = true
            fakeSync.syncStatus = "desynced"
            fakeController.participants = [
                fakeController.participants[0],
                { "participantId": "p-local", "displayName": "Guest", "identityKind": "guest",
                  "host": false, "local": true, "connected": true, "ready": true, "syncStatus": "desynced" }
            ]
        } else if (state === "reconnecting") {
            applyState("participant")
            visualState = state
            fakeController.phase = "reconnecting"
        } else if (state === "hostGrace") {
            applyState("participant")
            visualState = state
            fakeController.hostGraceActive = true
            fakeController.participants = [
                { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
                  "host": true, "local": false, "connected": false, "ready": true, "syncStatus": "inSync" },
                fakeController.participants[1]
            ]
        }
        partyPanel.positionPanel()
    }

    function applyOverflowFixture() {
        resetFixture()
        visualState = "overflow"
        fakeController.localIsHost = false
        fakeController.localParticipantId = "p-local"
        fakeController.canInvite = false
        fakeController.canToggleControlMode = false
        fakeController.canEnd = false
        var people = []
        for (var i = 0; i < 12; ++i) {
            people.push({
                "participantId": "p-" + i,
                "displayName": i === 0 ? "A_very_long_room_participant_name_that_must_elide_cleanly" : "Guest " + (i + 1),
                "identityKind": i === 0 ? "account" : "guest",
                "host": i === 0,
                "local": i === 1,
                "connected": true,
                "ready": true,
                "syncStatus": i % 3 === 0 ? "buffering" : "inSync"
            })
        }
        fakeController.participants = people
        var messages = []
        for (var j = 0; j < 18; ++j) {
            messages.push({
                "displayName": j % 2 === 0 ? "A_very_long_room_participant_name_that_must_elide_cleanly" : "Guest",
                "message": "A deliberately long Watch Party message used to force deterministic chat overflow without changing the panel height. Message " + (j + 1) + "."
            })
        }
        fakeController.chatMessages = messages
        partyPanel.positionPanel()
    }

    QtObject {
        id: fakeController
        property bool serviceConfigured: true
        property bool signedIn: true
        property string signedInUsername: "Hemanth56"
        property string phase: "active"
        property bool busy: false
        property string errorCategory: ""
        property string errorText: ""
        property string noticeText: ""
        property bool inRoom: true
        property string roomId: "WP-4N7K-2Q9M"
        property string localParticipantId: "p-host"
        property bool localIsHost: true
        property string controlMode: "host"
        property var roomSource: ({})
        property bool localSourceReady: true
        property bool canInvite: true
        property bool canToggleControlMode: true
        property bool canEnd: true
        property bool canLeave: true
        property bool canChat: true
        property bool inviteBusy: false
        property bool hostGraceActive: false
        property var participants: []
        property var chatMessages: []
        property var reactions: []
        function startParty(source) { return true }
        function setSharedControl(enabled) { controlMode = enabled ? "shared" : "host"; return true }
        function inviteExactUsername(username) { return username.length > 0 }
        function removeParticipant(participantId) { return participantId.length > 0 }
        function sendChat(message) { return message.length > 0 }
        function sendReaction(reaction) { return reaction.length > 0 }
        function catchUp() { return true }
        function leaveParty() { return true }
        function endParty() { return true }
        function clearFeedback() {}
    }

    QtObject {
        id: fakeSync
        property bool catchUpAvailable: false
        property string syncStatus: "inSync"
    }

    Rectangle {
        objectName: "watchPartyHarnessPlayerSurface"
        anchors.fill: parent
        color: "#050506"

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(1, 1, 1, 0.025)
        }
    }

    Rectangle {
        id: bottomDock
        objectName: "watchPartyHarnessBottomDock"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(164, Math.max(112, harness.height * 0.18))
        color: Qt.rgba(0.025, 0.026, 0.035, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
    }

    Row {
        id: rightCluster
        objectName: "watchPartyHarnessRightCluster"
        anchors.right: parent.right
        anchors.rightMargin: 54
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(26, bottomDock.height * 0.22)
        spacing: 6

        Rectangle { width: 40; height: 40; radius: 20; color: Qt.rgba(1, 1, 1, 0.06) }

        Colosseum.WatchPartyPanel {
            id: partyPanel
            overlayParent: harness
            controller: fakeController
            syncController: fakeSync
            sourceInfo: harness.sourceInfo
            localSourceMatches: harness.localSourceMatches
            panelOpen: harness.showPanel
            onToggleRequested: function(wasOpen) { partyPanel.panelOpen = !wasOpen }
        }

        Rectangle { width: 48; height: 48; radius: 24; color: Qt.rgba(1, 1, 1, 0.06) }
        Rectangle { width: 48; height: 48; radius: 24; color: Qt.rgba(1, 1, 1, 0.06) }
        Rectangle { width: 44; height: 40; radius: 12; color: Qt.rgba(1, 1, 1, 0.06) }
        Rectangle { width: 48; height: 48; radius: 24; color: Qt.rgba(1, 1, 1, 0.06) }
    }

    Component.onCompleted: {
        applyState(visualState)
        partyPanel.positionPanel()
    }
}
