// ChromeScrim — a subtle dark gradient band pinned to the top of a page so the back button and
// window controls (light glyphs) stay legible against ANY background: bright cover/banner art OR
// dark wallpaper. Light chrome + a top scrim reads on any colour — the standard OS status-bar
// pattern. Drop one instance into a page's chrome with a z BELOW the controls (e.g. z: 16, while
// back/window controls sit at z: 20/30). App-wide: any page that floats controls over content
// adds this one line.
import QtQuick

Rectangle {
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: 120
    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
        GradientStop { position: 1.0; color: "transparent" }
    }
}
