// GildedRainHeavy.qml — MOCKUP: the "heavier" Gilded Rain, for Hemanth to preview
// before we decide whether it replaces / joins the shipped Gilded Rain (2026-07-19).
//
// Not registered anywhere yet — a standalone preview scene. Launch it with qml.exe to
// watch it fullscreen. If it lands, it folds into the native-wallpaper registry the same
// way GildedRain.qml does (running gate + nativeSceneFor route already provided for).
//
// Design intent (frontend-design pass): a DOWNPOUR, not just "more drops". Three depth
// layers (near/mid/far) falling on a gentle ~9 degree wind-slant, denser splash-rings at
// the base, and a faint haze so near streaks emerge from depth. Same house black + ember
// gold as ArenaNight/GildedRain so all three read as one set. Pure scene-graph — Rectangles,
// gradients, transform/opacity animations, NEVER a per-frame Canvas repaint. `running:false`
// freezes it dead (ambient-motion doctrine).
//
// [Agent 5 (Claude), shell]
import QtQuick

Item {
    id: rain
    // sensible preview size when launched bare via qml.exe; a real host anchors.fill it
    implicitWidth: 1600
    implicitHeight: 900

    property bool running: true

    // house palette — split into three depths so parallax, not sheer count, sells "heavy"
    readonly property color inkbg: "#07070a"
    readonly property color goldNear: "#f2d98a"   // brightest, closest
    readonly property color goldMid:  "#e0ba60"   // the body of the rain
    readonly property color goldFar:  "#b8923f"   // dim, farthest

    readonly property real slantDeg: 9            // the storm-lean — the signature move

    Rectangle { anchors.fill: parent; color: rain.inkbg }

    // ---- atmospheric haze: a faint vertical wash so near rain reads as emerging from
    //      depth rather than floating on flat black. Heavier rain carries this mist. ----
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.10, 0.09, 0.07, 0.0) }
            GradientStop { position: 0.55; color: Qt.rgba(0.12, 0.10, 0.07, 0.06) }
            GradientStop { position: 1.0; color: Qt.rgba(0.14, 0.11, 0.07, 0.12) }
        }
    }

    // ---- the rain field: one container tilted by the wind-slant, oversized + clipped so
    //      the corners stay covered. Every streak inside falls straight "down" in this
    //      rotated frame, so the WHOLE downpour leans uniformly — cheaper and cleaner than
    //      slanting each streak. Splashes live OUTSIDE this (upright, in screen space). ----
    Item {
        id: field
        anchors.centerIn: parent
        width: parent.width * 1.5
        height: parent.height * 1.5
        rotation: rain.slantDeg
        clip: false

        // one reusable rain layer — count/size/speed/brightness set the depth character
        component RainLayer: Repeater {
            id: layer
            required property int count
            required property color tone
            required property real wMin
            required property real wSpan
            required property real lenMin
            required property real lenSpan
            required property real brightMin
            required property real brightSpan
            required property real speedMin
            required property real speedSpan
            property Item host: field
            model: count
            delegate: Item {
                id: streak
                required property int index
                readonly property real lane: Math.random()
                readonly property real len: layer.lenMin + Math.random() * layer.lenSpan
                readonly property real bright: layer.brightMin + Math.random() * layer.brightSpan
                readonly property real speedMs: layer.speedMin + Math.random() * layer.speedSpan
                width: layer.wMin + Math.random() * layer.wSpan
                height: len
                x: lane * layer.host.width
                y: -len

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.72; color: Qt.rgba(layer.tone.r, layer.tone.g, layer.tone.b, streak.bright) }
                        GradientStop { position: 1.0; color: Qt.rgba(layer.tone.r, layer.tone.g, layer.tone.b, Math.min(1, streak.bright * 1.35)) }
                    }
                }

                SequentialAnimation on y {
                    running: rain.running
                    loops: Animation.Infinite
                    // dense stagger so the field starts full, never a synced sheet
                    PauseAnimation { duration: (streak.index % 26) * 120 }
                    NumberAnimation { from: -streak.len; to: layer.host.height + streak.len; duration: streak.speedMs; easing.type: Easing.InQuad }
                }
            }
        }

        // FAR — many, thin, dim, slow: the deep backdrop of the downpour
        RainLayer { count: 66; tone: rain.goldFar;  wMin: 0.6; wSpan: 0.7; lenMin: 28; lenSpan: 34; brightMin: 0.05; brightSpan: 0.11; speedMin: 2700; speedSpan: 1700 }
        // MID — the body of the rain
        RainLayer { count: 88; tone: rain.goldMid;  wMin: 1.0; wSpan: 0.9; lenMin: 46; lenSpan: 52; brightMin: 0.14; brightSpan: 0.22; speedMin: 1500; speedSpan: 1300 }
        // NEAR — few, thick, bright, fast, long: the streaks that read as "heavy, right here"
        RainLayer { count: 24; tone: rain.goldNear; wMin: 1.8; wSpan: 1.4; lenMin: 92; lenSpan: 66; brightMin: 0.30; brightSpan: 0.26; speedMin: 850;  speedSpan: 700 }
    }

    // ---- base pool-glow, breathing — a touch stronger than the calm variant ----
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: rain.height * 0.30
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0.88, 0.73, 0.38, 0.14) }
        }
        SequentialAnimation on opacity {
            running: rain.running
            loops: Animation.Infinite
            NumberAnimation { from: 0.6; to: 1.0; duration: 5200; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.0; to: 0.6; duration: 5200; easing.type: Easing.InOutSine }
        }
    }

    // ---- splash rings: doubled up for the heavier landing rate. Bordered Rectangles
    //      (radius = half → circle) scaled up while fading; no Shape, no Canvas. Upright in
    //      screen space so the pooling sits along the real floor, not the tilted field. ----
    Repeater {
        model: 20
        delegate: Rectangle {
            id: ring
            required property int index
            readonly property real lane: (index + 0.5) / 20 + (Math.random() - 0.5) * 0.05
            readonly property real baseSize: 9 + Math.random() * 11
            width: baseSize
            height: baseSize
            radius: baseSize / 2
            color: "transparent"
            border.width: 1
            border.color: rain.goldMid
            antialiasing: true
            x: lane * rain.width - baseSize / 2
            y: rain.height * (0.82 + Math.random() * 0.15) - baseSize / 2
            opacity: 0
            transformOrigin: Item.Center

            SequentialAnimation {
                running: rain.running
                loops: Animation.Infinite
                PauseAnimation { duration: 400 + ring.index * 300 + Math.random() * 700 }
                ParallelAnimation {
                    NumberAnimation { target: ring; property: "scale"; from: 0.2; to: 2.6; duration: 1500; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        NumberAnimation { target: ring; property: "opacity"; from: 0.0; to: 0.44; duration: 360; easing.type: Easing.OutSine }
                        NumberAnimation { target: ring; property: "opacity"; from: 0.44; to: 0.0; duration: 1140; easing.type: Easing.InSine }
                    }
                }
                PauseAnimation { duration: 900 + Math.random() * 1700 }
            }
        }
    }
}
