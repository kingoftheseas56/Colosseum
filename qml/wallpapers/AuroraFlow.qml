// AuroraFlow — a living gradient wallpaper.
//
// Ported (2026-07-20) from the KDE Plasma wallpaper plugin
// VicenteMcMahon/kde-plasma-gradient-wallpaper (LGPL-2.1-or-later). The KDE-specific
// pieces were stripped for a plain Qt6 build: no `WallpaperItem`/org.kde.plasma root,
// no `wallpaper.configuration` object (colours are fixed here), and Qt5's
// QtGraphicalEffects LinearGradient is replaced by Qt Quick Shapes. What survives is
// the original idea: two gradient states the scene breathes between, forever.
//
// Scene-graph only (no Canvas), and freeze-gated via `running` so it stops on
// immersive surfaces / minimize like the other native scenes (motion doctrine).
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    anchors.fill: parent
    property bool running: true

    property color c1: "#141f47"
    property color c2: "#4e2585"
    property color c3: "#1a5f7a"
    property real glowX: 0.34
    property real glowY: 0.30

    SequentialAnimation {
        running: root.running
        loops: Animation.Infinite
        ParallelAnimation {
            ColorAnimation { target: root; property: "c1"; to: "#213a74"; duration: 8000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "c2"; to: "#6f37a6"; duration: 8000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "c3"; to: "#278f98"; duration: 8000; easing.type: Easing.InOutSine }
            NumberAnimation { target: root; property: "glowX"; to: 0.70; duration: 8000; easing.type: Easing.InOutSine }
            NumberAnimation { target: root; property: "glowY"; to: 0.62; duration: 8000; easing.type: Easing.InOutSine }
        }
        ParallelAnimation {
            ColorAnimation { target: root; property: "c1"; to: "#141f47"; duration: 8000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "c2"; to: "#4e2585"; duration: 8000; easing.type: Easing.InOutSine }
            ColorAnimation { target: root; property: "c3"; to: "#1a5f7a"; duration: 8000; easing.type: Easing.InOutSine }
            NumberAnimation { target: root; property: "glowX"; to: 0.34; duration: 8000; easing.type: Easing.InOutSine }
            NumberAnimation { target: root; property: "glowY"; to: 0.30; duration: 8000; easing.type: Easing.InOutSine }
        }
    }

    // base diagonal gradient
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: root.width; y2: root.height
                GradientStop { position: 0.0; color: root.c1 }
                GradientStop { position: 0.5; color: root.c2 }
                GradientStop { position: 1.0; color: root.c3 }
            }
            startX: 0; startY: 0
            PathLine { x: root.width; y: 0 }
            PathLine { x: root.width; y: root.height }
            PathLine { x: 0; y: root.height }
        }
    }
    // soft drifting glow for mesh-gradient depth
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: root.width * root.glowX; centerY: root.height * root.glowY
                centerRadius: Math.max(root.width, root.height) * 0.55
                focalX: root.width * root.glowX; focalY: root.height * root.glowY
                GradientStop { position: 0.0; color: Qt.rgba(0.62, 0.5, 1.0, 0.30) }
                GradientStop { position: 1.0; color: Qt.rgba(0.62, 0.5, 1.0, 0.0) }
            }
            startX: 0; startY: 0
            PathLine { x: root.width; y: 0 }
            PathLine { x: root.width; y: root.height }
            PathLine { x: 0; y: root.height }
        }
    }
}
