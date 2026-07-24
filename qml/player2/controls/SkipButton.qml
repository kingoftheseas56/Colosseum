import QtQuick
import "Player2Browser.js" as Browser

// SkipButton — the "Skip Intro / Recap / Credits" affordance. Behaviour and look mirror the main
// player's Feature-4 skip pill: a MONOCHROME dark pill, bottom-right, text-only ("Skip Intro" + a dim
// "Skip"), that FADES WITH THE CHROME. It appears only while the playhead sits inside a host-resolved
// skip segment and seeks past it on tap; an auto-skip segment jumps itself once on entry. Pure Player 2
// house style — segments + position in, typed seek out; the active segment is derived by
// Player2Browser.js (headless-tested), the same active-segment rule the main player uses.
Item {
    id: skip
    anchors.fill: parent

    property QtObject theme
    property var segments: []
    property real positionSeconds: 0
    property bool chromeShown: true   // yields to the chrome auto-hide, like the main player's pill
    // `enabled` is Item's own property: the shell clears it while a menu/drawer is open, which both
    // hides the pill (via `shown`) and stops it intercepting input.

    signal skipRequested(real toSeconds)

    readonly property var seg: Browser.activeSegment(skip.segments, skip.positionSeconds)
    readonly property bool shown: skip.enabled && skip.chromeShown && skip.seg !== null

    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDim: theme ? theme.inkDim : "#c9c8d0"

    // An auto-skip segment jumps itself the moment it becomes active (fires once — `seg` holds the same
    // object while the playhead stays inside it, so this doesn't re-trigger until the segment changes).
    onSegChanged: {
        if (skip.enabled && skip.seg && skip.seg.autoSkip === true)
            skip.skipRequested(Number(skip.seg.endSeconds))
    }

    Rectangle {
        id: pill
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 176   // above the transport dock, matching the main player
        width: row.implicitWidth + 28
        height: 42
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.82)
        border.width: 1
        border.color: pillMa.containsMouse ? Qt.rgba(1, 1, 1, 0.30) : Qt.rgba(1, 1, 1, 0.18)

        opacity: skip.shown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: skip.seg ? Browser.skipLabel(skip.seg.kind) : ""
                color: skip.ink; font.family: "Segoe UI"; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Skip"
                color: skip.inkDim; font.family: "Segoe UI"; font.pixelSize: 12
            }
        }

        MouseArea {
            id: pillMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: skip.shown
            cursorShape: Qt.PointingHandCursor
            onClicked: if (skip.seg) skip.skipRequested(Number(skip.seg.endSeconds))
        }
    }
}
