// ArenaNight.qml — "The Arena at Night", Colosseum's first NATIVE living wallpaper
// (ratified from the 2026-07-18 mock: tiers of glass rising into the dark, gold
// embers drifting, a rare beam sweeping the tiers).
//
// Everything is scene-graph work — Shapes, item opacity, transform animations —
// never a per-frame Canvas repaint, so the GPU composites it like any other page.
// `running: false` freezes every animation dead: the host binds it away whenever
// a reader/player owns the screen or the window is minimized, per the ambient-
// motion doctrine (motion only while the shell is actually being looked at).
//
// [Agent 0 (Claude), shell]
import QtQuick
import QtQuick.Shapes

Item {
    id: arena

    property bool running: true

    // The arena's virtual center sits below the frame; tiers are ellipse arcs
    // rising toward the top, wider than tall (the mock's 1.35 ratio).
    readonly property real cx: width / 2
    readonly property real cy: height * 1.28

    Rectangle { anchors.fill: parent; color: "#07070a" }

    // ---- the tiers: 10 white hairline arcs, each with a short gold keystone ----
    Repeater {
        model: 10
        delegate: Item {
            id: tier
            required property int index
            anchors.fill: parent
            // breathe: a slow, staggered opacity swell — felt more than seen
            SequentialAnimation on opacity {
                running: arena.running
                loops: Animation.Infinite
                NumberAnimation { from: 0.55; to: 1.0; duration: 5200 + tier.index * 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.0; to: 0.55; duration: 5200 + tier.index * 700; easing.type: Easing.InOutSine }
            }

            readonly property real rad: arena.height * (0.42 + tier.index * 0.155)

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.030 + tier.index * 0.006)
                    strokeWidth: 1 + tier.index * 0.2
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: arena.cx; centerY: arena.cy
                        radiusX: tier.rad * 1.35; radiusY: tier.rad
                        startAngle: 194; sweepAngle: 152
                    }
                }
                ShapePath {
                    strokeColor: Qt.rgba(0.79, 0.64, 0.29, 0.10)
                    strokeWidth: 1.2 + tier.index * 0.2
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: arena.cx; centerY: arena.cy
                        radiusX: tier.rad * 1.35; radiusY: tier.rad
                        startAngle: 263; sweepAngle: 14
                    }
                }
            }
        }
    }

    // ---- the beam: once every ~20s a faint gold shaft sweeps the tiers ----
    Item {
        id: beam
        x: arena.cx
        y: arena.cy
        width: arena.height * 1.9
        height: 3
        opacity: 0
        transformOrigin: Item.Left

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.rgba(0.79, 0.64, 0.29, 0.30) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        SequentialAnimation {
            running: arena.running
            loops: Animation.Infinite
            PauseAnimation { duration: 15600 }
            ParallelAnimation {
                RotationAnimation { target: beam; from: 194; to: 346; duration: 4400; easing.type: Easing.InOutSine }
                SequentialAnimation {
                    NumberAnimation { target: beam; property: "opacity"; from: 0; to: 1; duration: 2200; easing.type: Easing.InOutSine }
                    NumberAnimation { target: beam; property: "opacity"; from: 1; to: 0; duration: 2200; easing.type: Easing.InOutSine }
                }
            }
        }
    }

    // ---- the embers: sparse gold motes drifting up, twinkling out of phase ----
    Repeater {
        model: 42
        delegate: Rectangle {
            id: ember
            required property int index
            // per-ember character rolled once at creation; the lanes stay organic
            // because no two embers share speed, size, phase, or twinkle rate
            readonly property real lane: Math.random()
            readonly property real speedMs: 26000 + Math.random() * 34000
            width: 1.5 + Math.random() * 2
            height: width
            radius: width / 2
            color: "#e0ba60"
            x: lane * arena.width
            opacity: 0

            SequentialAnimation on y {
                running: arena.running
                loops: Animation.Infinite
                PauseAnimation { duration: ember.index * 260 }   // stagger the first wave
                NumberAnimation {
                    from: arena.height * (0.15 + (ember.index % 7) * 0.13)
                    to: -8
                    duration: ember.speedMs
                }
                PropertyAction { value: arena.height + 8 }
                NumberAnimation { from: arena.height + 8; to: -8; duration: ember.speedMs }
            }
            SequentialAnimation on opacity {
                running: arena.running
                loops: Animation.Infinite
                PauseAnimation { duration: ember.index * 260 }
                NumberAnimation { from: 0.10; to: 0.30 + (ember.index % 5) * 0.09; duration: 1400 + (ember.index % 6) * 500; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.10; duration: 1400 + (ember.index % 6) * 500; easing.type: Easing.InOutSine }
            }
        }
    }
}
