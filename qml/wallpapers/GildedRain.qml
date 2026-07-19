// GildedRain.qml — "Gilded Rain", Colosseum's second NATIVE living wallpaper
// (companion to ArenaNight: chosen by Hemanth 2026-07-19). Thin gold rain streaks
// falling down dark glass at varied speeds, soft splash-rings pooling where they
// land, and a faint gold glow breathing at the base. Calm, vertical — the deliberate
// counterpoint to the Arena's rising embers.
//
// Same doctrine as ArenaNight: everything is scene-graph work — Rectangles, gradients,
// transform/opacity animations — NEVER a per-frame Canvas repaint, so the GPU composites
// it like any other page. `running: false` freezes every animation dead; the host binds
// it away whenever a reader/player owns the screen or the window is minimized (ambient
// motion only while the shell is actually being looked at).
//
// [Agent 5 (Claude), shell]
import QtQuick

Item {
    id: rain

    property bool running: true

    // house palette — the Arena's black + ember gold, kept identical so the two
    // living wallpapers read as one set.
    readonly property color inkbg: "#07070a"
    readonly property color gold: "#e0ba60"

    Rectangle { anchors.fill: parent; color: rain.inkbg }

    // ---- a faint gold pool-glow at the base, breathing (felt more than seen) ----
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: rain.height * 0.28
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0.88, 0.73, 0.38, 0.10) }
        }
        SequentialAnimation on opacity {
            running: rain.running
            loops: Animation.Infinite
            NumberAnimation { from: 0.55; to: 1.0; duration: 6400; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.0; to: 0.55; duration: 6400; easing.type: Easing.InOutSine }
        }
    }

    // ---- the rain: thin gold streaks, each a soft top→bottom gradient so it reads as
    //      a falling drop-trail rather than a hard bar. Per-streak character rolled once
    //      at creation (lane, speed, length, brightness) so no two share a rhythm. ----
    Repeater {
        model: 66
        delegate: Item {
            id: streak
            required property int index
            readonly property real lane: Math.random()
            readonly property real speedMs: 1500 + Math.random() * 2600
            readonly property real len: 46 + Math.random() * 64
            readonly property real bright: 0.14 + Math.random() * 0.26
            width: 1 + Math.random() * 1.4
            height: len
            x: lane * rain.width
            y: -len

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.75; color: Qt.rgba(0.88, 0.73, 0.38, streak.bright) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.95, 0.82, 0.48, streak.bright * 1.3) }
                }
            }

            SequentialAnimation on y {
                running: rain.running
                loops: Animation.Infinite
                // stagger the first fall so the field starts full, never a synced sheet
                PauseAnimation { duration: (streak.index % 22) * 190 }
                NumberAnimation { from: -streak.len; to: rain.height + streak.len; duration: streak.speedMs; easing.type: Easing.InQuad }
            }
        }
    }

    // ---- splash rings: where drops land, a thin gold ring blooms and fades. Drawn as a
    //      bordered Rectangle (radius = half → a circle), scaled up while fading — no Shape,
    //      no Canvas. Each emitter sits at a fixed lane along the base and pulses on its
    //      own staggered loop, so landings feel scattered, not metronomic. ----
    Repeater {
        model: 11
        delegate: Rectangle {
            id: ring
            required property int index
            readonly property real lane: (index + 0.5) / 11 + (Math.random() - 0.5) * 0.06
            readonly property real baseSize: 10 + Math.random() * 10
            width: baseSize
            height: baseSize
            radius: baseSize / 2
            color: "transparent"
            border.width: 1
            border.color: rain.gold
            antialiasing: true
            x: lane * rain.width - baseSize / 2
            y: rain.height * (0.80 + Math.random() * 0.16) - baseSize / 2
            opacity: 0
            transformOrigin: Item.Center

            SequentialAnimation {
                running: rain.running
                loops: Animation.Infinite
                PauseAnimation { duration: 900 + ring.index * 640 + Math.random() * 1200 }
                ParallelAnimation {
                    NumberAnimation { target: ring; property: "scale"; from: 0.2; to: 2.4; duration: 1700; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        NumberAnimation { target: ring; property: "opacity"; from: 0.0; to: 0.42; duration: 420; easing.type: Easing.OutSine }
                        NumberAnimation { target: ring; property: "opacity"; from: 0.42; to: 0.0; duration: 1280; easing.type: Easing.InSine }
                    }
                }
                PauseAnimation { duration: 1800 + Math.random() * 2600 }
            }
        }
    }
}
