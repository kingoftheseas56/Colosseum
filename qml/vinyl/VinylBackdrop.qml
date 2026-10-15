import QtQuick
import QtQuick.Effects
import ".."

Item {
    id: root
    objectName: "vinylBackdrop"
    property bool wordsShown: false
    property bool tallFixture: false
    property url source
    Theme {
        id: theme
    }
    Rectangle {
        anchors.fill: parent
        color: theme.biblioWashBottom
    }
    Image {
        id: art
        anchors.fill: parent
        anchors.margins: -60
        source: root.source || Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg")
        fillMode: Image.PreserveAspectCrop
        visible: false
    }
    MultiEffect {
        anchors.fill: parent
        source: art
        blurEnabled: true
        blur: 1
        blurMax: root.tallFixture ? 24 : 64
        blurMultiplier: 0.5
        brightness: 0
        saturation: root.tallFixture ? -0.12 : -0.24
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0
                color: Qt.rgba(theme.biblioWashBottom.r, theme.biblioWashBottom.g, theme.biblioWashBottom.b, root.wordsShown ? 0.60 : 0.42)
            }
            GradientStop {
                position: 1
                color: Qt.rgba(theme.biblioWashBottom.r, theme.biblioWashBottom.g, theme.biblioWashBottom.b, root.wordsShown ? 0.84 : 0.76)
            }
        }
    }
}
