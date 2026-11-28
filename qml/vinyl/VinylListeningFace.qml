import QtQuick

// The pre-V2 widgets are baked from their original 220-unit QML. Only the
// title and artwork are live; the PNGs already contain the empty art windows.
Image {
    id: face
    required property bool cassette
    required property string title
    required property string displayFont
    required property url portrait
    required property var covers
    readonly property real unit: width / 220
    readonly property real artRadius: 10

    source: cassette ? "assets/vinyl-cassette-face.png" : "assets/vinyl-radio-face.png"
    asynchronous: true
    cache: true
    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
    smooth: true

    component LiveArtwork: Image {
        asynchronous: true
        cache: true
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        visible: status === Image.Ready
    }

    Loader {
        active: face.cassette
        visible: face.status === Image.Ready
        sourceComponent: Item {
            Text {
                x: 26 * face.unit; y: 16 * face.unit
                width: 168; height: 52
                scale: face.unit
                transformOrigin: Item.TopLeft
                text: face.title
                textFormat: Text.PlainText
                font.family: face.displayFont
                font.pixelSize: 22
                font.italic: true
                color: "#2b2a26"
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Repeater {
                model: Math.min(3, face.covers.length)
                delegate: LiveArtwork {
                    required property int index
                    readonly property real shellUnit: face.unit * 220 / 300
                    x: (40 + 76 * index) * shellUnit
                    y: 81 * face.unit + 24 * shellUnit
                    width: 68 * shellUnit; height: width
                    fillMode: Image.PreserveAspectFit
                    source: face.covers[index] || ""
                }
            }
        }
    }
    Loader {
        active: !face.cassette && String(face.portrait) !== ""
        visible: face.status === Image.Ready
        x: 23 * face.unit; y: 45 * face.unit
        width: 174 * face.unit; height: 90 * face.unit
        sourceComponent: LiveArtwork {
            fillMode: Image.PreserveAspectCrop
            clip: true
            source: face.portrait
        }
    }
}
