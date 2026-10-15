import QtQuick

// The +/-10s seek buttons: a Lucide rotate glyph (ccw = back, cw = forward) with the seek amount
// rendered as centered QML Text — never baked into the SVG (see the design spec: no SVG <text>).
// Composes PlayerIcon so the loop geometry is the exact matched Lucide pair.
Item {
    id: root

    property bool   forward: false
    property int    seconds: 10
    property color  ink: "#f7f7f5"
    // Passed in from PlayerPage (theme.hud) so this file never depends on cross-file `theme` scope.
    property string hudFamily: "Segoe UI"

    PlayerIcon {
        anchors.fill: parent
        kind: root.forward ? "seekForward" : "seekBack"
        ink: root.ink
    }
    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: Math.round(root.height * 0.03)
        text: Math.round(root.seconds)
        color: root.ink
        font.family: root.hudFamily
        font.pixelSize: Math.max(8, Math.round(Math.min(root.width, root.height) * 0.24))
        font.weight: Font.DemiBold
        font.features: ({ "tnum": 1 })   // tabular numerals so "10"/"5" stay centered
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
