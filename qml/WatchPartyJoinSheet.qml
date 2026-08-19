import QtQuick
import QtQuick.Controls

Popup {
    id: sheet
    objectName: "watchPartyJoinSheet"

    property var controller: null
    readonly property bool signedIn: controller ? !!controller.signedIn : false
    readonly property bool inRoom: controller ? !!controller.inRoom : false
    readonly property string phase: controller ? String(controller.phase || "idle") : "idle"

    // Slice 06: stable atlas metrics for the outside-player Join composition.
    readonly property int atlasWidth: 430
    readonly property int atlasHorizontalPadding: 24
    readonly property int atlasTopPadding: 22
    readonly property int atlasBottomPadding: 24
    readonly property int atlasGap: 13
    readonly property int atlasHeaderHeight: 34
    readonly property int atlasFieldHeight: 66
    readonly property int atlasFieldRadius: 11
    readonly property real atlasScrimOpacity: 0.58
    readonly property string visualJoinState:
        inRoom ? "joined" : (!signedIn ? "guest" : "signedIn")

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: atlasWidth
    height: contentColumn.implicitHeight + atlasTopPadding + atlasBottomPadding
    padding: 0

    function openSheet() {
        if (controller && controller.refreshIdentity)
            controller.refreshIdentity()
        roomInput.text = ""
        guestInput.text = ""
        if (controller && controller.clearFeedback)
            controller.clearFeedback()
        open()
        roomInput.forceActiveFocus()
    }

    function canSubmitJoin() {
        if (!controller || controller.busy || !controller.serviceConfigured || inRoom)
            return false
        var roomId = roomInput.text.trim()
        if (roomId.length === 0)
            return false
        if (!signedIn && guestInput.text.trim().length === 0)
            return false
        return true
    }

    function submitJoin() {
        if (!canSubmitJoin())
            return
        var roomId = roomInput.text.trim()
        if (roomId.length === 0)
            return
        var guestName = signedIn ? "" : guestInput.text.trim()
        if (!signedIn && guestName.length === 0)
            return
        controller.joinRoom(roomId, guestName)
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, sheet.atlasScrimOpacity)
    }

    background: Rectangle {
        objectName: "watchPartyJoinSurface"
        radius: 16
        color: Qt.rgba(13 / 255, 13 / 255, 18 / 255, 0.985)
        border.width: 1
        border.color: theme.edge
    }

    Theme { id: theme }

    contentItem: Column {
        id: contentColumn
        x: sheet.atlasHorizontalPadding
        y: sheet.atlasTopPadding
        width: sheet.width - (sheet.atlasHorizontalPadding * 2)
        // Popup.padding is 0 (this sheet positions itself manually via x/y above),
        // so QQuickPopup stretches contentItem to the full available box instead of
        // padding it down — without an explicit height, the Column still PACKS its
        // children correctly at implicitHeight but then sits inside that oversized
        // box, leaving the surplus as dead space below the last child instead of
        // real bottom padding. Pinning height to implicitHeight makes the Column
        // hug its own content regardless of what Popup tries to stretch it to.
        height: implicitHeight
        spacing: sheet.atlasGap

        Row {
            objectName: "watchPartyJoinHeader"
            width: parent.width
            height: sheet.atlasHeaderHeight

            Text {
                width: parent.width - closeButton.width
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: sheet.inRoom ? "Watch Party joined" : "Join Watch Party"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 23
            }

            Item {
                id: closeButton
                objectName: "watchPartyJoinClose"
                width: 34
                height: 34
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Close Join Watch Party"

                Rectangle {
                    anchors.fill: parent
                    radius: 17
                    color: closeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 22
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.close()
                }
                Keys.onReturnPressed: sheet.close()
                Keys.onEnterPressed: sheet.close()
                Keys.onSpacePressed: sheet.close()
            }
        }

        Rectangle {
            objectName: "watchPartyJoinRoomField"
            visible: !sheet.inRoom
            width: parent.width
            height: sheet.atlasFieldHeight
            radius: sheet.atlasFieldRadius
            color: Qt.rgba(1, 1, 1, 0.055)
            border.width: 1
            border.color: roomInput.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.11)

            Text {
                x: 12
                y: 8
                text: "ROOM ID"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                font.letterSpacing: 1.2
            }

            TextInput {
                id: roomInput
                objectName: "watchPartyJoinRoomId"
                x: 12
                y: 27
                width: parent.width - 24
                height: 30
                color: theme.ink
                selectionColor: theme.gold
                selectedTextColor: "#111111"
                font.family: theme.ui
                font.pixelSize: 15
                clip: true
                activeFocusOnTab: true
                maximumLength: 128
                Accessible.name: "Watch Party Room ID"
                onAccepted: {
                    if (sheet.signedIn)
                        sheet.submitJoin()
                    else
                        guestInput.forceActiveFocus()
                }
            }
        }

        Rectangle {
            objectName: "watchPartyJoinGuestField"
            visible: !sheet.inRoom && !sheet.signedIn
            width: parent.width
            height: sheet.atlasFieldHeight
            radius: sheet.atlasFieldRadius
            color: Qt.rgba(1, 1, 1, 0.055)
            border.width: 1
            border.color: guestInput.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.11)

            Text {
                x: 12
                y: 8
                text: "TEMPORARY DISPLAY NAME"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                font.letterSpacing: 1.2
            }

            TextInput {
                id: guestInput
                objectName: "watchPartyJoinGuestName"
                x: 12
                y: 27
                width: parent.width - 24
                height: 30
                color: theme.ink
                selectionColor: theme.gold
                selectedTextColor: "#111111"
                font.family: theme.ui
                font.pixelSize: 15
                clip: true
                activeFocusOnTab: true
                maximumLength: 80
                Accessible.name: "Temporary Watch Party display name"
                onAccepted: sheet.submitJoin()
            }
        }

        Text {
            objectName: "watchPartyJoinPrivacy"
            visible: !sheet.inRoom
            width: parent.width
            text: "Private room · 2–12 people · playback coordination only"
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
        }

        SheetButton {
            objectName: "watchPartyJoinSubmit"
            visible: !sheet.inRoom
            width: parent.width
            label: sheet.controller && sheet.controller.busy ? "Joining…" : "Join Room"
            primary: true
            enabled: sheet.canSubmitJoin()
            onClicked: sheet.submitJoin()
        }

        Text {
            visible: !sheet.inRoom
                     && sheet.controller
                     && !sheet.controller.serviceConfigured
            width: parent.width
            text: "Watch Party service is not configured in this build."
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Rectangle {
            objectName: "watchPartyJoinJoinedCard"
            property bool playbackReady: false
            visible: sheet.inRoom
            width: parent.width
            height: joinedColumn.implicitHeight + 24
            radius: 11
            color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
            border.width: 1
            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.26)

            Column {
                id: joinedColumn
                x: 12
                y: 12
                width: parent.width - 24
                spacing: 6

                Text {
                    width: parent.width
                    text: "Room " + (sheet.controller ? sheet.controller.roomId : "")
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Text {
                    width: parent.width
                    text: {
                        var src = sheet.controller ? sheet.controller.roomSource : ({})
                        if (src && src.kind === "torrent")
                            return "The room requires its exact torrent source. Open that source in Player 1 to become playback-ready."
                        if (src && src.kind === "debrid")
                            return "The room requires its verified debrid source. Open that source in Player 1 to become playback-ready."
                        return "The room source is not ready on this client yet."
                    }
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
        }

        Text {
            objectName: "watchPartyJoinError"
            visible: sheet.controller
                     && String(sheet.controller.errorText || "").length > 0
            width: parent.width
            text: sheet.controller ? sheet.controller.errorText : ""
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        Row {
            objectName: "watchPartyJoinActions"
            visible: sheet.inRoom
            width: parent.width
            spacing: 8

            SheetButton {
                width: (parent.width - 8) / 2
                label: "Close"
                onClicked: sheet.close()
            }

            SheetButton {
                objectName: "watchPartyJoinLeave"
                width: (parent.width - 8) / 2
                label: "Leave Room"
                danger: true
                enabled: sheet.controller && sheet.controller.canLeave
                onClicked: {
                    if (sheet.controller && sheet.controller.leaveParty())
                        sheet.close()
                }
            }
        }
    }

    component SheetButton: Rectangle {
        id: button
        property string label: ""
        property bool primary: false
        property bool danger: false
        signal clicked()

        height: 40
        radius: 10
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: label

        color: !enabled
               ? Qt.rgba(1, 1, 1, 0.035)
               : primary
                 ? theme.gold
                 : buttonMouse.containsMouse
                   ? Qt.rgba(1, 1, 1, 0.12)
                   : Qt.rgba(1, 1, 1, 0.07)
        border.width: primary ? 0 : 1
        border.color: activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.11)
        opacity: enabled ? 1.0 : 0.45

        Text {
            anchors.centerIn: parent
            text: button.label
            color: (button.primary && button.enabled) ? "#111111" : theme.ink
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            enabled: button.enabled
            hoverEnabled: true
            cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: button.clicked()
        }

        Keys.onReturnPressed: if (enabled) button.clicked()
        Keys.onEnterPressed: if (enabled) button.clicked()
        Keys.onSpacePressed: if (enabled) button.clicked()
    }
}
