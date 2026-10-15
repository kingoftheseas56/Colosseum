import QtQuick

Item {
    id: root
    objectName: "dcauEnvironmentWatchtowerV11"
    property bool reducedMotion: false

    // Locked v11 Watchtower-space oracle rendered directly from the approved HTML scene.
    // Preserve the sparse star field, blue horizon, atmosphere and deep vignette as one composition.
    Image {
        id: watchtowerV11Scene
        anchors.fill: parent
        source: Qt.resolvedUrl("../assets/universes/dcau/environments/justice-v11-bg.png")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
