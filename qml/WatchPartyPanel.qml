pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: party

    width: 40
    height: 40

    property var controller: null
    property var syncController: null
    property var sourceInfo: ({})
    property Item overlayParent: null
    property bool panelOpen: false
    property bool localSourceMatches: true

    // Slice 01 visual foundation: these are presentation metrics only. They mirror the approved
    // HTML atlas and are deliberately exposed so QuickTest / visual harnesses can assert the
    // surface without reaching into anonymous child geometry. Behavior remains controller-owned.
    readonly property int panelWidth: 430
    readonly property int panelMaxHeight: 590
    readonly property int panelRadius: 14
    readonly property int panelHeaderHeight: 50
    readonly property int panelInset: 14
    readonly property int panelSectionGap: 12
    // Slice 07 Player 1 integration metrics. The atlas stays 430x590 at normal player sizes,
    // but the popover must remain physically contained when Player 1 is narrow or short.
    readonly property int panelEdgeMargin: 10
    readonly property int panelControlGap: 10
    readonly property int panelArrowInset: 25
    readonly property real panelEffectiveWidth:
        overlayParent ? Math.min(panelWidth, Math.max(1, overlayParent.width - panelEdgeMargin * 2))
                      : panelWidth
    readonly property real panelEffectiveHeight:
        overlayParent ? Math.min(panelMaxHeight,
                                 Math.max(1, Math.min(overlayParent.height - panelEdgeMargin * 2,
                                                     controlTopInOverlay() - panelControlGap - panelEdgeMargin)))
                      : panelMaxHeight
    // Slice 02 host-state metrics: copied from the approved Host / Host Control atlas state.
    // These remain presentation facts only; capacity/authority still come from the controller.
    readonly property int participantCapacity: 12
    readonly property int participantRowHeight: 44
    readonly property int chatViewportMaxHeight: 100
    readonly property color panelFill: Qt.rgba(10 / 255, 13 / 255, 18 / 255, 0.96)
    readonly property color panelEdge: Qt.rgba(1, 1, 1, 0.14)

    readonly property bool sourceEligible: !!(sourceInfo && sourceInfo.eligible === true)
    readonly property string sourceEligibility:
        sourceInfo && sourceInfo.eligibility ? String(sourceInfo.eligibility) : "unsupported"
    readonly property string phase: controller ? String(controller.phase || "idle") : "idle"
    readonly property bool inRoom: controller ? !!controller.inRoom : false
    readonly property bool localIsHost: controller ? !!controller.localIsHost : false
    // Slice 03 authority presentation. These are derived visual states only; the controller remains
    // the authority owner. QML additionally fails closed on local role so stale capability flags
    // cannot briefly expose host-only controls to a participant.
    readonly property bool hostAuthorityVisible: inRoom && localIsHost
    readonly property bool hostControlState:
        hostAuthorityVisible && controller && String(controller.controlMode || "host") === "host"
    readonly property bool hostSharedControlState:
        hostAuthorityVisible && controller && String(controller.controlMode || "host") === "shared"
    readonly property bool participantState: inRoom && !localIsHost
    readonly property string visualAuthorityState:
        hostControlState ? "hostControl"
                         : hostSharedControlState ? "hostShared"
                                                  : participantState ? "participant" : "none"
    readonly property bool startAvailable:
        controller && controller.serviceConfigured && controller.signedIn
        && sourceEligible && !controller.inRoom && !controller.busy
    readonly property bool catchUpAvailable:
        syncController && !!syncController.catchUpAvailable
    readonly property string syncStatus:
        syncController ? String(syncController.syncStatus || "unknown") : "unknown"
    // Slice 04 readiness/recovery presentation. Room membership is deliberately independent from
    // local playback readiness: an exact-source mismatch keeps the participant in-room but blocks
    // sync recovery. Catch Up is only exposed once the exact room source is active and the sync
    // controller explicitly reports a recoverable desync.
    readonly property bool sourceNotReadyState:
        inRoom && !localSourceMatches
    readonly property bool catchUpState:
        inRoom && localSourceMatches && catchUpAvailable && syncStatus === "desynced"
    readonly property string visualReadinessState:
        !inRoom ? "none"
                : sourceNotReadyState ? "sourceNotReady"
                                      : catchUpState ? "catchUp" : "ready"
    readonly property bool reconnecting:
        controller && String(controller.phase || "") === "reconnecting"
    readonly property bool hostGraceActive:
        controller && !!controller.hostGraceActive
    // Slice 05 connection-lifecycle presentation. Transport recovery preserves membership, but
    // lifecycle visuals fail closed when membership is gone. A local reconnect has precedence over
    // host grace because it is the more immediate condition affecting this client. Host grace is a
    // participant-only presentation state; a local host never sees a banner claiming "the host" is
    // reconnecting while their own transport is the recovering seam.
    readonly property bool reconnectingState:
        inRoom && reconnecting
    readonly property bool hostGraceState:
        inRoom && participantState && hostGraceActive && !reconnectingState
    readonly property string visualLifecycleState:
        !inRoom ? "none"
                : reconnectingState ? "reconnecting"
                                    : hostGraceState ? "hostGrace" : "steady"

    signal toggleRequested(bool wasOpen)
    signal copyRequested(string text)

    onXChanged: if (party.panelOpen) party.positionPanel()
    onYChanged: if (party.panelOpen) party.positionPanel()
    onWidthChanged: if (party.panelOpen) party.positionPanel()
    onHeightChanged: if (party.panelOpen) party.positionPanel()

    Connections {
        target: party.overlayParent
        enabled: party.overlayParent !== null
        function onWidthChanged() { party.positionPanel() }
        function onHeightChanged() { party.positionPanel() }
    }

    function clampX(v, lo, hi) {
        return Math.max(lo, Math.min(hi, v))
    }

    function controlCenterInOverlay() {
        if (!overlayParent)
            return Qt.point(party.width / 2, party.height / 2)
        // Explicit geometry reads keep this binding/reposition seam reactive as Player 1 resizes.
        var geometryTick = party.x + party.y + party.width + party.height
                         + overlayParent.width + overlayParent.height
        return party.mapToItem(overlayParent, party.width / 2, party.height / 2)
    }

    function controlTopInOverlay() {
        if (!overlayParent)
            return 0
        var geometryTick = party.x + party.y + overlayParent.width + overlayParent.height
        return party.mapToItem(overlayParent, 0, 0).y
    }

    function positionPanel() {
        if (!watchPartyPopover)
            return
        if (overlayParent) {
            var controlCenter = controlCenterInOverlay()
            var desiredX = controlCenter.x - (watchPartyPopover.width - party.panelArrowInset)
            var maxX = Math.max(party.panelEdgeMargin,
                                overlayParent.width - watchPartyPopover.width - party.panelEdgeMargin)
            watchPartyPopover.x = clampX(desiredX, party.panelEdgeMargin, maxX)
            watchPartyPopover.y = Math.max(
                party.panelEdgeMargin,
                controlTopInOverlay() - watchPartyPopover.height - party.panelControlGap)
        } else {
            watchPartyPopover.x = party.width - watchPartyPopover.width
            watchPartyPopover.y = -watchPartyPopover.height - party.panelControlGap
        }
        positionPanelChrome()
    }

    function positionPanelChrome() {
        if (!watchPartyPopover || !watchPartyPanelArrow || !watchPartyPanelShadow)
            return
        watchPartyPanelShadow.x = watchPartyPopover.x + 12
        watchPartyPanelShadow.y = watchPartyPopover.y + 18
        if (overlayParent) {
            var controlCenter = controlCenterInOverlay()
            var arrowMin = watchPartyPopover.x + watchPartyPanelArrow.width
            var arrowMax = watchPartyPopover.x + watchPartyPopover.width - watchPartyPanelArrow.width * 2
            watchPartyPanelArrow.x = clampX(
                controlCenter.x - watchPartyPanelArrow.width / 2,
                arrowMin, Math.max(arrowMin, arrowMax))
        } else {
            watchPartyPanelArrow.x = watchPartyPopover.x + watchPartyPopover.width - party.panelArrowInset
        }
        watchPartyPanelArrow.y = watchPartyPopover.y + watchPartyPopover.height - 4
    }

    function closeFromPanel() {
        party.panelOpen = false
        faceChip.forceActiveFocus()
    }

    function lifecycleMessage() {
        if (reconnectingState)
            return "Reconnecting to the Watch Party… playback sync will resume from authoritative room state."
        if (hostGraceState)
            return "The host is reconnecting. The room stays active during the short recovery window."
        return ""
    }

    function sourceStatusTitle() {
        if (sourceEligible)
            return sourceEligibility === "torrent" ? "Exact torrent source" : "Verified debrid source"
        return "Source unavailable for Watch Party"
    }

    function sourceStatusDetail() {
        if (sourceEligible)
            return "Everyone joins the same source. Colosseum only coordinates playback."
        if (sourceInfo && String(sourceInfo.reason || "") === "direct_source_not_verified_debrid")
            return "This direct stream is not a verified debrid source."
        if (sourceInfo && String(sourceInfo.reason || "") === "invalid_torrent_identity")
            return "This stream does not have a valid torrent identity."
        return "Use an exact torrent source or a verified debrid source."
    }

    function participantTitle(row) {
        if (!row)
            return ""
        var title = String(row.displayName || "")
        if (row.host)
            title += "  ·  Host"
        if (row.local)
            title += "  ·  You"
        return title
    }

    function participantMeta(row) {
        if (!row)
            return ""
        var bits = []
        bits.push(row.identityKind === "guest" ? "Guest" : "Signed in")
        if (!row.connected)
            bits.push("Reconnecting")
        else if (!row.ready)
            bits.push("Not ready")
        else if (row.syncStatus === "buffering")
            bits.push("Buffering")
        else if (row.syncStatus === "desynced")
            bits.push("Out of sync")
        else if (row.syncStatus === "inSync")
            bits.push("In sync")
        return bits.join(" · ")
    }

    function runStart() {
        if (startAvailable)
            controller.startParty(sourceInfo)
    }

    function runInvite() {
        if (!controller || !controller.canInvite)
            return
        var target = inviteInput.text
        if (target.trim().length === 0)
            return
        if (controller.inviteExactUsername(target))
            inviteInput.text = ""
    }

    function runChat() {
        if (!controller || !controller.canChat)
            return
        var value = chatInput.text
        if (value.trim().length === 0)
            return
        if (controller.sendChat(value))
            chatInput.text = ""
    }

    function runCatchUp() {
        if (!catchUpState || !controller)
            return
        controller.catchUp()
    }

    Theme { id: theme }

    // Native Player 1 applet face: same 40px circular hit target as Audio/Subtitles.
    Item {
        id: faceChip
        objectName: "watchPartyPlayerControl"
        anchors.fill: parent
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: "Watch Party"
        Accessible.description: party.inRoom
                                ? "Open active Watch Party"
                                : "Open Watch Party"

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: party.panelOpen
                   ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.16)
                   : faceMouse.containsMouse
                     ? Qt.rgba(1, 1, 1, 0.12)
                     : "transparent"
            border.width: (party.panelOpen || party.inRoom) ? 1 : 0
            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
        }

        // Two-person glyph drawn locally so the player does not expand its pinned
        // Lucide subset for one feature icon.
        Item {
            anchors.centerIn: parent
            width: 22
            height: 20
            opacity: faceChip.enabled ? 1.0 : 0.45

            Rectangle {
                x: 3; y: 2
                width: 7; height: 7
                radius: 3.5
                color: party.panelOpen || party.inRoom ? theme.gold : theme.ink
            }
            Rectangle {
                x: 12; y: 4
                width: 6; height: 6
                radius: 3
                color: party.panelOpen || party.inRoom ? theme.gold : theme.ink
            }
            Rectangle {
                x: 1; y: 11
                width: 11; height: 7
                radius: 5
                color: party.panelOpen || party.inRoom ? theme.gold : theme.ink
            }
            Rectangle {
                x: 11; y: 12
                width: 10; height: 6
                radius: 5
                color: party.panelOpen || party.inRoom ? theme.gold : theme.ink
            }
        }

        Rectangle {
            visible: party.inRoom && !party.panelOpen
            width: 6
            height: 6
            radius: 3
            color: theme.gold
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 6
            anchors.topMargin: 6
        }

        MouseArea {
            id: faceMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: party.toggleRequested(party.panelOpen)
        }

        Keys.onReturnPressed: party.toggleRequested(party.panelOpen)
        Keys.onEnterPressed: party.toggleRequested(party.panelOpen)
        Keys.onSpacePressed: party.toggleRequested(party.panelOpen)
    }

    // The atlas uses a deep, soft shadow behind the panel. Qt Quick's core primitives do not
    // provide a blur without pulling another effect module into Player 1, so Slice 01 keeps the
    // same depth cue as a restrained neutral shadow plate. It is presentation-only.
    Rectangle {
        id: watchPartyPanelShadow
        objectName: "watchPartyPanelShadow"
        parent: party.overlayParent ? party.overlayParent : party
        visible: party.panelOpen
        z: party.overlayParent ? 38 : 28
        width: Math.max(0, watchPartyPopover.width - 24)
        height: Math.max(0, watchPartyPopover.height - 6)
        radius: party.panelRadius + 4
        color: Qt.rgba(0, 0, 0, 0.34)
    }

    // Pointer is attached to the popover geometry rather than the 40px applet item. This keeps
    // it visually fused to the panel after the popover is reparented into Player 1's overlay.
    Rectangle {
        id: watchPartyPanelArrow
        objectName: "watchPartyPanelArrow"
        parent: party.overlayParent ? party.overlayParent : party
        visible: party.panelOpen
        z: party.overlayParent ? 39 : 29
        width: 8
        height: 8
        rotation: 45
        color: party.panelFill
        border.width: 1
        border.color: party.panelEdge
    }

    Rectangle {
        id: watchPartyPopover
        objectName: "watchPartyPanel"
        parent: party.overlayParent ? party.overlayParent : party
        visible: party.panelOpen
        z: party.overlayParent ? 40 : 30
        width: party.panelEffectiveWidth
        height: party.panelEffectiveHeight
        radius: party.panelRadius
        color: party.panelFill
        border.width: 1
        border.color: party.panelEdge
        clip: true

        onXChanged: party.positionPanelChrome()
        onYChanged: party.positionPanelChrome()
        onWidthChanged: party.positionPanel()
        onHeightChanged: party.positionPanel()
        onVisibleChanged: if (visible) {
            party.positionPanel()
            panelFocus.forceActiveFocus()
        }

        FocusScope {
            id: panelFocus
            objectName: "watchPartyPanelFocus"
            anchors.fill: parent
            Keys.onEscapePressed: party.closeFromPanel()

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {}
            }

            Row {
                id: panelHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 16
                anchors.rightMargin: 10
                height: party.panelHeaderHeight
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Watch Party"
                    color: theme.ink
                    font.family: theme.hud
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: party.inRoom ? party.controller.roomId : ""
                    color: theme.inkDimmer
                    font.family: theme.hud
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    width: Math.min(180, implicitWidth)
                }
            }

            Item {
                id: closeControl
                objectName: "watchPartyClose"
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.top: parent.top
                anchors.topMargin: 7
                width: 36
                height: 36
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Close Watch Party panel"

                Rectangle {
                    anchors.fill: parent
                    radius: 18
                    color: closeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.11) : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 22
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: party.closeFromPanel()
                }
                Keys.onReturnPressed: party.closeFromPanel()
                Keys.onEnterPressed: party.closeFromPanel()
                Keys.onSpacePressed: party.closeFromPanel()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                y: party.panelHeaderHeight
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            Flickable {
                id: bodyFlick
                objectName: "watchPartyBody"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: party.panelHeaderHeight + 1
                anchors.bottom: parent.bottom
                contentWidth: width
                contentHeight: bodyColumn.implicitHeight + 28
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: bodyColumn
                    x: party.panelInset
                    y: party.panelInset
                    width: bodyFlick.width - party.panelInset * 2
                    spacing: party.panelSectionGap

                    Rectangle {
                        id: lifecycleBanner
                        objectName: "watchPartyLifecycleBanner"
                        property string stateCategory:
                            party.visualLifecycleState === "reconnecting" ? "reconnecting"
                            : party.visualLifecycleState === "hostGrace" ? "hostGrace" : ""
                        visible: stateCategory.length > 0
                        width: parent.width
                        height: lifecycleBannerText.implicitHeight + 20
                        radius: 10
                        color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
                        border.width: 1
                        border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.24)

                        Text {
                            id: lifecycleBannerText
                            objectName: "watchPartyLifecycleMessage"
                            x: 10
                            y: 10
                            width: parent.width - 20
                            text: party.lifecycleMessage()
                            color: theme.inkDim
                            font.family: theme.hud
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                    }

                    Rectangle {
                        objectName: "watchPartySourceCard"
                        width: parent.width
                        height: sourceTextColumn.implicitHeight + 24
                        radius: 11
                        color: party.sourceEligible
                               ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.09)
                               : Qt.rgba(1, 1, 1, 0.055)
                        border.width: 1
                        border.color: party.sourceEligible
                                      ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.28)
                                      : Qt.rgba(1, 1, 1, 0.10)

                        Column {
                            id: sourceTextColumn
                            x: 12
                            y: 11
                            width: parent.width - 24
                            spacing: 4
                            Text {
                                width: parent.width
                                text: party.sourceStatusTitle()
                                color: party.sourceEligible ? theme.gold : theme.ink
                                font.family: theme.hud
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }
                            Text {
                                width: parent.width
                                text: party.sourceStatusDetail()
                                color: theme.inkDim
                                font.family: theme.hud
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Column {
                        visible: !party.inRoom
                        width: parent.width
                        spacing: 10

                        Text {
                            width: parent.width
                            text: !party.controller || !party.controller.serviceConfigured
                                  ? "Watch Party service is not configured."
                                  : !party.controller.signedIn
                                    ? "Sign in to host. Guests can join from the taskbar without an account."
                                    : "Start a private room from the source already playing."
                            color: theme.inkDim
                            font.family: theme.hud
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        ActionButton {
                            id: startButton
                            objectName: "watchPartyStart"
                            width: parent.width
                            label: party.controller && party.controller.busy ? "Starting…" : "Start Watch Party"
                            primary: true
                            enabled: party.startAvailable
                            onClicked: party.runStart()
                        }
                    }

                    Column {
                        visible: party.inRoom
                        width: parent.width
                        spacing: 12

                        Row {
                            objectName: "watchPartyRoomIdentity"
                            width: parent.width
                            spacing: 8

                            Column {
                                width: parent.width - copyRoomButton.width - 8
                                spacing: 2
                                Text {
                                    text: "ROOM ID"
                                    color: theme.inkDimmer
                                    font.family: theme.hud
                                    font.pixelSize: 10
                                    font.letterSpacing: 1.2
                                }
                                Text {
                                    objectName: "watchPartyRoomId"
                                    width: parent.width
                                    text: party.controller ? party.controller.roomId : ""
                                    color: theme.ink
                                    font.family: theme.hud
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                }
                            }

                            ActionButton {
                                id: copyRoomButton
                                objectName: "watchPartyCopyRoomId"
                                width: 74
                                label: "Copy"
                                enabled: party.controller && String(party.controller.roomId || "").length > 0
                                onClicked: party.copyRequested(String(party.controller.roomId || ""))
                            }
                        }

                        Rectangle {
                            objectName: "watchPartySourceMismatch"
                            visible: party.sourceNotReadyState
                            width: parent.width
                            height: mismatchText.implicitHeight + 20
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.06)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.12)
                            Text {
                                id: mismatchText
                                objectName: "watchPartySourceMismatchMessage"
                                x: 10
                                y: 10
                                width: parent.width - 20
                                text: "This player is not on the room's exact source yet. Open the room source before playback can sync."
                                color: theme.inkDim
                                font.family: theme.hud
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }

                        Column {
                            objectName: "watchPartyControlSection"
                            visible: party.hostAuthorityVisible
                                     && party.controller && party.controller.canToggleControlMode
                            width: parent.width
                            spacing: 7

                            Text {
                                text: "PLAYBACK CONTROL"
                                color: theme.inkDimmer
                                font.family: theme.hud
                                font.pixelSize: 10
                                font.letterSpacing: 1.2
                            }

                            Row {
                                spacing: 8
                                ActionButton {
                                    objectName: "watchPartyHostControl"
                                    width: (bodyColumn.width - 8) / 2
                                    label: "Host Control"
                                    selected: party.controller && party.controller.controlMode === "host"
                                    onClicked: if (party.controller) party.controller.setSharedControl(false)
                                }
                                ActionButton {
                                    objectName: "watchPartySharedControl"
                                    width: (bodyColumn.width - 8) / 2
                                    label: "Shared Control"
                                    selected: party.controller && party.controller.controlMode === "shared"
                                    onClicked: if (party.controller) party.controller.setSharedControl(true)
                                }
                            }
                        }

                        Row {
                            objectName: "watchPartyCatchUpRow"
                            visible: party.catchUpState
                            width: parent.width
                            spacing: 8

                            Text {
                                objectName: "watchPartyCatchUpMessage"
                                width: parent.width - catchUpButton.width - 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: "You're out of sync with the room."
                                color: theme.inkDim
                                font.family: theme.hud
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            ActionButton {
                                id: catchUpButton
                                objectName: "watchPartyCatchUp"
                                width: 92
                                label: "Catch Up"
                                primary: true
                                enabled: party.catchUpState
                                onClicked: party.runCatchUp()
                            }
                        }

                        Column {
                            objectName: "watchPartyInviteSection"
                            visible: party.hostAuthorityVisible
                                     && party.controller && party.controller.canInvite
                            width: parent.width
                            spacing: 7

                            Text {
                                text: "INVITE BY EXACT USERNAME"
                                color: theme.inkDimmer
                                font.family: theme.hud
                                font.pixelSize: 10
                                font.letterSpacing: 1.2
                            }

                            Row {
                                width: parent.width
                                spacing: 8

                                Rectangle {
                                    width: parent.width - inviteButton.width - 8
                                    height: 38
                                    radius: 10
                                    color: inviteInput.activeFocus
                                           ? Qt.rgba(1, 1, 1, 0.11)
                                           : Qt.rgba(1, 1, 1, 0.07)
                                    border.width: 1
                                    border.color: inviteInput.activeFocus
                                                  ? theme.gold
                                                  : Qt.rgba(1, 1, 1, 0.12)

                                    TextInput {
                                        id: inviteInput
                                        objectName: "watchPartyInviteUsername"
                                        anchors.fill: parent
                                        anchors.leftMargin: 11
                                        anchors.rightMargin: 11
                                        verticalAlignment: TextInput.AlignVCenter
                                        color: theme.ink
                                        selectionColor: theme.gold
                                        selectedTextColor: "#111111"
                                        font.family: theme.hud
                                        font.pixelSize: 13
                                        clip: true
                                        activeFocusOnTab: true
                                        onAccepted: party.runInvite()
                                        Accessible.name: "Exact username to invite"
                                    }
                                    Text {
                                        anchors.fill: inviteInput
                                        verticalAlignment: Text.AlignVCenter
                                        visible: inviteInput.text.length === 0 && !inviteInput.activeFocus
                                        text: "Username"
                                        color: theme.inkDimmer
                                        font.family: theme.hud
                                        font.pixelSize: 13
                                    }
                                }

                                ActionButton {
                                    id: inviteButton
                                    objectName: "watchPartyInvite"
                                    width: 78
                                    label: party.controller && party.controller.inviteBusy ? "Sending…" : "Invite"
                                    enabled: party.controller
                                             && party.controller.canInvite
                                             && !party.controller.inviteBusy
                                             && inviteInput.text.trim().length > 0
                                    onClicked: party.runInvite()
                                }
                            }
                        }

                        Column {
                            objectName: "watchPartyParticipantsSection"
                            width: parent.width
                            spacing: 7

                            Text {
                                text: "PARTICIPANTS  "
                                      + (party.controller ? party.controller.participants.length : 0)
                                      + " / " + party.participantCapacity
                                color: theme.inkDimmer
                                font.family: theme.hud
                                font.pixelSize: 10
                                font.letterSpacing: 1.2
                            }

                            Repeater {
                                model: party.controller ? party.controller.participants : []
                                delegate: Rectangle {
                                    required property var modelData
                                    width: bodyColumn.width
                                    height: party.participantRowHeight
                                    radius: 9
                                    color: Qt.rgba(1, 1, 1, 0.045)

                                    Rectangle {
                                        x: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: modelData.connected && modelData.ready
                                               ? (modelData.syncStatus === "desynced" ? theme.inkDimmer : theme.gold)
                                               : theme.inkDimmer
                                    }

                                    Column {
                                        x: 28
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 40
                                        spacing: 1
                                        Text {
                                            width: parent.width
                                            text: party.participantTitle(modelData)
                                            color: theme.ink
                                            font.family: theme.hud
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                        }
                                        Text {
                                            width: parent.width
                                            text: party.participantMeta(modelData)
                                            color: theme.inkDimmer
                                            font.family: theme.hud
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                        }
                                    }
                                }
                            }
                        }

                        Column {
                            objectName: "watchPartyChatSection"
                            width: parent.width
                            spacing: 7

                            Text {
                                text: "ROOM CHAT"
                                color: theme.inkDimmer
                                font.family: theme.hud
                                font.pixelSize: 10
                                font.letterSpacing: 1.2
                            }

                            Rectangle {
                                objectName: "watchPartyChatViewport"
                                width: parent.width
                                height: Math.min(party.chatViewportMaxHeight,
                                                 Math.max(66, chatColumn.implicitHeight + 16))
                                radius: 10
                                color: Qt.rgba(1, 1, 1, 0.045)
                                clip: true

                                Flickable {
                                    id: chatFlick
                                    objectName: "watchPartyChatFlick"
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    contentWidth: width
                                    contentHeight: chatColumn.implicitHeight
                                    flickableDirection: Flickable.VerticalFlick
                                    boundsBehavior: Flickable.StopAtBounds

                                    Column {
                                        id: chatColumn
                                        width: parent.width
                                        spacing: 7

                                        Text {
                                            visible: !party.controller || party.controller.chatMessages.length === 0
                                            width: parent.width
                                            text: "Messages live only for this room."
                                            color: theme.inkDimmer
                                            font.family: theme.hud
                                            font.pixelSize: 11
                                            wrapMode: Text.Wrap
                                        }

                                        Repeater {
                                            model: party.controller ? party.controller.chatMessages : []
                                            delegate: Column {
                                                required property var modelData
                                                width: chatColumn.width
                                                spacing: 1

                                                Text {
                                                    width: parent.width
                                                    textFormat: Text.PlainText
                                                    text: String(modelData.displayName || "")
                                                    color: theme.ink
                                                    font.family: theme.hud
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    width: parent.width
                                                    textFormat: Text.PlainText
                                                    text: String(modelData.message || "")
                                                    color: theme.ink
                                                    font.family: theme.hud
                                                    font.pixelSize: 11
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 8

                                Rectangle {
                                    width: parent.width - sendChatButton.width - 8
                                    height: 38
                                    radius: 10
                                    color: chatInput.activeFocus
                                           ? Qt.rgba(1, 1, 1, 0.11)
                                           : Qt.rgba(1, 1, 1, 0.07)
                                    border.width: 1
                                    border.color: chatInput.activeFocus
                                                  ? theme.gold
                                                  : Qt.rgba(1, 1, 1, 0.12)

                                    TextInput {
                                        id: chatInput
                                        objectName: "watchPartyChatInput"
                                        anchors.fill: parent
                                        anchors.leftMargin: 11
                                        anchors.rightMargin: 11
                                        verticalAlignment: TextInput.AlignVCenter
                                        color: theme.ink
                                        selectionColor: theme.gold
                                        selectedTextColor: "#111111"
                                        font.family: theme.hud
                                        font.pixelSize: 13
                                        clip: true
                                        activeFocusOnTab: true
                                        maximumLength: 1000
                                        onAccepted: party.runChat()
                                        Accessible.name: "Watch Party message"
                                    }

                                    Text {
                                        anchors.fill: chatInput
                                        verticalAlignment: Text.AlignVCenter
                                        visible: chatInput.text.length === 0 && !chatInput.activeFocus
                                        text: "Message room"
                                        color: theme.inkDimmer
                                        font.family: theme.hud
                                        font.pixelSize: 13
                                    }
                                }

                                ActionButton {
                                    id: sendChatButton
                                    objectName: "watchPartySendChat"
                                    width: 68
                                    label: "Send"
                                    enabled: party.controller
                                             && party.controller.canChat
                                             && chatInput.text.trim().length > 0
                                    onClicked: party.runChat()
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 7

                                Repeater {
                                    model: [
                                        { "label": "Like", "value": "like" },
                                        { "label": "Laugh", "value": "laugh" },
                                        { "label": "Wow", "value": "wow" }
                                    ]
                                    delegate: ActionButton {
                                        required property var modelData
                                        objectName: "watchPartyReaction_" + modelData.value
                                        width: 72
                                        label: modelData.label
                                        enabled: party.controller && party.controller.canChat
                                        onClicked: if (party.controller)
                                                       party.controller.sendReaction(modelData.value)
                                    }
                                }

                                Item { width: Math.max(0, parent.width - 3 * 72 - 2 * 7); height: 1 }
                            }
                        }

                        Text {
                            visible: party.controller
                                     && String(party.controller.errorText || "").length > 0
                            width: parent.width
                            text: party.controller ? party.controller.errorText : ""
                            color: theme.inkDim
                            font.family: theme.hud
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }

                        Text {
                            visible: party.controller
                                     && String(party.controller.noticeText || "").length > 0
                            width: parent.width
                            text: party.controller ? party.controller.noticeText : ""
                            color: theme.inkDim
                            font.family: theme.hud
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }

                        Row {
                            objectName: "watchPartyRoomActions"
                            width: parent.width
                            spacing: 8

                            ActionButton {
                                objectName: "watchPartyLeave"
                                width: party.hostAuthorityVisible ? (parent.width - 8) / 2 : parent.width
                                label: "Leave Party"
                                enabled: party.controller && party.controller.canLeave
                                onClicked: if (party.controller) party.controller.leaveParty()
                            }

                            ActionButton {
                                objectName: "watchPartyEnd"
                                visible: party.hostAuthorityVisible
                                width: (parent.width - 8) / 2
                                label: "End Party"
                                danger: true
                                enabled: party.controller && party.controller.canEnd
                                onClicked: if (party.controller) party.controller.endParty()
                            }
                        }
                    }

                    Item { width: 1; height: 2 }
                }
            }

            Rectangle {
                id: watchPartyScrollThumb
                objectName: "watchPartyScrollThumb"
                visible: bodyFlick.contentHeight > bodyFlick.height + 1
                z: 3
                anchors.right: parent.right
                anchors.rightMargin: 4
                width: 3
                radius: width / 2
                height: Math.max(30, bodyFlick.height * bodyFlick.visibleArea.heightRatio)
                y: party.panelHeaderHeight + 5
                   + Math.max(0, bodyFlick.height - height - 10)
                     * Math.max(0, Math.min(1,
                         bodyFlick.visibleArea.yPosition
                         / Math.max(0.0001, 1 - bodyFlick.visibleArea.heightRatio)))
                color: theme.glassHi
            }
        }
    }

    component ActionButton: Rectangle {
        id: action
        property string label: ""
        property bool primary: false
        property bool selected: false
        property bool danger: false
        signal clicked()

        height: 36
        radius: 9
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: label

        color: !enabled
               ? Qt.rgba(1, 1, 1, 0.035)
               : primary
                 ? theme.gold
                 : selected
                   ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.16)
                   : buttonMouse.containsMouse
                     ? Qt.rgba(1, 1, 1, 0.12)
                     : Qt.rgba(1, 1, 1, 0.07)
        border.width: primary ? 0 : 1
        border.color: danger
                      ? theme.edge
                      : selected || activeFocus
                        ? theme.gold
                        : Qt.rgba(1, 1, 1, 0.10)
        opacity: enabled ? 1.0 : 0.45

        Text {
            anchors.centerIn: parent
            text: action.label
            color: action.primary ? "#111111"
                                  : action.danger ? theme.inkDim
                                                  : action.selected ? theme.gold : theme.ink
            font.family: theme.hud
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            enabled: action.enabled
            hoverEnabled: true
            cursorShape: action.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: action.clicked()
        }

        Keys.onReturnPressed: if (enabled) action.clicked()
        Keys.onEnterPressed: if (enabled) action.clicked()
        Keys.onSpacePressed: if (enabled) action.clicked()
    }
}
