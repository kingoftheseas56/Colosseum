import QtQuick

Item {
    id: root
    objectName: "dcauEnvironmentFutureGothamV11"
    property bool reducedMotion: false

    // v11 is the visual source of truth. This preserves its megastructure canyon,
    // suspended rail, neon signs, scan beams, moon, haze, and skyline depth.
    Image {
        id: futureGothamV11Scene
        anchors.fill: parent
        source: Qt.resolvedUrl("../assets/universes/dcau/environments/future-v11.svg")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
