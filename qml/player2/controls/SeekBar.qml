import QtQuick

// The scrubber. It reads the engine-fed position/duration/chapters from the C++ session and issues a
// typed seekExact() on release — NO Timer ever mutates position (house doctrine). A drag only sets a
// local preview; the truth stays session.position until the seek lands.
Item {
    id: root

    property var session
    property QtObject theme
    // The streaming cache strip. The session reports how far the transport has actually buffered, in
    // seconds, and -1 when the question does not apply — a local file, or an origin that never
    // declared its length. -1 collapses this to 0 width, which is how the shipped player behaves for
    // local playback: it hides the strip rather than painting a phantom fill over a file that is
    // already on disk (his ruling, 2026-07-20). So no local-path plumbing is needed here; the engine
    // answers honestly and the bar just draws what it is told.
    property real bufferedFraction: (session && session.bufferedSeconds >= 0 && root.dur > 0)
                                    ? Math.max(0, Math.min(1, session.bufferedSeconds / root.dur))
                                    : 0
    property bool seeking: false
    property real previewSeconds: 0

    implicitHeight: 22

    readonly property real dur: session && session.duration > 0 ? session.duration : 0
    readonly property real livePos: session ? session.position : 0
    readonly property real shownPos: seeking ? previewSeconds : livePos
    readonly property real frac: dur > 0 ? Math.max(0, Math.min(1, shownPos / dur)) : 0
    readonly property bool active: hover.containsMouse || seeking || root.activeFocus
    readonly property real value: root.shownPos
    readonly property real minimumValue: 0
    readonly property real maximumValue: root.dur
    readonly property real stepSize: 10

    focusPolicy: root.dur > 0 ? Qt.TabFocus : Qt.NoFocus
    Keys.onPressed: function(event) {
        if (!root.session || root.dur <= 0) return
        var step = (event.modifiers & Qt.ShiftModifier) ? 1 : 10
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) { root.session.seekRelative(-step); event.accepted = true }
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) { root.session.seekRelative(step); event.accepted = true }
        else if (event.key === Qt.Key_Home) { root.session.seekExact(0); event.accepted = true }
        else if (event.key === Qt.Key_End) { root.session.seekExact(Math.max(0, root.dur - 0.5)); event.accepted = true }
    }
    Accessible.role: Accessible.Slider
    Accessible.name: "Seek"

    function previewAt(x) {
        return dur * Math.max(0, Math.min(1, x / Math.max(1, barVisual.width)))
    }
    function fmtTime(s) {
        if (!isFinite(s) || s < 0) s = 0
        var t = Math.floor(s)
        var h = Math.floor(t / 3600)
        var m = Math.floor((t % 3600) / 60)
        var sec = t % 60
        var mm = (h > 0 && m < 10) ? "0" + m : String(m)
        var ss = sec < 10 ? "0" + sec : String(sec)
        return h > 0 ? h + ":" + mm + ":" + ss : m + ":" + ss
    }

    // The thin visual bar, vertically centered inside the taller grab area.
    Item {
        id: barVisual
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: root.active ? 5 : 3
        Behavior on height { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        Rectangle { // track
            anchors.fill: parent
            radius: height / 2
            color: Qt.rgba(1, 1, 1, 0.16)
        }
        Rectangle { // buffered cache
            height: parent.height
            radius: height / 2
            color: Qt.rgba(1, 1, 1, 0.30)
            width: parent.width * Math.max(0, Math.min(1, root.bufferedFraction))
            visible: width > 2
        }
        Rectangle { // progress
            height: parent.height
            radius: height / 2
            color: root.theme ? root.theme.gold : "#f0c44a"
            width: parent.width * root.frac
        }
        Repeater { // chapter ticks
            model: root.session ? root.session.chapters : []
            Rectangle {
                required property var modelData
                width: 2
                height: barVisual.height + (root.active ? 3 : 2)
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(1, 1, 1, 0.55)
                visible: root.dur > 0 && modelData.start > 1
                x: root.dur > 0 ? barVisual.width * (modelData.start / root.dur) - width / 2 : 0
            }
        }
    }

    Rectangle { // handle dot
        id: handle
        width: root.seeking ? 14 : 11
        height: width
        radius: width / 2
        color: root.theme ? root.theme.gold : "#f0c44a"
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.32)
        anchors.verticalCenter: parent.verticalCenter
        x: barVisual.width * root.frac - width / 2
        visible: root.dur > 0
        Behavior on width { NumberAnimation { duration: 90 } }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: function(mouse) {
            root.seeking = true
            root.previewSeconds = root.previewAt(mouse.x)
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.previewSeconds = root.previewAt(mouse.x)
        }
        onReleased: function(mouse) {
            if (root.session && root.dur > 0)
                root.session.seekExact(root.previewAt(mouse.x))
            root.seeking = false
        }
    }

    // Hover / scrub time card floating above the bar.
    Rectangle {
        id: tip
        visible: hover.containsMouse || root.seeking
        color: Qt.rgba(0, 0, 0, 0.86)
        radius: 7
        height: 26
        width: tipText.implicitWidth + 20
        y: -(height + 4)
        x: Math.max(0, Math.min(root.width - width,
                    (root.seeking ? barVisual.width * root.frac : hover.mouseX) - width / 2))
        Text {
            id: tipText
            anchors.centerIn: parent
            text: root.fmtTime(root.seeking ? root.previewSeconds : root.previewAt(hover.mouseX))
            color: root.theme ? root.theme.ink : "#f7f7f5"
            font.family: "Segoe UI"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
        }
    }
}
