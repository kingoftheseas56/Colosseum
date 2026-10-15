import QtQuick
import QtQuick.Effects

// Stremio-style per-show startup loader. Reference = Stremio's Player (stremio-web): a FULL-BLEED
// backdrop shown CLEARLY (not blurred; only subtle top/bottom gradients) with the show's CINEMETA
// LOGO — the stylized title art — centered as the hero. Text is only a fallback when no logo loads.
// Beneath: the episode line, a status line, and a thin INDETERMINATE bar. Colosseum exposes no
// trustworthy torrent readiness figure, so the bar only sweeps — it NEVER shows a fabricated number.
//
// Owned/fed by PlayerPage: `active` gates everything; PlayerPage flips it off on the truthful
// first-frame advance (or when the resume-choice prompt takes over). Art decode runs ONLY while
// active. Harbor shows the same logo with no backdrop; we keep Stremio's backdrop.
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

    // Base black — never a transparent gap before art decodes or when there is no backdrop.
    Rectangle { anchors.fill: parent; color: "#000000" }

    // Full-bleed backdrop, shown CLEARLY (Stremio shows meta.background sharp, not blurred).
    Image {
        id: backdrop
        anchors.fill: parent
        source: root.active ? root.backdropUrl : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        visible: root.active && status === Image.Ready
        opacity: 0.9
        sourceSize.width: Math.min(1920, Math.max(2, Math.round(root.width)))
        sourceSize.height: Math.min(1080, Math.max(2, Math.round(root.height)))
    }
    // A gentle overall darken so the (usually light) logo always reads over the backdrop.
    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.28) }
    // Subtle top + bottom gradients (Stremio's 0.35), clear through the middle.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0;  color: Qt.rgba(0, 0, 0, 0.55) }
            GradientStop { position: 0.30; color: Qt.rgba(0, 0, 0, 0.0) }
            GradientStop { position: 0.72; color: Qt.rgba(0, 0, 0, 0.0) }
            GradientStop { position: 1.0;  color: Qt.rgba(0, 0, 0, 0.60) }
        }
    }

    // HERO: the Cinemeta logo (stylized title art), centered and prominent. Fallback = the title.
    // Both breathe with a slow pulse (Stremio/Harbor's loader pulse).
    Column {
        id: identity
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: Math.round(-root.height * 0.05)
        width: parent.width * 0.72
        spacing: 18

        SequentialAnimation on opacity {
            running: root.active
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.82; duration: 1500; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.82; to: 1.0; duration: 1500; easing.type: Easing.InOutSine }
        }

        Image {
            id: logo
            anchors.horizontalCenter: parent.horizontalCenter
            source: root.active ? root.logoUrl : ""
            visible: root.logoUrl.toString().length > 0 && status === Image.Ready
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
            // Prominent, like Stremio/Harbor: up to ~30% of frame height, 72% width.
            height: Math.round(Math.min(220, root.height * 0.30))
            width: Math.min(implicitWidth, identity.width)
            sourceSize.height: 440
            // Soft shadow so a light logo reads on any backdrop.
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Qt.rgba(0, 0, 0, 0.7)
                shadowBlur: 0.9
                shadowVerticalOffset: 10
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !logo.visible && root.title.length > 0
            width: identity.width
            text: root.title
            color: "#ffffff"
            font.family: root.hudFamily
            font.pixelSize: Math.max(34, Math.min(64, root.width * 0.036))
            font.weight: Font.DemiBold
            lineHeight: 1.0
            style: Text.Raised
            styleColor: Qt.rgba(0, 0, 0, 0.55)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.episodeLine.length > 0
            text: root.episodeLine.toUpperCase()
            color: "#e2e2e6"
            font.family: root.hudFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 4
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Bottom: status line + indeterminate bar (hidden on error; retry lives in PlayerPage).
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(root.height * 0.11)
        spacing: 14
        width: Math.min(root.width * 0.40, 440)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.errored ? (root.errorText.length > 0) : (root.statusText.length > 0)
            text: root.errored ? root.errorText : root.statusText.toUpperCase()
            color: root.errored ? "#e6b8b0" : "#c8c8ce"
            font.family: root.hudFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 3
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
        KeyboardAction {
            id: cancelKeyboard
            anchors.fill: parent
            pointerEnabled: false
            focusEnabled: root.active
            accessibleName: qsTr("Cancel loading")
            focusRadius: width / 2
            onTriggered: root.cancelRequested()
        }
    }

    // Full-bleed click-swallow so taps on the loader never fall through to the video surface.
    MouseArea { anchors.fill: parent; z: -1; onClicked: {} }
}
