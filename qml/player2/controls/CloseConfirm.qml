import QtQuick

// A faithful re-implementation of the current player's close-confirm (parity spec F2): a centred
// 380×150 card — NO full-screen dim — with "End this session?", the Continue-Watching reassurance, and
// "Keep watching" / "End session" (gold-bordered) buttons. The shell owns when it appears and what
// confirming does (typed closeRequested seam); this surface just asks. Plain QtQuick only.
Item {
    id: confirm

    property QtObject theme
    property bool open: false

    signal confirmed()
    signal cancelled()

    readonly property color panelColor: theme ? theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.95)
    readonly property color gold: theme ? theme.gold : "#f0c44a"
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDim: theme ? theme.inkDim : "#c9c8d0"

    anchors.fill: parent
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.centerIn: parent
        width: 380
        height: 150
        radius: 18
        color: confirm.panelColor
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)

        // Absorb clicks on the card so they don't fall through to the video (parity spec F2).
        MouseArea { anchors.fill: parent; hoverEnabled: true }

        Text {
            x: 18; y: 16
            text: "End this session?"
            color: confirm.ink
            font.family: "Segoe UI"; font.pixelSize: 16; font.weight: Font.DemiBold
        }
        Text {
            x: 18; y: 46
            width: parent.width - 36
            text: "Your spot stays in Continue Watching."
            color: confirm.inkDim
            font.family: "Segoe UI"; font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 16
            anchors.bottomMargin: 14
            spacing: 8
            Rectangle {
                width: 130; height: 34; radius: 9
                color: keepArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.14)
                Text {
                    anchors.centerIn: parent; text: "Keep watching"
                    color: confirm.ink; font.family: "Segoe UI"; font.pixelSize: 13
                }
                MouseArea {
                    id: keepArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: confirm.cancelled()
                }
            }
            Rectangle {
                width: 110; height: 34; radius: 9
                color: endArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.12)
                border.width: 1; border.color: confirm.gold
                Text {
                    anchors.centerIn: parent; text: "End session"
                    color: confirm.ink; font.family: "Segoe UI"; font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: endArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: confirm.confirmed()
                }
            }
        }
    }
}
