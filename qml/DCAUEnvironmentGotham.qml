import QtQuick

Item {
    id: root
    objectName: "dcauEnvironmentGothamV11"
    property bool reducedMotion: false

    // Locked v11 visual oracle rendered directly from the approved HTML scene.
    // Keep the city silhouette, crescent, beams, haze, rooftop and vignette intact.
    Image {
        id: gothamV11Scene
        anchors.fill: parent
        source: Qt.resolvedUrl("../assets/universes/dcau/environments/gotham-v11-bg.png")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
