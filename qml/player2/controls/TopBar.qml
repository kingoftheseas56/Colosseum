import QtQuick
import "../.."

// Player 2's title bar, matched element-for-element to the current player's rather than designed
// fresh: a 112px scrim fading downward, a circular Back at the left with NOW PLAYING / title /
// subtitle beside it, the live wall clock, then Minimize and Close at the right. Fullscreen is NOT
// here - it lives once, in the bottom transport, exactly as the current player has it. (The measured
// source is recorded in the parity ledger; naming the production file here would breach the
// isolation contract, which forbids this tree from referencing it at all.)
//
// It exists because the lab never needed one: the harness ran in an ordinary desktop window with its
// own frame. Inside the app's frameless shell there was nothing up here at all, which is what Hemanth
// hit - "no min, max, close buttons" (2026-07-25).
Item {
    id: bar

    property QtObject theme: null
    property string title: ""
    property string subtitle: ""
    property string nowClock: ""
    property bool shown: true
    // Narrow layouts drop the micro-label and the subtitle, same threshold as the shipped bar (the
    // measured source is recorded in the parity ledger, per the note above). Matching it keeps this
    // bar's behaviour identical in the 680-899px band instead of dropping the clock/NOW PLAYING/
    // episode line early.
    readonly property bool tight: bar.width < 680

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    height: 112

    transform: Translate {
        y: bar.shown ? 0 : -8
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }
    opacity: bar.shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    visible: opacity > 0.001

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.60) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }

    // NOTE: a round icon button also exists inline in TransportBar.qml. It is bound to that file's
    // scope, so it could not be reused here without reworking verified transport chrome late in the
    // day. The two should become one component - deliberate, recorded duplication, not an oversight.
    component BarButton: Item {
        id: rb
        property string icon: ""
        property string tooltip: ""
        property real size: 38
        property string androidCaption: ""
        readonly property bool androidLabelled: Qt.platform.os === "android" && androidCaption.length > 0
        signal tapped()
        implicitWidth: androidLabelled ? Math.max(size, androidCaption.length * 7 + 12) : size
        implicitHeight: androidLabelled ? size + 24 : size
        scale: tap.pressed ? 0.95 : (tap.containsMouse ? 1.04 : 1.0)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: rb.size; height: rb.size
            radius: width / 2
            color: tap.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
        }
        Player2Icon {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: rb.size; height: rb.size
            kind: rb.icon
            ink: bar.theme ? bar.theme.ink : "#f7f7f5"
            accessibleName: rb.tooltip
        }
        MouseArea {
            id: tap
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: rb.tapped()
        }
        KeyboardAction {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: rb.width; height: rb.size
            pointerEnabled: false
            accessibleName: rb.tooltip
            tooltipText: rb.icon === "minimizeToBar" ? qsTr("Minimize") : rb.tooltip
            tooltipHovered: tap.containsMouse
            androidTooltipPolicy: rb.androidCaption.length ? "caption" : "drop"
            androidCaptionText: rb.androidCaption
            focusRadius: rb.size / 2
            onTriggered: rb.tapped()
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: bar.tight ? 16 : 28
        anchors.top: parent.top
        anchors.topMargin: bar.tight ? 12 : 18
        spacing: 14

        BarButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "back"
            tooltip: "Back"
            onTapped: bar.backRequested()
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: "NOW PLAYING"
                visible: !bar.tight
                color: bar.theme ? bar.theme.inkDimmer : "#9a99a5"
                font.family: "Segoe UI"
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 2
            }
            Text {
                text: bar.title
                color: bar.theme ? bar.theme.ink : "#f7f7f5"
                font.family: "Segoe UI"
                font.pixelSize: bar.tight ? 16 : 19
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth, bar.width * 0.5)
            }
            Text {
                text: bar.subtitle
                visible: text.length > 0 && !bar.tight
                color: bar.theme ? bar.theme.inkDim : "#c9c8d0"
                font.family: "Segoe UI"
                font.pixelSize: 12
                elide: Text.ElideRight
                width: Math.min(implicitWidth, bar.width * 0.4)
            }
        }
    }

    // The live wall clock, left of the window verbs — the one place the player tells you the time.
    Text {
        id: nowClockLabel
        anchors.right: titleBarVerbs.left
        anchors.rightMargin: bar.tight ? 14 : 20
        anchors.verticalCenter: titleBarVerbs.verticalCenter
        visible: !bar.tight && bar.nowClock.length > 0
        text: bar.nowClock
        color: bar.theme ? bar.theme.inkDim : "#c9c8d0"
        font.family: "Segoe UI"
        font.pixelSize: 13
        font.letterSpacing: 0.5
        font.features: ({ "tnum": 1 })
        style: Text.Raised
        styleColor: Qt.rgba(0, 0, 0, 0.45)
    }

    Row {
        id: titleBarVerbs
        anchors.right: parent.right
        anchors.rightMargin: bar.tight ? 12 : 20
        anchors.top: parent.top
        anchors.topMargin: bar.tight ? 10 : 16
        spacing: 6

        BarButton {
            icon: "minimizeToBar"
            tooltip: "Minimize — paused in the taskbar, resumes with no reload"
            androidCaption: qsTr("Minimize")
            onTapped: bar.minimizeRequested()
        }
        BarButton {
            icon: "cancel"
            tooltip: "Close"
            onTapped: bar.closeRequested()
        }
    }
}
