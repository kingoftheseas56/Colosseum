import QtQuick
import QtQuick.Controls.Basic

ProgressBar {
    id: progress
    padding: 0
    implicitHeight: 6
    indeterminate: true

    Theme { id: theme }

    background: Rectangle {
        radius: height / 2
        color: Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.15)
    }
    contentItem: Item {
        id: track
        clip: true
        Rectangle {
            id: segment
            width: Math.max(8, track.width * 0.35)
            height: track.height
            radius: height / 2
            color: theme.gold
            x: -width
            NumberAnimation on x {
                running: progress.visible
                from: -segment.width
                to: track.width
                duration: 1500
                loops: Animation.Infinite
                easing.type: Easing.InOutSine
            }
        }
    }
}
