import QtQuick
import "UniverseContentApi.js" as UniverseContent

Item {
    id: root
    function resolveContentAsset(path) { root.universeContentGeneration; return UniverseContent.asset(path) }
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? Content.generation : 0
    objectName: "dcauEnvironmentGothamV11"
    property bool reducedMotion: false

    // Locked v11 visual oracle rendered directly from the approved HTML scene.
    // Keep the city silhouette, crescent, beams, haze, rooftop and vignette intact.
    Image {
        id: gothamV11Scene
        anchors.fill: parent
        source: root.resolveContentAsset("../assets/universes/dcau/environments/gotham-v11-bg.png")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
