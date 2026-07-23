import QtQuick

// Paints the active subtitle line. The C++ session owns cue timing (a cue is set on arrival and
// cleared by a C++ timer after its duration), so this layer only renders session.subtitleText —
// no QML timer decides when a cue is on screen. Bitmap (PGS) cues are handled in a later slice.
Item {
    id: layer

    property var session
    property QtObject theme

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * 0.08 // near-bottom, matching the default sub-pos
        width: parent.width * 0.82
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: layer.session ? layer.session.subtitleText : ""
        visible: text.length > 0
        color: "#ffffff"
        font.family: "Segoe UI"
        font.pixelSize: Math.max(18, parent.height * 0.045)
        font.weight: Font.DemiBold
        style: Text.Outline
        styleColor: "#000000"
    }
}
