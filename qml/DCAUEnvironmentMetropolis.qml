import QtQuick

Item {
    id: root
    objectName: "dcauEnvironmentMetropolisV11"
    property bool reducedMotion: false

    // v11 is the visual source of truth. Keep the approved layered city artwork
    // intact instead of approximating it with generic QML building rectangles.
    Image {
        id: metropolisV11Scene
        anchors.fill: parent
        source: Qt.resolvedUrl("../assets/universes/dcau/environments/metropolis-v11.svg")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
