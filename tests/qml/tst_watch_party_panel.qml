// Watch Party reference bundle + QML visual Slice 04 — focused Player 1 readiness/recovery contract.
//
// This instantiates the production WatchPartyPanel with injected presentation fakes. It proves the
// source/signed-in/service gate mechanically: a generic direct source cannot enable Start, while
// the same UI with an eligible exact torrent can. No live player, account service, or room network
// participates in this Quick Test.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "WatchPartyPanel"
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
        property string phase: "idle"
        property bool busy: false
        property string errorCategory: ""
        property string errorText: ""
        property string noticeText: ""
        property bool inRoom: false
        property string roomId: ""
        property string localParticipantId: ""
        property bool localIsHost: false
        property string controlMode: "host"
        property var roomSource: ({})
        property bool localSourceReady: false
        property bool canInvite: false
        property bool canToggleControlMode: false
        property bool canEnd: false
        property bool canLeave: false
        property bool canChat: false
        property bool inviteBusy: false
        property bool hostGraceActive: false
        property var participants: []
        property var chatMessages: []
        property var reactions: []

        property int startCalls: 0
        property int catchUpCalls: 0
        property var lastStartSource: ({})

        function startParty(source) {
            startCalls += 1
            lastStartSource = source
            return true
        }
        function setSharedControl(enabled) { return true }
        function inviteExactUsername(username) { return true }
        function removeParticipant(participantId) { return true }
        function sendChat(message) { return true }
        function sendReaction(reaction) { return true }
        function catchUp() { catchUpCalls += 1; return true }
        function leaveParty() { return true }
        function endParty() { return true }
        function clearFeedback() {}
    }

    QtObject {
        id: syncController
        property bool catchUpAvailable: false
        property string syncStatus: "unknown"
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

    function eligibleTorrentCandidate() {
        return {
            "infoHash": "0123456789abcdef0123456789abcdef01234567",
            "fileIdx": 7,
            "addonId": "com.example.torrent"
        }
    }

    function unsupportedDirectCandidate() {
        return {
            "url": "https://cdn.example.test/video.mkv",
            "addonId": "com.example.direct"
        }
    }

    function unsupportedDirect() {
        return {
            "eligible": false,
            "eligibility": "unsupported",
            "reason": "direct_source_not_verified_debrid",
            "descriptor": ({})
        }
    }

    function enterHostControlRoom() {
        panel.sourceInfo = eligibleTorrent()
        panel.localSourceMatches = true
        controller.inRoom = true
        controller.phase = "in_room"
        controller.roomId = "WP-4N7K-2Q9M"
        controller.localParticipantId = "p-host"
        controller.localIsHost = true
        controller.controlMode = "host"
        controller.canInvite = true
        controller.canToggleControlMode = true
        controller.canEnd = true
        controller.canLeave = true
        controller.canChat = true
        controller.participants = [
            { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
              "host": true, "local": true, "connected": true, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-guest-1", "displayName": "Guest", "identityKind": "guest",
              "host": false, "local": false, "connected": true, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-guest-2", "displayName": "Guest 2", "identityKind": "guest",
              "host": false, "local": false, "connected": true, "ready": true, "syncStatus": "buffering" }
        ]
        controller.chatMessages = [
            { "displayName": "Hemanth56", "message": "Ready when you are." },
            { "displayName": "Guest", "message": "Yep." }
        ]
        wait(0)
    }

    function enterHostSharedRoom() {
        enterHostControlRoom()
        controller.controlMode = "shared"
        wait(0)
    }

    function enterParticipantRoom() {
        panel.sourceInfo = eligibleTorrent()
        panel.localSourceMatches = true
        controller.inRoom = true
        controller.phase = "in_room"
        controller.roomId = "WP-4N7K-2Q9M"
        controller.localParticipantId = "p-guest"
        controller.localIsHost = false
        controller.controlMode = "shared"
        controller.canInvite = false
        controller.canToggleControlMode = false
        controller.canEnd = false
        controller.canLeave = true
        controller.canChat = true
        controller.participants = [
            { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
              "host": true, "local": false, "connected": true, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-guest", "displayName": "Guest", "identityKind": "guest",
              "host": false, "local": true, "connected": true, "ready": true, "syncStatus": "inSync" }
        ]
        controller.chatMessages = [
            { "displayName": "Hemanth56", "message": "Ready when you are." },
            { "displayName": "Guest", "message": "Yep." }
        ]
        wait(0)
    }

    function enterSourceNotReadyParticipantRoom() {
        enterParticipantRoom()
        panel.localSourceMatches = false
        controller.localSourceReady = false
        syncController.catchUpAvailable = false
        syncController.syncStatus = "unknown"
        controller.participants = [
            { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
              "host": true, "local": false, "connected": true, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-guest", "displayName": "Guest", "identityKind": "guest",
              "host": false, "local": true, "connected": true, "ready": false, "syncStatus": "unknown" }
        ]
        wait(0)
    }

    function enterCatchUpParticipantRoom() {
        enterParticipantRoom()
        panel.localSourceMatches = true
        controller.localSourceReady = true
        syncController.catchUpAvailable = true
        syncController.syncStatus = "desynced"
        controller.participants = [
            { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
              "host": true, "local": false, "connected": true, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-guest", "displayName": "Guest", "identityKind": "guest",
              "host": false, "local": true, "connected": true, "ready": true, "syncStatus": "desynced" }
        ]
        wait(0)
    }

    function enterReconnectingParticipantRoom() {
        enterParticipantRoom()
        controller.phase = "reconnecting"
        controller.hostGraceActive = false
        wait(0)
    }

    function enterHostGraceParticipantRoom() {
        enterParticipantRoom()
        controller.phase = "active"
        controller.hostGraceActive = true
        controller.participants = [
            { "participantId": "p-host", "displayName": "Hemanth56", "identityKind": "account",
              "host": true, "local": false, "connected": false, "ready": true, "syncStatus": "inSync" },
            { "participantId": "p-guest", "displayName": "Guest", "identityKind": "guest",
              "host": false, "local": true, "connected": true, "ready": true, "syncStatus": "inSync" }
        ]
        wait(0)
    }

    function init() {
        controller.serviceConfigured = true
        controller.signedIn = true
        controller.inRoom = false
        controller.phase = "idle"
        controller.busy = false
        controller.roomId = ""
        controller.localParticipantId = ""
        controller.localIsHost = false
        controller.controlMode = "host"
        controller.canInvite = false
        controller.canToggleControlMode = false
        controller.canEnd = false
        controller.canLeave = false
        controller.canChat = false
        controller.inviteBusy = false
        controller.hostGraceActive = false
        controller.participants = []
        controller.chatMessages = []
        controller.startCalls = 0
        controller.catchUpCalls = 0
        syncController.catchUpAvailable = false
        syncController.syncStatus = "unknown"
        controller.lastStartSource = ({})
        panel = panelComponent.createObject(testWindow.contentItem, {
            // Player 1's real applet control sits near the bottom of the player chrome, leaving
            // room above for the popover to open upward. Anchoring the fixture there (instead of
            // the (0,0) default) matches that placement so panelEffectiveHeight does not collapse
            // against the window's top edge.
            "x": testWindow.width - 90,
            "y": testWindow.height - 120,
            "controller": controller,
            "syncController": syncController,
            "overlayParent": testWindow.contentItem,
            "panelOpen": true,
            "sourceInfo": unsupportedDirect(),
            "sourceCandidate": unsupportedDirectCandidate()
        })
        verify(panel !== null)
        wait(40)
    }

    function cleanup() {
        if (panel)
            panel.destroy()
        panel = null
    }

    function test_player_control_and_panel_are_named() {
        verify(findChild(testWindow.contentItem, "watchPartyPlayerControl") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyPanel") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyStart") !== null)
    }

    function test_slice01_visual_foundation_contract() {
        compare(panel.panelWidth, 430)
        compare(panel.panelMaxHeight, 590)
        compare(panel.panelRadius, 14)
        compare(panel.panelHeaderHeight, 50)
        compare(panel.panelInset, 14)
        compare(panel.panelSectionGap, 12)

        verify(findChild(testWindow.contentItem, "watchPartyPanelShadow") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyPanelArrow") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyScrollThumb") !== null)
    }


    function test_slice02_host_control_atlas_state() {
        enterHostControlRoom()

        compare(panel.hostControlState, true)
        compare(panel.participantCapacity, 12)
        compare(panel.participantRowHeight, 44)
        compare(panel.chatViewportMaxHeight, 100)

        var sourceCard = findChild(testWindow.contentItem, "watchPartySourceCard")
        var roomIdentity = findChild(testWindow.contentItem, "watchPartyRoomIdentity")
        var controlSection = findChild(testWindow.contentItem, "watchPartyControlSection")
        var inviteSection = findChild(testWindow.contentItem, "watchPartyInviteSection")
        var participantsSection = findChild(testWindow.contentItem, "watchPartyParticipantsSection")
        var chatSection = findChild(testWindow.contentItem, "watchPartyChatSection")
        var chatViewport = findChild(testWindow.contentItem, "watchPartyChatViewport")
        var roomActions = findChild(testWindow.contentItem, "watchPartyRoomActions")

        verify(sourceCard !== null && sourceCard.visible)
        verify(roomIdentity !== null && roomIdentity.visible)
        verify(controlSection !== null && controlSection.visible)
        verify(inviteSection !== null && inviteSection.visible)
        verify(participantsSection !== null && participantsSection.visible)
        verify(chatSection !== null && chatSection.visible)
        verify(chatViewport !== null && chatViewport.height <= panel.chatViewportMaxHeight)
        verify(roomActions !== null && roomActions.visible)

        compare(findChild(testWindow.contentItem, "watchPartyRoomId").text, "WP-4N7K-2Q9M")
        compare(findChild(testWindow.contentItem, "watchPartyHostControl").selected, true)
        compare(findChild(testWindow.contentItem, "watchPartySharedControl").selected, false)
        compare(findChild(testWindow.contentItem, "watchPartyEnd").visible, true)
        verify(findChild(testWindow.contentItem, "watchPartyReaction_like") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyReaction_laugh") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyReaction_wow") !== null)
    }

    function test_slice03_host_shared_control_atlas_state() {
        enterHostSharedRoom()

        compare(panel.hostSharedControlState, true)
        compare(panel.participantState, false)
        compare(panel.visualAuthorityState, "hostShared")
        compare(findChild(testWindow.contentItem, "watchPartyHostControl").selected, false)
        compare(findChild(testWindow.contentItem, "watchPartySharedControl").selected, true)
        compare(findChild(testWindow.contentItem, "watchPartyControlSection").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyInviteSection").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyEnd").visible, true)
    }

    function test_slice03_participant_atlas_state() {
        enterParticipantRoom()

        compare(panel.hostControlState, false)
        compare(panel.hostSharedControlState, false)
        compare(panel.participantState, true)
        compare(panel.visualAuthorityState, "participant")

        var controlSection = findChild(testWindow.contentItem, "watchPartyControlSection")
        var inviteSection = findChild(testWindow.contentItem, "watchPartyInviteSection")
        var endButton = findChild(testWindow.contentItem, "watchPartyEnd")
        var leaveButton = findChild(testWindow.contentItem, "watchPartyLeave")
        var roomActions = findChild(testWindow.contentItem, "watchPartyRoomActions")
        var chatSection = findChild(testWindow.contentItem, "watchPartyChatSection")

        verify(controlSection !== null)
        verify(inviteSection !== null)
        verify(endButton !== null)
        compare(controlSection.visible, false)
        compare(inviteSection.visible, false)
        compare(endButton.visible, false)
        compare(leaveButton.visible, true)
        compare(leaveButton.width, roomActions.width)
        compare(chatSection.visible, true)
        verify(findChild(testWindow.contentItem, "watchPartyReaction_like") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyReaction_laugh") !== null)
        verify(findChild(testWindow.contentItem, "watchPartyReaction_wow") !== null)

        compare(panel.participantTitle(controller.participants[0]), "Hemanth56  ·  Host")
        compare(panel.participantTitle(controller.participants[1]), "Guest  ·  You")
    }

    function test_slice03_participant_hides_stale_host_authority() {
        enterParticipantRoom()

        // Presentation must fail closed if controller capability flags lag behind local role.
        controller.canInvite = true
        controller.canToggleControlMode = true
        controller.canEnd = true
        wait(0)

        compare(findChild(testWindow.contentItem, "watchPartyControlSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyInviteSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyEnd").visible, false)
    }

    function test_slice04_source_not_ready_atlas_state() {
        enterSourceNotReadyParticipantRoom()

        compare(panel.sourceNotReadyState, true)
        compare(panel.catchUpState, false)
        compare(panel.visualReadinessState, "sourceNotReady")

        var sourceMismatch = findChild(testWindow.contentItem, "watchPartySourceMismatch")
        var mismatchMessage = findChild(testWindow.contentItem, "watchPartySourceMismatchMessage")
        var catchRow = findChild(testWindow.contentItem, "watchPartyCatchUpRow")
        verify(sourceMismatch !== null)
        verify(mismatchMessage !== null)
        verify(catchRow !== null)
        compare(sourceMismatch.visible, true)
        compare(catchRow.visible, false)
        compare(mismatchMessage.text, "This player is not on the room's exact source yet. Open the room source before playback can sync.")
        compare(panel.participantMeta(controller.participants[1]), "Guest · Not ready")

        compare(findChild(testWindow.contentItem, "watchPartyControlSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyInviteSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyEnd").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyLeave").visible, true)
    }

    function test_slice04_catch_up_atlas_state() {
        enterCatchUpParticipantRoom()

        compare(panel.sourceNotReadyState, false)
        compare(panel.catchUpState, true)
        compare(panel.visualReadinessState, "catchUp")

        var sourceMismatch = findChild(testWindow.contentItem, "watchPartySourceMismatch")
        var catchRow = findChild(testWindow.contentItem, "watchPartyCatchUpRow")
        var catchMessage = findChild(testWindow.contentItem, "watchPartyCatchUpMessage")
        var catchButton = findChild(testWindow.contentItem, "watchPartyCatchUp")
        verify(sourceMismatch !== null)
        verify(catchRow !== null)
        verify(catchMessage !== null)
        verify(catchButton !== null)
        compare(sourceMismatch.visible, false)
        compare(catchRow.visible, true)
        compare(catchMessage.text, "You're out of sync with the room.")
        compare(catchButton.visible, true)
        compare(catchButton.enabled, true)
        compare(panel.participantMeta(controller.participants[1]), "Guest · Out of sync")

        // Let the scene graph sync the geometry produced by enterCatchUpParticipantRoom()'s
        // participant-list/state rebuild before hit-testing a click against it (wait(0) alone
        // is one event-loop turn, not a render sync).
        wait(60)
        mouseClick(catchButton)
        compare(controller.catchUpCalls, 1)
    }

    function test_slice04_catch_up_fails_closed_without_exact_source() {
        enterCatchUpParticipantRoom()
        panel.localSourceMatches = false
        wait(0)

        compare(panel.sourceNotReadyState, true)
        compare(panel.catchUpState, false)
        compare(findChild(testWindow.contentItem, "watchPartySourceMismatch").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyCatchUpRow").visible, false)

        panel.runCatchUp()
        compare(controller.catchUpCalls, 0)
    }

    function test_slice05_reconnecting_atlas_state() {
        enterReconnectingParticipantRoom()

        compare(panel.reconnectingState, true)
        compare(panel.hostGraceState, false)
        compare(panel.visualLifecycleState, "reconnecting")

        var banner = findChild(testWindow.contentItem, "watchPartyLifecycleBanner")
        var message = findChild(testWindow.contentItem, "watchPartyLifecycleMessage")
        verify(banner !== null)
        verify(message !== null)
        compare(banner.visible, true)
        compare(banner.stateCategory, "reconnecting")
        compare(message.text, "Reconnecting to the Watch Party… playback sync will resume from authoritative room state.")

        // Reconnection preserves the room surface instead of visually resetting membership.
        compare(findChild(testWindow.contentItem, "watchPartySourceCard").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyRoomIdentity").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyParticipantsSection").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyChatSection").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyLeave").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyControlSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyInviteSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyEnd").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyRoomId").text, "WP-4N7K-2Q9M")
    }

    function test_slice05_host_grace_atlas_state() {
        enterHostGraceParticipantRoom()

        compare(panel.reconnectingState, false)
        compare(panel.hostGraceState, true)
        compare(panel.visualLifecycleState, "hostGrace")

        var banner = findChild(testWindow.contentItem, "watchPartyLifecycleBanner")
        var message = findChild(testWindow.contentItem, "watchPartyLifecycleMessage")
        verify(banner !== null)
        verify(message !== null)
        compare(banner.visible, true)
        compare(banner.stateCategory, "hostGrace")
        compare(message.text, "The host is reconnecting. The room stays active during the short recovery window.")
        compare(panel.participantMeta(controller.participants[0]), "Signed in · Reconnecting")
        compare(panel.participantMeta(controller.participants[1]), "Guest · In sync")

        compare(findChild(testWindow.contentItem, "watchPartySourceCard").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyRoomIdentity").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyParticipantsSection").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyChatSection").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyLeave").visible, true)
        compare(findChild(testWindow.contentItem, "watchPartyControlSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyInviteSection").visible, false)
        compare(findChild(testWindow.contentItem, "watchPartyEnd").visible, false)
    }

    function test_slice05_local_reconnect_takes_precedence_over_host_grace() {
        enterParticipantRoom()
        controller.hostGraceActive = true
        controller.phase = "reconnecting"
        wait(0)

        compare(panel.reconnectingState, true)
        compare(panel.hostGraceState, false)
        compare(panel.visualLifecycleState, "reconnecting")
        compare(findChild(testWindow.contentItem, "watchPartyLifecycleBanner").stateCategory, "reconnecting")
        compare(findChild(testWindow.contentItem, "watchPartyLifecycleMessage").text,
                "Reconnecting to the Watch Party… playback sync will resume from authoritative room state.")
    }

    function test_slice05_lifecycle_flags_fail_closed_outside_membership() {
        enterParticipantRoom()
        controller.inRoom = false
        controller.hostGraceActive = true
        controller.phase = "reconnecting"
        wait(0)

        compare(panel.reconnectingState, false)
        compare(panel.hostGraceState, false)
        compare(panel.visualLifecycleState, "none")
        compare(findChild(testWindow.contentItem, "watchPartyLifecycleBanner").visible, false)
    }

    function test_unsupported_direct_source_cannot_enable_start() {
        var start = findChild(testWindow.contentItem, "watchPartyStart")
        verify(start !== null)

        // Non-vacuous negative control: the same control becomes enabled after the
        // source mutation in test_eligible_torrent_enables_start().
        compare(panel.sourceEligible, false)
        compare(panel.startAvailable, false)
        compare(start.enabled, false)

        mouseClick(start)
        compare(controller.startCalls, 0)
    }

    function test_eligible_torrent_enables_start() {
        panel.sourceInfo = eligibleTorrent()
        panel.sourceCandidate = eligibleTorrentCandidate()
        wait(0)

        var start = findChild(testWindow.contentItem, "watchPartyStart")
        verify(start !== null)
        compare(panel.sourceEligible, true)
        compare(panel.startAvailable, true)
        compare(start.enabled, true)

        mouseClick(start)
        compare(controller.startCalls, 1)
        compare(controller.lastStartSource.infoHash,
                "0123456789abcdef0123456789abcdef01234567")
        compare(controller.lastStartSource.fileIdx, 7)
        verify(controller.lastStartSource.descriptor === undefined)
    }

    function test_signed_out_or_unconfigured_host_cannot_start() {
        panel.sourceInfo = eligibleTorrent()
        controller.signedIn = false
        wait(0)
        compare(panel.startAvailable, false)
        compare(findChild(testWindow.contentItem, "watchPartyStart").enabled, false)

        controller.signedIn = true
        controller.serviceConfigured = false
        wait(0)
        compare(panel.startAvailable, false)
        compare(findChild(testWindow.contentItem, "watchPartyStart").enabled, false)
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
}
