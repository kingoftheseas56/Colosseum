import QtQuick
import "Player2Browser.js" as Browser

// SkipButton — the "Skip Intro / Recap / Credits" affordance. It appears only while the playhead sits
// inside a host-resolved skip segment and jumps past it on tap; an auto-skip segment fires itself once
// on entry. Pure in the Player 2 house style: segments + position in, typed seek intent out. It lives
// OUTSIDE the fading transport dock so it persists when the chrome auto-hides (like a streaming app's
// skip button). Which segment is active is derived by Player2Browser.js (headless-tested).
Item {
    id: skip
    anchors.fill: parent

    property QtObject theme
    property var segments: []
    property real positionSeconds: 0
    // `enabled` is Item's own property: the shell clears it while a menu/drawer is open, which both
    // hides the button (via `shown`) and stops it intercepting input. No shadow declaration needed.

    signal skipRequested(real toSeconds)

    readonly property var seg: Browser.activeSegment(skip.segments, skip.positionSeconds)
    readonly property bool shown: skip.enabled && skip.seg !== null

    readonly property color gold: theme ? theme.gold : "#f0c44a"
    readonly property color ink: theme ? theme.ink : "#f7f7f5"

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
        anchors.rightMargin: 40
        anchors.bottomMargin: 156   // clear of the transport dock
        width: row.implicitWidth + 40
        height: 48
        radius: 8
        color: pillMa.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0.04, 0.05, 0.07, 0.92)
        border.width: 1
        border.color: skip.gold

        opacity: skip.shown ? 1 : 0
        visible: opacity > 0.01
        // slide up a touch as it fades in
        transform: Translate { y: skip.shown ? 0 : 8; Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } } }
        Behavior on opacity { NumberAnimation { duration: 160 } }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: skip.seg ? Browser.skipLabel(skip.seg.kind) : ""
                color: skip.ink; font.family: "Segoe UI"; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Player2Icon {
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18
                kind: "nextEpisode"   // skip-forward glyph
                ink: skip.gold
                accessibleName: skip.seg ? Browser.skipLabel(skip.seg.kind) : "Skip"
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
