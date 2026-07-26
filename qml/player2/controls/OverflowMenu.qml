import QtQuick

// The right-click "more controls" panel. Rows cycle typed session settings in place (loudness,
// aspect) and toggle the stats overlay. Plain QtQuick; the shell positions it at the cursor and
// dismisses it. No settings live here that a dedicated control already owns.
Item {
    id: menu

    property QtObject theme
    property var session
    property bool open: false
    signal toggleStatsRequested()
    signal showShortcutsRequested()
    signal pipRequested()

    readonly property color panelColor: theme ? theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.94)
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"
    readonly property color gold: theme ? theme.gold : "#f0c44a"

    readonly property var normalizationNames: ["Smooth", "Light", "Full (EBU R128)"]

    // Aspect/fill lives in the bottom HUD now (TransportBar), matching the current player — not here.
    function cycleLoudness() {
        if (session)
            session.setNormalizationMode((session.normalizationMode + 1) % 3)
    }

    implicitWidth: 300
    implicitHeight: panel.implicitHeight
    visible: open
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 110 } }

    component MenuRow: Item {
        property string label: ""
        property string value: ""
        signal tapped()
        width: parent ? parent.width : 0
        height: 40
        Rectangle {
            anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: 1; radius: 8
            color: rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: label; color: menu.ink; font.family: "Segoe UI"; font.pixelSize: 13
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: value; color: menu.gold; font.family: "Segoe UI"; font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: parent.tapped() }
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        implicitHeight: col.implicitHeight + 20
        color: menu.panelColor
        radius: 14
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
        MouseArea { anchors.fill: parent; hoverEnabled: true } // click-swallow

        Column {
            id: col
            anchors.fill: parent
            anchors.margins: 10
            spacing: 0
            Text {
                text: "More controls"; color: menu.inkDimmer
                font.family: "Segoe UI"; font.pixelSize: 11; font.letterSpacing: 1.2
                bottomPadding: 6
            }
            MenuRow { label: "Playback stats"; onTapped: menu.toggleStatsRequested() }
            MenuRow {
                label: "Loudness"
                value: menu.session ? menu.normalizationNames[menu.session.normalizationMode] : ""
                onTapped: menu.cycleLoudness()
            }
            MenuRow { label: "Picture-in-picture"; onTapped: menu.pipRequested() }
            MenuRow { label: "Keyboard shortcuts"; value: "?"; onTapped: menu.showShortcutsRequested() }
        }
    }
}
