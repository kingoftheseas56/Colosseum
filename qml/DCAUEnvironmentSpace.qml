import QtQuick
import "UniverseContentApi.js" as UniverseContent

Item {
    id: root
    function resolveContentAsset(path) { root.universeContentGeneration; return UniverseContent.asset(path) }
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? Content.generation : 0
    objectName: "dcauEnvironmentWatchtowerV11"
    property bool reducedMotion: false

    // Locked v11 Watchtower-space oracle rendered directly from the approved HTML scene.
    // Preserve the sparse star field, blue horizon, atmosphere and deep vignette as one composition.
    Image {
        id: watchtowerV11Scene
        anchors.fill: parent
        source: root.resolveContentAsset("../assets/universes/dcau/environments/justice-v11-bg.png")
        fillMode: Image.Stretch
        sourceSize.width: 1600
        sourceSize.height: 900
        smooth: true
        mipmap: true
        cache: true
    }
}
