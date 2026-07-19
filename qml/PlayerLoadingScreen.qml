import QtQuick
import QtQuick.Effects

// Harbor/Stremio-style per-show startup loader. Full-bleed episode art (blurred, dimmed, saturated)
// under a three-stop black gradient; a centered show logo OR title; an uppercase episode line; a
// status line; and a thin INDETERMINATE bar. Colosseum exposes no trustworthy torrent readiness
// figure, so this NEVER shows a fabricated number — the bar just sweeps.
//
// Owned/fed by PlayerPage: `active` gates the whole thing; PlayerPage flips it off on the truthful
// first-frame advance (or when the resume-choice prompt must take over). Art decode + blur run ONLY
// while active, so nothing keeps decoding once the loader is hidden.
Item {
    id: root

    property string title: ""
    property string episodeLine: ""
    property url    logoUrl: ""
    property url    backdropUrl: ""
    property string statusText: ""
    property bool   active: false
    property bool   errored: false
    // Passed in from PlayerPage (theme.hud) so this file never depends on cross-file `theme` scope.
    property string hudFamily: "Segoe UI"
    property string errorText: ""

    signal cancelRequested()

    visible: opacity > 0.001
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

    // Base black — never a transparent gap before the art decodes or when there is no art.
    Rectangle { anchors.fill: parent; color: "#05070b" }

    // Full-bleed episode/backdrop art: async + cached, bounded source size, decoded ONLY while
    // active. Blurred/dimmed/desaturated so text stays legible; best-effort (missing art = black).
    Image {
        id: art
        anchors.fill: parent
        source: root.active ? root.backdropUrl : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        visible: false
        sourceSize.width: Math.min(1920, Math.max(2, Math.round(root.width)))
        sourceSize.height: Math.min(1080, Math.max(2, Math.round(root.height)))
    }
    MultiEffect {
        anchors.fill: art
        source: art
        visible: root.active && art.status === Image.Ready
        blurEnabled: root.active
        blur: 1.0
        blurMax: 40
        saturation: 0.4
        brightness: -0.10
        opacity: 0.55
    }

    // Three-stop black gradient: legible text, bottom darkest (approximates Harbor's overlay).
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.65) }
            GradientStop { position: 0.5; color: Qt.rgba(0, 0, 0, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
        }
    }

    // Center identity: logo when it has arrived, else the title in the HUD face.
    Column {
        anchors.centerIn: parent
        width: parent.width * 0.72
        spacing: 16

        Image {
            id: logo
            anchors.horizontalCenter: parent.horizontalCenter
            source: root.active ? root.logoUrl : ""
            visible: root.logoUrl.toString().length > 0 && status === Image.Ready
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
            height: 44
            width: Math.min(implicitWidth, parent.width)
            sourceSize.height: 88
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !logo.visible && root.title.length > 0
            width: root.width * 0.72
            text: root.title
            color: "#f7f7f5"
            font.family: root.hudFamily
            font.pixelSize: Math.max(30, Math.min(60, root.width * 0.034))
            font.weight: Font.DemiBold
            lineHeight: 1.05
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.episodeLine.length > 0
            text: root.episodeLine.toUpperCase()
            color: "#c9c8d0"
            font.family: root.hudFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 3
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Bottom: status line + indeterminate bar (hidden on error; retry lives in PlayerPage).
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(root.height * 0.12)
        spacing: 14
        width: Math.min(root.width * 0.40, 440)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.errored ? (root.errorText.length > 0) : (root.statusText.length > 0)
            text: root.errored ? root.errorText : root.statusText
            color: root.errored ? "#e6b8b0" : "#9a99a5"
            font.family: root.hudFamily
            font.pixelSize: 13
            font.letterSpacing: 1
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: parent.width
        }

        // Thin INDETERMINATE bar: a 40% white segment sweeps across a muted track, continuously.
        // No fabricated figure — there is no trustworthy torrent readiness number to show.
        Item {
            id: bar
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            height: 2
            visible: !root.errored
            clip: true
            Rectangle { anchors.fill: parent; radius: 1; color: Qt.rgba(1, 1, 1, 0.14) }
            Rectangle {
                id: seg
                width: parent.width * 0.4
                height: parent.height
                radius: 1
                color: Qt.rgba(1, 1, 1, 0.85)
                x: -width
                SequentialAnimation on x {
                    running: root.active && !root.errored
                    loops: Animation.Infinite
                    NumberAnimation { from: -seg.width; to: bar.width; duration: 1100; easing.type: Easing.InOutSine }
                }
            }
        }
    }

    // Cancel affordance (the chrome's back button is hidden while loading): emits cancelRequested,
    // which PlayerPage wires to the SAME back/cancel/session-parking action — no second shutdown path.
    Item {
        id: cancelBtn
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 28
        anchors.topMargin: 24
        width: 40
        height: 40
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: cancelHover.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06)
        }
        PlayerIcon { anchors.fill: parent; kind: "back"; ink: "#f7f7f5" }
        MouseArea {
            id: cancelHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.cancelRequested()
        }
    }

    // Full-bleed click-swallow so taps on the loader never fall through to the video surface.
    MouseArea { anchors.fill: parent; z: -1; onClicked: {} }
}
