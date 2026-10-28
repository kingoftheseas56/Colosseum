import QtQuick
import "UniverseContentApi.js" as UniverseContent

Item {
    id: root
    function resolveContentAsset(path) { root.universeContentGeneration; return UniverseContent.asset(path) }
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? Content.generation : 0
    objectName: "dcauEnvironmentMetropolisV11"
    property bool reducedMotion: false

    // v11 is the visual source of truth. Keep the approved layered city artwork
    // intact instead of approximating it with generic QML building rectangles.
    Image {
        id: metropolisV11Scene
        anchors.fill: parent
        source: root.resolveContentAsset("../assets/universes/dcau/environments/metropolis-v11.svg")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
