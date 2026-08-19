// Watch Party Slice 6 — focused accountless/signed-in join sheet contract.
//
// The production sheet is driven with an injected presentation fake. The test proves that signed-
// out users need only Room ID + temporary room-local name, while signed-in users do not provide a
// client username. It deliberately does not fake source acquisition or Player 1 navigation.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "WatchPartyJoinSheet"
    when: windowShown

    Window {
        id: testWindow
        width: 720
        height: 620
        visible: true
    }

    QtObject {
        id: controller

        property bool signedIn: false
        property bool inRoom: false
        property bool busy: false
        property bool serviceConfigured: true
        property string phase: "idle"
        property string errorText: ""
        property string roomId: ""
        property var roomSource: ({})
        property bool canLeave: false

        property int joinCalls: 0
        property string lastRoomId: ""
        property string lastGuestName: ""
        property int refreshCalls: 0
        property int clearCalls: 0

        function refreshIdentity() { refreshCalls += 1 }
        function clearFeedback() { clearCalls += 1 }
        function joinRoom(roomIdValue, guestNameValue) {
            joinCalls += 1
            lastRoomId = roomIdValue
            lastGuestName = guestNameValue
            return true
        }
        function leaveParty() { return true }
    }

    Component {
        id: sheetComponent
        Colosseum.WatchPartyJoinSheet {}
    }

    property var sheet: null

    function init() {
        controller.signedIn = false
        controller.inRoom = false
        controller.busy = false
        controller.serviceConfigured = true
        controller.errorText = ""
        controller.roomId = ""
        controller.roomSource = ({})
        controller.canLeave = false
        controller.joinCalls = 0
        controller.lastRoomId = ""
        controller.lastGuestName = ""
        controller.refreshCalls = 0
        controller.clearCalls = 0

        sheet = sheetComponent.createObject(testWindow.contentItem, {
            "controller": controller,
            "x": 140,
            "y": 70
        })
        verify(sheet !== null)
        sheet.openSheet()
        wait(40)
    }

    function cleanup() {
        if (sheet) {
            sheet.close()
            sheet.destroy()
        }
        sheet = null
    }

    function test_open_refreshes_identity_and_exposes_named_controls() {
        compare(controller.refreshCalls, 1)
        compare(controller.clearCalls, 1)
        verify(findChild(sheet.contentItem, "watchPartyJoinRoomId") !== null)
        verify(findChild(sheet.contentItem, "watchPartyJoinGuestName") !== null)
        verify(findChild(sheet.contentItem, "watchPartyJoinSubmit") !== null)
    }

    function test_accountless_join_requires_room_and_temporary_name() {
        var room = findChild(sheet.contentItem, "watchPartyJoinRoomId")
        var guest = findChild(sheet.contentItem, "watchPartyJoinGuestName")
        var submit = findChild(sheet.contentItem, "watchPartyJoinSubmit")
        verify(room !== null)
        verify(guest !== null)
        verify(submit !== null)

        room.text = "room-guest"
        wait(0)
        compare(submit.enabled, false)

        guest.text = "Temporary Guest"
        wait(0)
        compare(submit.enabled, true)

        mouseClick(submit)
        compare(controller.joinCalls, 1)
        compare(controller.lastRoomId, "room-guest")
        compare(controller.lastGuestName, "Temporary Guest")
    }

    function test_signed_in_join_never_collects_client_username() {
        controller.signedIn = true
        wait(0)

        var room = findChild(sheet.contentItem, "watchPartyJoinRoomId")
        var guest = findChild(sheet.contentItem, "watchPartyJoinGuestName")
        var submit = findChild(sheet.contentItem, "watchPartyJoinSubmit")
        verify(room !== null)
        verify(guest !== null)
        verify(submit !== null)
        compare(guest.visible, false)

        room.text = "room-signed-in"
        wait(0)
        compare(submit.enabled, true)

        mouseClick(submit)
        compare(controller.joinCalls, 1)
        compare(controller.lastRoomId, "room-signed-in")
        compare(controller.lastGuestName, "")
    }

    function test_unconfigured_service_never_enables_join() {
        var room = findChild(sheet.contentItem, "watchPartyJoinRoomId")
        var guest = findChild(sheet.contentItem, "watchPartyJoinGuestName")
        var submit = findChild(sheet.contentItem, "watchPartyJoinSubmit")

        room.text = "room-guest"
        guest.text = "Temporary Guest"
        controller.serviceConfigured = false
        wait(0)

        compare(submit.enabled, false)
        mouseClick(submit)
        compare(controller.joinCalls, 0)
    }

    function test_slice06_guest_join_atlas_state() {
        compare(sheet.width, 650)
        compare(sheet.visualJoinState, "guest")
        compare(findChild(sheet.contentItem, "watchPartyJoinHeader").visible, true)
        compare(findChild(sheet.contentItem, "watchPartyJoinRoomField").visible, true)
        compare(findChild(sheet.contentItem, "watchPartyJoinGuestField").visible, true)
        compare(findChild(sheet.contentItem, "watchPartyJoinPrivacy").visible, true)
        compare(findChild(sheet.contentItem, "watchPartyJoinJoinedCard").visible, false)
        compare(findChild(sheet.contentItem, "watchPartyJoinActions").visible, false)
    }

    function test_slice06_signed_in_join_atlas_state() {
        controller.signedIn = true
        wait(0)
        compare(sheet.visualJoinState, "signedIn")
        compare(findChild(sheet.contentItem, "watchPartyJoinRoomField").visible, true)
        compare(findChild(sheet.contentItem, "watchPartyJoinGuestField").visible, false)
        compare(findChild(sheet.contentItem, "watchPartyJoinPrivacy").visible, true)
    }

    function test_slice06_joined_membership_does_not_imply_playback_ready() {
        controller.inRoom = true
        controller.roomId = "WP-4N7K-2Q9M"
        controller.roomSource = ({ "kind": "torrent" })
        controller.canLeave = true
        wait(0)

        compare(sheet.visualJoinState, "joined")
        compare(findChild(sheet.contentItem, "watchPartyJoinRoomField").visible, false)
        compare(findChild(sheet.contentItem, "watchPartyJoinGuestField").visible, false)
        compare(findChild(sheet.contentItem, "watchPartyJoinJoinedCard").visible, true)
        compare(findChild(sheet.contentItem, "watchPartyJoinActions").visible, true)
        verify(findChild(sheet.contentItem, "watchPartyJoinJoinedCard").playbackReady === false)
    }

    function test_slice06_direct_submit_fails_closed_when_service_unconfigured() {
        var room = findChild(sheet.contentItem, "watchPartyJoinRoomId")
        var guest = findChild(sheet.contentItem, "watchPartyJoinGuestName")
        room.text = "WP-4N7K-2Q9M"
        guest.text = "Guest"
        controller.serviceConfigured = false
        wait(0)

        sheet.submitJoin()
        compare(controller.joinCalls, 0)
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
