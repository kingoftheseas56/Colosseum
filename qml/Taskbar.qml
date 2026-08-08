// Taskbar.qml - the OS-shell's auto-hiding switcher bar.
// The closed Colosseum button and the open taskbar are the same object, so the bar
// grows out of the button instead of swapping between two separate pieces.
import QtQuick
import QtQuick.Layouts

Item {
    id: bar
    anchors.fill: parent

    property var groups: (typeof Sessions !== "undefined") ? (Sessions.revision, Sessions.groups()) : []
    property string activeId: (typeof Sessions !== "undefined") ? Sessions.activeId : ""
    property bool open: false
    readonly property int leftEdge: Math.max(18, Math.min(80, parent.width * 0.045))
    readonly property int bottomGap: 16
    readonly property int closedSize: 64
    // The closed dock now holds the arch AND the permanent Vault folder door, so its closed width is
    // explicit: 8 left + 48 home + 14 spacing + 46 door + 14 right = 130. closedSize stays 64 for the
    // dock HEIGHT and the closed corner radius (changing closedSize itself would make a 130-tall
    // capsule with a 65px radius). (Slice 10)
    readonly property int closedWidth: 130

    signal switchRequested(string id)
    signal closeRequested(string id)
    signal startClicked()
    signal openMediaClicked()             // Open Media… — hand the app a local file (Slice 8)
    signal openRecentRequested()          // Open Recent disclosure — the remembered files (Slice 9)
    signal vaultClicked()                 // Vault folder door — opens the "On this machine" page (Slice 10)
    property bool vaultActive: false      // the Vault page is the front surface
    signal downloadsClicked()
    property int downloadsBadge: 0        // live download jobs (gold count chip)
    property bool downloadsActive: false  // the Downloads page is the front surface
    signal extensionsClicked()
    property bool extensionsActive: false // the Extensions page is the front surface
    signal settingsClicked()
    property bool settingsActive: false   // the Settings page is the front surface
    signal updateClicked()
    property bool updateActive: false      // the Update page is the front surface
    property bool updateAvailable: false   // a verified newer release exists
    property bool updateUnseen: false      // the user has not opened its chronicle yet

    // A verified release is a taskbar event, not a silent state change.  Reveal the
    // dock once so the update affordance is visible without requiring the user to
    // discover the closed-shell hit target first.
    onUpdateAvailableChanged: if (updateAvailable) reveal()

    onOpenChanged: if (!open) fan.visible = false

    // ---- auto-reveal (2026-07-04): a minimize extends the bar out of the icon so you SEE the
    //      session arrive, then it pulls back after 15s without taskbar interaction. A bar the
    //      user opened by clicking the icon is sticky — only auto-revealed bars pull back. ----
    property bool autoRevealed: false
    function reveal() {
        open = true
        autoRevealed = true
        idleTimer.restart()
    }
    Timer {
        id: idleTimer
        interval: 15000
        onTriggered: {
            if (dockHover.hovered || fanHover.hovered) { restart(); return }   // still engaged
            if (bar.autoRevealed) { bar.open = false; bar.autoRevealed = false }
        }
    }

    // while the fan is up, a click anywhere that isn't the fan collapses it (Windows popup rule);
    // sits UNDER the dock and fan, so their own controls keep first claim on the click.
    MouseArea {
        anchors.fill: parent
        visible: fan.visible
        onClicked: fan.visible = false
    }

    function groupHasActive(group) {
        var sessions = group.sessions || []
        for (var i = 0; i < sessions.length; i++) {
            if (sessions[i].id === bar.activeId) return true
        }
        return false
    }

    // Session tiles are ICON CIRCLES now (Hemanth 2026-07-18, his three SVGs):
    // the icon says which surface lives there — no world names on the bar.
    function sessionIcon(group) {
        var app = String(group.appType || "")
        if (app === "tankoban") return "../assets/icons/comic-book.svg"
        if (app === "biblio") return "../assets/icons/book-library.svg"
        if (app === "theatre") return "../assets/icons/projector-theatre.svg"
        return group.icon || ""
    }

    Rectangle {
        id: dock
        x: bar.leftEdge
        y: parent.height - height - bar.bottomGap
        width: bar.open ? Math.min(parent.width - (bar.leftEdge * 2), 1720) : bar.closedWidth
        height: bar.closedSize
        radius: bar.open ? 18 : bar.closedSize / 2
        clip: true
        color: startMa.containsMouse || bar.open ? Qt.rgba(0.02, 0.02, 0.04, 0.78)
                                                 : Qt.rgba(0.02, 0.02, 0.03, 0.72)
        border.width: 1
        border.color: startMa.containsMouse ? Qt.rgba(0.94, 0.76, 0.35, 0.56)
                                            : Qt.rgba(1, 1, 1, 0.16)

        Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }

        // any engagement with the bar holds the auto-pullback clock
        HoverHandler {
            id: dockHover
            onHoveredChanged: if (hovered && bar.autoRevealed) idleTimer.restart()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 14
            spacing: 14

            Item {
                objectName: "colosseumTaskbarHomeButton"
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    anchors.fill: parent
                    radius: bar.open ? 14 : 24
                    color: startMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                    border.width: bar.open ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.13)

                    Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: 140 } }
                }

                Image {
                    anchors.centerIn: parent
                    width: 28; height: 28
                    source: "../assets/icons/colosseum.svg"
                    fillMode: Image.PreserveAspectFit
                }

                MouseArea {
                    id: startMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        bar.open = !bar.open
                        bar.autoRevealed = false   // opened (or closed) by hand → sticky, no pullback
                    }
                }
            }

            // ---- Vault: the permanent folder door — opens the "On this machine" full page (Slice 10).
            //      ALWAYS visible (unlike the open-only page controls), so it rides in the closed
            //      capsule beside the arch; the dock widens to closedWidth to hold both. Clones the
            //      page-control hover + gold active-underline language; no badge. ----
            Item {
                objectName: "taskbarVaultDoor"
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: vaultMa.containsMouse || bar.vaultActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                }
                Image {
                    anchors.centerIn: parent
                    width: 21; height: 21
                    source: "../assets/icons/vault-folder.svg"
                    fillMode: Image.PreserveAspectFit
                    opacity: bar.vaultActive ? 1 : 0.75
                }
                Rectangle {   // active-page underline, same gold language as session tiles
                    visible: bar.vaultActive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                    width: 20; height: 3; radius: 2
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
                }
                MouseArea {
                    id: vaultMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.vaultClicked()
                }
            }

            // ---- Open Media: hand the app a local file (Open Media…, Vault Slice 8) ----
            // An ACTION, not a page: no active-underline, no badge — just icon + hover.
            Item {
                objectName: "taskbarOpenMedia"
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: omMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                }
                Image {
                    anchors.centerIn: parent
                    width: 21; height: 21
                    source: "../assets/icons/open-media.svg"
                    fillMode: Image.PreserveAspectFit
                    opacity: omMa.containsMouse ? 1 : 0.75
                }
                MouseArea {
                    id: omMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.openMediaClicked()
                }
                // Recent disclosure (Slice 9): the corner caret opens the remembered-files list;
                // the icon itself still opens the picker (Slice 8 preserved). Declared after omMa
                // so its click wins in the corner it occupies.
                Rectangle {
                    objectName: "openRecentDisclosure"
                    width: 15; height: 15; radius: 4
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.rightMargin: -1; anchors.bottomMargin: -1
                    color: recentMa.containsMouse ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(0.10, 0.10, 0.13, 0.95)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.22)
                    Canvas {
                        anchors.centerIn: parent; width: 9; height: 6
                        onPaint: {
                            var ctx = getContext("2d"); ctx.reset()
                            ctx.strokeStyle = "#cfcfd6"; ctx.lineWidth = 1.4; ctx.lineCap = "round"
                            ctx.beginPath(); ctx.moveTo(1, 1); ctx.lineTo(4.5, 4.5); ctx.lineTo(8, 1); ctx.stroke()
                        }
                    }
                    MouseArea {
                        id: recentMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bar.openRecentRequested()
                    }
                }
            }

            // ---- Downloads: lives beside the Colosseum icon (ratified 2026-07-04) ----
            Item {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: dlMa.containsMouse || bar.downloadsActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                }
                Image {
                    anchors.centerIn: parent
                    width: 21; height: 21
                    source: "../assets/icons/download.svg"
                    fillMode: Image.PreserveAspectFit
                    opacity: bar.downloadsActive ? 1 : 0.75
                }
                Rectangle {   // active-page underline, same gold language as session tiles
                    visible: bar.downloadsActive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                    width: 20; height: 3; radius: 2
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
                }
                Rectangle {   // live-jobs badge
                    visible: bar.downloadsBadge > 0
                    anchors.top: parent.top; anchors.right: parent.right
                    anchors.topMargin: 2; anchors.rightMargin: 2
                    width: Math.max(17, badgeT.implicitWidth + 8); height: 17; radius: 9
                    color: "#f0c44a"
                    Text { id: badgeT; anchors.centerIn: parent
                           text: bar.downloadsBadge
                           color: "#141207"; font.pixelSize: 11; font.weight: Font.Bold }
                }
                MouseArea {
                    id: dlMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.downloadsClicked()
                }
            }

            // ---- Extensions: the store, beside Downloads (ratified 2026-07-05) ----
            Item {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: extMa.containsMouse || bar.extensionsActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                }
                Image {
                    anchors.centerIn: parent
                    width: 21; height: 21
                    source: "../assets/icons/extensions.svg"
                    fillMode: Image.PreserveAspectFit
                    opacity: bar.extensionsActive ? 1 : 0.75
                }
                Rectangle {   // active-page underline, same gold language as session tiles
                    visible: bar.extensionsActive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                    width: 20; height: 3; radius: 2
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
                }
                MouseArea {
                    id: extMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.extensionsClicked()
                }
            }

            // ---- Settings: the global preferences sliders, beside Extensions (Task 2).
            //      A distinct sliders glyph (not the gear) so it never reads as the
            //      wallpaper settings gear in TopBar (Hemanth, 2026-08-02). ----
            Item {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: settMa.containsMouse || bar.settingsActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                }
                Image {
                    anchors.centerIn: parent
                    width: 21; height: 21
                    source: "../assets/icons/preferences.svg"
                    fillMode: Image.PreserveAspectFit
                    opacity: bar.settingsActive ? 1 : 0.75
                }
                Rectangle {   // active-page underline, same gold language as session tiles
                    visible: bar.settingsActive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                    width: 20; height: 3; radius: 2
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
                }
                MouseArea {
                    id: settMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.settingsClicked()
                }
            }

            // ---- Update: a quiet release bell beside Settings. The gold dot persists while
            // a verified release is available; only the unseen flag is allowed to animate.
            Item {
                id: updateButton
                objectName: "colosseumUpdateTaskbarButton"
                property alias updateAvailable: bar.updateAvailable
                property alias updateUnseen: bar.updateUnseen
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open

                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: updateMa.containsMouse || bar.updateActive
                           ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                }
                Image {
                    anchors.centerIn: parent
                    width: 21; height: 21
                    source: "../assets/icons/update.svg"
                    fillMode: Image.PreserveAspectFit
                    opacity: bar.updateActive ? 1 : 0.75
                }
                Rectangle {
                    visible: bar.updateActive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                    width: 20; height: 3; radius: 2
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
                }
                Rectangle {
                    id: updateBadge
                    objectName: "colosseumUpdateBadge"
                    visible: bar.updateAvailable
                    anchors.top: parent.top; anchors.right: parent.right
                    anchors.topMargin: 2; anchors.rightMargin: 2
                    width: 12; height: 12; radius: 6
                    color: "#f0c44a"
                    border.width: 1
                    border.color: Qt.rgba(0.08, 0.07, 0.03, 0.88)

                    SequentialAnimation on scale {
                        running: bar.updateUnseen
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 1.18; duration: 600; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.18; to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                        PauseAnimation { duration: 1200 }
                    }
                }
                Accessible.role: Accessible.Button
                Accessible.name: bar.updateAvailable ? "Update available" : "Updates"
                MouseArea {
                    id: updateMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.updateClicked()
                }
            }

            Row {
                Layout.fillWidth: true
                spacing: 10
                opacity: bar.open ? 1 : 0
                enabled: bar.open

                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                Repeater {
                    model: bar.groups
                    // Windows-taskbar circles (Hemanth 2026-07-18): one icon circle per
                    // surface — comic book / book library / projector — no world names.
                    // Gold ring = the active session lives here; count chip = a stack
                    // (click fans out, the fan rows still carry full titles + close).
                    delegate: Rectangle {
                        id: tile
                        required property var modelData

                        property bool isActive: bar.groupHasActive(modelData)
                        property bool multi: (modelData.sessions || []).length > 1

                        width: 46
                        height: 46
                        radius: 23
                        color: tileHover.hovered || tile.isActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                        border.width: tile.isActive ? 1.5 : 1
                        border.color: tile.isActive ? Qt.rgba(0.94, 0.77, 0.29, 0.85)
                                                    : Qt.rgba(1, 1, 1, 0.10)

                        Image {
                            anchors.centerIn: parent
                            width: 24; height: 24
                            sourceSize.width: 48; sourceSize.height: 48
                            source: bar.sessionIcon(tile.modelData)
                            fillMode: Image.PreserveAspectFit
                            opacity: tileHover.hovered || tile.isActive ? 1 : 0.8
                        }

                        // stack count — the circle can't say "(3)" in words anymore
                        Rectangle {
                            visible: tile.multi
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            width: 16; height: 16; radius: 8
                            color: Qt.rgba(0.10, 0.10, 0.13, 0.95)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.22)
                            Text {
                                anchors.centerIn: parent
                                text: (tile.modelData.sessions || []).length
                                color: "#f1f1f4"
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }

                        MouseArea {
                            id: tileMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var sessions = tile.modelData.sessions || []
                                if (sessions.length === 1) {
                                    bar.switchRequested(sessions[0].id)
                                    bar.open = false
                                } else {
                                    fan.openFor(tile, sessions)
                                }
                            }
                        }

                        HoverHandler { id: tileHover }

                        // Chrome-style close — single-session circles only (a stack fans
                        // out; the fan rows carry their own close). Rides the circle's
                        // top-right shoulder, painted on hover; click closes, not
                        // switches, because it sits above tileMa in its corner.
                        Item {
                            id: tileClose
                            visible: !tile.multi
                            anchors.right: parent.right
                            anchors.rightMargin: -3
                            anchors.top: parent.top
                            anchors.topMargin: -3
                            width: 18; height: 18
                            opacity: tileHover.hovered ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 120 } }

                            Rectangle {
                                anchors.fill: parent
                                radius: 9
                                color: Qt.rgba(0.10, 0.10, 0.13, 0.95)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.22)
                            }
                            Rectangle {
                                width: 8; height: 1.4; radius: 1; anchors.centerIn: parent
                                rotation: 45
                                color: tileCloseMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            }
                            Rectangle {
                                width: 8; height: 1.4; radius: 1; anchors.centerIn: parent
                                rotation: -45
                                color: tileCloseMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            }

                            MouseArea {
                                id: tileCloseMa
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: tileClose.visible && tileHover.hovered
                                cursorShape: Qt.PointingHandCursor
                                onClicked: bar.closeRequested(tile.modelData.sessions[0].id)
                            }
                        }
                    }
                }
            }

        }
    }

    Rectangle {
        id: fan
        property var sessions: []
        width: 292
        visible: false
        height: fanCol.implicitHeight + 16
        radius: 18
        color: Qt.rgba(0.04, 0.04, 0.06, 0.96)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)

        HoverHandler {
            id: fanHover
            onHoveredChanged: if (hovered && bar.autoRevealed) idleTimer.restart()
        }

        function openFor(tile, nextSessions) {
            fan.sessions = nextSessions
            var point = tile.mapToItem(bar, 0, 0)
            fan.x = Math.min(Math.max(bar.leftEdge, point.x), bar.width - fan.width - bar.leftEdge)
            fan.y = dock.y - fan.height - 8
            fan.visible = true
        }

        Column {
            id: fanCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Repeater {
                model: fan.sessions
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 40
                    radius: 10
                    color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 60
                        elide: Text.ElideRight
                        text: modelData.title
                        color: "#eaeaef"
                        font.pixelSize: 13
                    }

                    Item {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        z: 1   // close outranks rowMa (declared later, fills the whole row)
                        width: 24
                        height: 24

                        Rectangle {
                            width: 11; height: 1.4; radius: 1
                            color: closeMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            anchors.centerIn: parent
                            rotation: 45
                        }

                        Rectangle {
                            width: 11; height: 1.4; radius: 1
                            color: closeMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            anchors.centerIn: parent
                            rotation: -45
                        }

                        MouseArea {
                            id: closeMa
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                bar.closeRequested(modelData.id)
                                fan.visible = false
                            }
                        }
                    }

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            bar.switchRequested(modelData.id)
                            fan.visible = false
                            bar.open = false
                        }
                    }
                }
            }
        }
    }
}
