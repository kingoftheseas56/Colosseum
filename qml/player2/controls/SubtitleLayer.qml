import QtQuick
import "Player2Browser.js" as Browser

// Paints the active subtitle cue. The C++ session owns cue timing (set on arrival, cleared by a C++
// timer after its duration), so this layer only renders what the session publishes — no QML timer
// decides when a cue is on screen. Text cues paint as a line; bitmap (PGS/DVD) cues paint as the
// decoded picture, served by the "player2subtitle" image provider and positioned by the cue's region.
Item {
    id: layer

    property var session
    property QtObject theme

    // --- text cue ---
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

    // --- bitmap (PGS/DVD) cue ---
    readonly property var bitmap: layer.session ? layer.session.subtitleBitmap : ({})
    readonly property bool hasBitmap: !!layer.bitmap && layer.bitmap.id !== undefined
    readonly property var bitmapBox: Browser.subtitleBitmapLayout(layer.bitmap, layer.width, layer.height)
    Image {
        visible: layer.hasBitmap && layer.bitmapBox.width > 0
        source: layer.hasBitmap ? "image://player2subtitle/" + layer.bitmap.id : ""
        x: layer.bitmapBox.x
        y: layer.bitmapBox.y
        width: layer.bitmapBox.width
        height: layer.bitmapBox.height
        fillMode: Image.Stretch   // the region already matches the picture's aspect; scale to the frame
        smooth: true
        cache: false              // each cue is a fresh id; don't retain the previous picture
        asynchronous: false       // cues are small and short-lived — show them promptly
    }
}
