import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import "SystemFocusContainment.js" as SystemFocusContainment

Popup {
    id: sheet
    objectName: "watchPartyJoinSheet"

    property var controller: null
    property Item focusReturnItem: null
    readonly property bool signedIn: controller ? !!controller.signedIn : false
    readonly property bool inRoom: controller ? !!controller.inRoom : false
    readonly property string phase: controller ? String(controller.phase || "idle") : "idle"

    // Polished join-sheet atlas metrics (approved mockup: colosseum-watch-party-polished-mock.html).
    readonly property int atlasHorizontalPadding: 30
    readonly property int atlasTopPadding: 26
    readonly property int atlasBottomPadding: 30
    readonly property real atlasScrimOpacity: 0.58
    readonly property string visualJoinState:
        inRoom ? "joined" : (!signedIn ? "guest" : "signedIn")

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(650, (parent ? parent.width : 1280) - 48)
    // Use the Popup's OWN padding, not a manually-positioned contentItem. A Popup
    // ignores its contentItem's x/y and stretches it to the content rect, so the
    // old "column at x:30/y:26 + padding baked into height" approach pinned every
    // child to the top-left and dumped the whole top+bottom padding budget at the
    // bottom as a dead band. Real insets fix it: implicitHeight = content + top +
    // bottom, availableHeight == the column's natural height, so no stretch, no void.
    leftPadding: atlasHorizontalPadding
    rightPadding: atlasHorizontalPadding
    topPadding: atlasTopPadding
    bottomPadding: atlasBottomPadding
    height: implicitHeight

    function rememberInvoker() {
        const windowObject = parent ? parent.Window.window : null
        const active = windowObject ? windowObject.activeFocusItem : null
        if (active && !SystemFocusContainment.isWithin(active, contentColumn))
            focusReturnItem = active
    }

    function restoreInvoker() {
        const target = focusReturnItem
        focusReturnItem = null
        Qt.callLater(function() {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus()
        })
    }

    function focusInitial() {
        Qt.callLater(function() {
            if (!sheet.opened)
                return
            if (!sheet.inRoom) {
                roomInput.forceActiveFocus()
                return
            }
            closeButton.forceActiveFocus()
        })
    }

    function openSheet() {
        rememberInvoker()
        if (controller && controller.refreshIdentity)
            controller.refreshIdentity()
        roomInput.text = ""
        guestInput.text = ""
        if (controller && controller.clearFeedback)
            controller.clearFeedback()
        open()
    }

    onOpened: focusInitial()
    onClosed: restoreInvoker()

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

    Shortcut {
        sequence: "Tab"
        enabled: sheet.opened
        onActivated: {
            const windowObject = sheet.parent ? sheet.parent.Window.window : null
            SystemFocusContainment.move(windowObject, contentColumn, true)
        }
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: sheet.opened
        onActivated: {
            const windowObject = sheet.parent ? sheet.parent.Window.window : null
            SystemFocusContainment.move(windowObject, contentColumn, false)
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, sheet.atlasScrimOpacity)
    }

    background: Rectangle {
        objectName: "watchPartyJoinSurface"
        radius: 22
        border.width: 1
        border.color: theme.edge
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(12 / 255, 15 / 255, 24 / 255, 0.985) }
            GradientStop { position: 1.0; color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.988) }
        }

        // No drop shadow: a black MultiEffect shadow over the near-black home
        // screen paints a dark smudge below the card that reads as dead sheet,
        // not depth. The 0.58 scrim + 1px edge border carry the separation.

        // 1px top inner highlight — mirrors the mockup's inset top hairline.
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
            height: 1
            color: Qt.rgba(1, 1, 1, 0.05)
        }
    }

    Theme { id: theme }

    contentItem: Column {
        id: contentColumn
        // The Popup positions this at (leftPadding, topPadding) and sizes it to the
        // content rect; children bind to parent.width (== availableWidth). No manual
        // x/y/width — those would be ignored and reintroduce the padding-at-bottom bug.
        spacing: 0

        Keys.priority: Keys.AfterItem
        Keys.onPressed: function(event) {
            if (event.key !== Qt.Key_Tab)
                return
            const forward = !(event.modifiers & Qt.ShiftModifier)
            const windowObject = sheet.parent ? sheet.parent.Window.window : null
            if (SystemFocusContainment.move(windowObject, contentColumn, forward))
                event.accepted = true
        }

        Row {
            id: headerRow
            objectName: "watchPartyJoinHeader"
            width: parent.width
            height: 56
            spacing: 16

            Item {
                id: partyMark
                width: 56
                height: 56

                Rectangle {
                    anchors.fill: parent
                    radius: 15
                    border.width: 1
                    border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.28)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.10) }
                        GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.035) }
                    }
                }

                WatchPartyGlyph {
                    anchors.centerIn: parent
                    kind: "party"
                    glyphSize: 31
                    strokeColor: theme.gold
                    strokeWidth: 1.7
                }
            }

            Text {
                id: headerTitle
                width: headerRow.width - partyMark.width - closeButton.width - (headerRow.spacing * 2)
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.inRoom ? "Watch Party joined" : "Join Watch Party"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 37
                font.weight: Font.Normal
                font.letterSpacing: -0.7
                elide: Text.ElideRight
            }

            Item {
                id: closeButton
                objectName: "watchPartyJoinClose"
                width: 42
                height: 42
                anchors.verticalCenter: parent.verticalCenter
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Close Join Watch Party"

                Rectangle {
                    anchors.fill: parent
                    radius: 21
                    color: closeButton.activeFocus
                        ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.12)
                        : (closeMouse.containsMouse ? theme.glassTint : Qt.rgba(1, 1, 1, 0.035))
                    border.width: closeButton.activeFocus ? 2 : 1
                    border.color: closeButton.activeFocus
                        ? theme.gold
                        : (closeMouse.containsMouse ? theme.edge : Qt.rgba(1, 1, 1, 0.12))
                }

                WatchPartyGlyph {
                    anchors.centerIn: parent
                    kind: "close"
                    glyphSize: 18
                    strokeColor: theme.inkDim
                    strokeWidth: 1.8
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

        Item { width: parent.width; height: 28 }

        Column {
            id: roomFieldBlock
            objectName: "watchPartyJoinRoomField"
            visible: !sheet.inRoom
            width: parent.width
            height: roomFieldBlock.visible ? implicitHeight : 0
            spacing: 9

            Text {
                text: "ROOM ID"
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1.7
            }

            Rectangle {
                width: parent.width
                height: 76
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.055)
                border.width: 1
                border.color: roomInput.activeFocus ? theme.gold : Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.72)

                TextInput {
                    id: roomInput
                    objectName: "watchPartyJoinRoomId"
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.ink
                    selectionColor: theme.gold
                    selectedTextColor: "#111111"
                    font.family: theme.ui
                    font.pixelSize: 18
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
        }

        Item { id: spacerAfterRoom; width: parent.width; visible: !sheet.inRoom; height: spacerAfterRoom.visible ? 14 : 0 }

        Rectangle {
            id: guestWrap
            objectName: "watchPartyJoinGuestField"
            visible: !sheet.inRoom && !sheet.signedIn
            width: parent.width
            height: guestWrap.visible ? (guestColumn.implicitHeight + 29) : 0
            radius: 15
            color: Qt.rgba(1, 1, 1, 0.025)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.10)

            Column {
                id: guestColumn
                x: 15
                y: 14
                width: parent.width - 30
                spacing: 9

                Text {
                    text: "DISPLAY NAME FOR THIS ROOM"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.7
                }

                Rectangle {
                    width: parent.width
                    height: 52
                    radius: 11
                    color: Qt.rgba(1, 1, 1, 0.045)
                    border.width: 1
                    border.color: guestInput.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.12)

                    TextInput {
                        id: guestInput
                        objectName: "watchPartyJoinGuestName"
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        verticalAlignment: TextInput.AlignVCenter
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
            }
        }

        Item {
            id: spacerAfterGuest
            width: parent.width
            visible: !sheet.inRoom && !sheet.signedIn
            height: spacerAfterGuest.visible ? 18 : 0
        }

        Flow {
            id: factsRow
            objectName: "watchPartyJoinPrivacy"
            visible: !sheet.inRoom
            width: parent.width
            height: factsRow.visible ? implicitHeight : 0
            spacing: 9

            WatchPartyFactPill { kind: "lock"; label: "Private" }
            WatchPartyFactPill { kind: "people"; label: "2–12 people" }
            WatchPartyFactPill { kind: "play"; label: "Synced playback" }
            WatchPartyFactPill { kind: "download"; label: "Fetches the room's source" }
        }

        Item { id: spacerBeforeDivider; width: parent.width; visible: !sheet.inRoom; height: spacerBeforeDivider.visible ? 22 : 0 }

        Rectangle {
            id: divider
            visible: !sheet.inRoom
            width: parent.width
            height: divider.visible ? 1 : 0
            color: Qt.rgba(1, 1, 1, 0.12)
        }

        Item { id: spacerAfterDivider; width: parent.width; visible: !sheet.inRoom; height: spacerAfterDivider.visible ? 22 : 0 }

        SheetButton {
            objectName: "watchPartyJoinSubmit"
            visible: !sheet.inRoom && sheet.controller && sheet.controller.serviceConfigured
            width: parent.width
            height: visible ? 40 : 0
            label: sheet.controller && sheet.controller.busy ? "Joining…" : "Join Room"
            primary: true
            enabled: sheet.canSubmitJoin()
            onClicked: sheet.submitJoin()
        }

        Rectangle {
            id: statusCard
            objectName: "watchPartyJoinStatusCard"
            visible: !sheet.inRoom && sheet.controller && !sheet.controller.serviceConfigured
            width: parent.width
            height: statusCard.visible ? Math.max(82, statusColumn.implicitHeight + 32) : 0
            radius: 14
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.105)
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.055) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.035) }
            }

            Item {
                id: statusMark
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 38
                height: 38

                Rectangle {
                    anchors.fill: parent
                    radius: 19
                    color: Qt.rgba(1, 1, 1, 0.035)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.15)
                }

                WatchPartyGlyph {
                    anchors.centerIn: parent
                    kind: "info"
                    glyphSize: 17
                    strokeColor: theme.inkDim
                    strokeWidth: 1.8
                }
            }

            Column {
                id: statusColumn
                x: statusMark.x + statusMark.width + 14
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - 18
                spacing: 5

                Text {
                    width: parent.width
                    text: "Watch Party is unavailable in this build."
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: "Room joining will appear here when the service is configured."
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 13
                    lineHeight: 1.4
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle {
            id: joinedCard
            objectName: "watchPartyJoinJoinedCard"
            property bool playbackReady: false
            visible: sheet.inRoom
            width: parent.width
            height: joinedCard.visible ? (joinedColumn.implicitHeight + 24) : 0
            radius: 14
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

        Item {
            id: spacerBeforeError
            width: parent.width
            visible: sheet.controller && String(sheet.controller.errorText || "").length > 0
            height: spacerBeforeError.visible ? 14 : 0
        }

        Text {
            id: errorText
            objectName: "watchPartyJoinError"
            visible: sheet.controller
                     && String(sheet.controller.errorText || "").length > 0
            width: parent.width
            height: errorText.visible ? implicitHeight : 0
            text: sheet.controller ? sheet.controller.errorText : ""
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        Item { id: spacerBeforeActions; width: parent.width; visible: sheet.inRoom; height: spacerBeforeActions.visible ? 14 : 0 }

        Row {
            id: actionsRow
            objectName: "watchPartyJoinActions"
            visible: sheet.inRoom
            width: parent.width
            height: actionsRow.visible ? implicitHeight : 0
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

    // WatchPartyGlyph — house vector-glyph idiom (see qml/account/AccountSecurityIcon.qml):
    // Item + Shape scaled to glyphSize, one ShapePath per glyph "kind" with the inactive
    // paths stroked transparent. Transcribed from the approved mockup's inline SVGs.
    component WatchPartyGlyph: Item {
        id: glyphRoot

        property string kind: "party"
        property color strokeColor: theme.gold
        property real strokeWidth: 1.7
        property real glyphSize: 24
        readonly property real viewBoxSize: kind === "party" ? 32 : 24

        implicitWidth: glyphSize
        implicitHeight: glyphSize

        Shape {
            anchors.centerIn: parent
            width: glyphRoot.viewBoxSize
            height: glyphRoot.viewBoxSize
            scale: glyphRoot.glyphSize / glyphRoot.viewBoxSize
            antialiasing: true

            // party (people+) — header badge glyph, viewBox 32
            ShapePath {
                strokeColor: glyphRoot.kind === "party" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M8 11a4 4 0 1 0 8 0 4 4 0 1 0-8 0" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "party" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M19.3 12.5a3.2 3.2 0 1 0 6.4 0 3.2 3.2 0 1 0-6.4 0" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "party" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M4.5 25c.7-5.1 3.4-7.6 7.7-7.6 4.4 0 7.1 2.5 7.8 7.6" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "party" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M18.5 20.1c1.2-1.7 2.9-2.5 5.1-2.5 3.1 0 5 1.9 5.4 5.7" }
            }

            // close (×)
            ShapePath {
                strokeColor: glyphRoot.kind === "close" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M6 6l12 12M18 6L6 18" }
            }

            // lock (private)
            ShapePath {
                strokeColor: glyphRoot.kind === "lock" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M5.5 10.5h13a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "lock" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M8.5 10.5V8a3.5 3.5 0 0 1 7 0v2.5" }
            }

            // people (2-12)
            ShapePath {
                strokeColor: glyphRoot.kind === "people" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M6 8.5a3 3 0 1 0 6 0 3 3 0 1 0-6 0" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "people" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M14 10a2.5 2.5 0 1 0 5 0 2.5 2.5 0 1 0-5 0" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "people" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M3.8 19c.5-4 2.3-6 5.4-6 3.2 0 5 2 5.5 6M14.1 14.3c.7-.8 1.7-1.2 3-1.2 2.4 0 3.7 1.6 4.1 4.8" }
            }

            // synced playback (filled triangle)
            ShapePath {
                strokeColor: "transparent"
                fillColor: glyphRoot.kind === "play" ? glyphRoot.strokeColor : "transparent"
                PathSvg { path: "M8 5.8v12.4L18 12z" }
            }

            // download (fetches the room's source — arrow into tray)
            ShapePath {
                strokeColor: glyphRoot.kind === "download" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M12 4v10m0 0l-4-4m4 4l4-4" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "download" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M5 17h14" }
            }

            // info (status card)
            ShapePath {
                strokeColor: glyphRoot.kind === "info" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M3.5 12a8.5 8.5 0 1 0 17 0 8.5 8.5 0 1 0-17 0" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "info" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M12 10.5v5" }
            }
            ShapePath {
                strokeColor: glyphRoot.kind === "info" ? glyphRoot.strokeColor : "transparent"
                strokeWidth: glyphRoot.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M12 7.2h.01" }
            }
        }
    }

    // WatchPartyFactPill — the "Private / 2–12 people / Synced playback" glass pills.
    component WatchPartyFactPill: Rectangle {
        id: pill
        property string kind: "lock"
        property string label: ""

        width: pillRow.implicitWidth + 30
        height: 40
        radius: 20
        color: Qt.rgba(1, 1, 1, 0.025)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.105)

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 9

            WatchPartyGlyph {
                anchors.verticalCenter: parent.verticalCenter
                kind: pill.kind
                glyphSize: 16
                strokeColor: theme.gold
                strokeWidth: 1.7
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pill.label
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
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
